#include "stdafx.h"
#include "ServLink.h"
#include "ServLinkMgr.h"

#pragma comment (lib,"Ws2_32.lib")
#pragma comment(lib,"Mswsock.lib")

#define MAX_Connect_Seconds	3  // 几秒连接不上，踢掉
#define MAX_Silent_Seconds	5  // 几秒没收到数据，检查是否发生socket close
#define MAX_Invalid_Seconds 20 // 链接无效持续几秒，closesocket【本程序中需断开链接时，先shutdown成无效链接，再才真正回收socket】

// closesocket && shutdown
/*
	1、首先需要区分一下关闭socket和关闭TCP连接的区别：
		·关闭TCP连接是指TCP协议层的东西，就是两个TCP端之间交换了一些协议包（FIN，RST等）
		·而关闭socket是指关闭用户应用程序中的socket句柄，释放相关资源。但是当用户关闭socket句柄时会隐含的触发TCP连接的关闭过程

	2、TCP连接的关闭过程有两种，一种是优雅关闭（graceful close），一种是强制关闭（hard close或abortive close）
		·优雅关闭是指，如果发送缓存中还有数据未发出则其发出去，并且收到所有数据的ACK之后，发送FIN包，开始关闭过程
		·强制关闭是指如果缓存中还有数据，则这些数据都将被丢弃，然后发送RST包，直接重置TCP连接

	3、shutdown()用于关闭TCP连接，但并不关闭socket句柄，第二个参数指定关闭的是：发送/接受通道

	4、closesocket()用于关闭socket句柄，并释放相关资源，socket选项：SO_LINGER 决定其触发的TCP关闭是优雅/强制

	5、为什么要先shutdown再等待后closesocket？为保证缓冲数据发出去吗
*/

const DWORD IN_BUFFER_SIZE = 1024 * 4;
const DWORD OUT_BUFFER_SIZE = 1024 * 8;
const DWORD MAX_SEND = 1024 * 48;
const DWORD MAX_MESSAGE_LENGTH = 1024 * 1024;

WORD ServLink::s_nID = 0;

ServLink::ServLink(ServLinkMgr* pMgr, DWORD sendbuffsize)
	: _sendBuf(sendbuffsize)
#ifdef _Use_ArrayBuf
	, _recvBuf(new char[2 * IN_BUFFER_SIZE])
#else
	, _recvBuf(2 * IN_BUFFER_SIZE) //_recvBuf开两倍大小，避免接收缓冲不够，见PostRecv()的Notice
#endif
	, _pMgr(pMgr)
{
	_sClient = INVALID_SOCKET;

	_ovSend.eType = IO_Write;
	_ovRecv.eType = IO_Read;

	_eState = STATE_DEAD;			// unsafe

	InitializeCriticalSection(&_csLock);

	_nLinkID = ++s_nID; // LinkID从1开始

	_eLastError = Message_NoError;

	_bCanWrite = true;

	_timeInvalid = 0;
}
ServLink::~ServLink()
{
#ifdef _Use_ArrayBuf
	if (_recvBuf) delete[]_recvBuf;
#endif

	DeleteCriticalSection(&_csLock);
}

bool ServLink::SendMsg(stMsg& msg, DWORD msgSize)
{
	if (IsInvalid()) return false;

	if (msgSize >= MAX_SEND)
	{
		OnInvalidMessage(Message_Overflow7, 0, true);
		return false;
	}

	cLock lock(_csLock);

	msg.len = msgSize;

	//【brief.6】限制buf能增长到的最大长度，避免整理延时的内存占用
	_sendBuf.append(&msg, msgSize);

	//if (!_sendBuf.append(pAddedBuffer, dwAddedSize))
	//{
	//	OnInvalidMessage(Message_Overflow11, 0, true);
	//	return false;
	//}

	//【brief.7】消息积累超过定长后才调底层API，同时要在其它线程定期发送所有数据，避免消息延时
	if (_sendBuf.readableBytes() < _sendBuf.size() / 4) return true;

	if (_bCanWrite && msgSize > 0)
	{
		return PostSend(_sendBuf.beginRead(), _sendBuf.readableBytes());
	}
	return true;
}

void ServLink::DoneIOCallback(DWORD dwNumberOfBytesTransferred, EnumIO type)
{
	//【优化Maintain里的轮询FD_CLOSE】
	if (0 == dwNumberOfBytesTransferred)
	{
		WSANETWORKEVENTS events;
		if (WSAEnumNetworkEvents(_sClient, _hEventClose, &events) == 0)
		{
			if (events.lNetworkEvents & FD_CLOSE)
			{
				printf("Maintenance FD_CLOSE shutdown socket ID:%d \n", _nLinkID);
				OnInvalidMessage(Net_Dead, 0, false);
			}
		}
		else{
			Err("ErrorAPI_WSAEnumNetworkEvents");
		}
		return;
	}

	//if (0 == dwNumberOfBytesTransferred){
	//	// DoneIO的数据为0就断开，不会误伤吗？
	//	OnInvalidMessage(DoneIO_ZeroByte, 0, false);
	//	return;
	//}

	if (type == IO_Write){ // 处理写IO的完成回调

		if (_eState == STATE_CONNECTED) OnSend_DoneIO(dwNumberOfBytesTransferred); // 仍连接时，要处理发送缓冲的更新(有东西已发走了)

	}else if (type == IO_Read){  // 处理读IO的完成回调
		if (IsInvalid())
		{
			(_eState == STATE_ACCEPTING) ? printf("Error_DoneIOCallback : Accepting ID: %d \n", _nLinkID) : printf("Error_DoneIOCallback : Connect ID: %d \n", _nLinkID);
			return;
		}

		if (_eState == STATE_ACCEPTING)  // 【1、AcceptEx完成后，回调至此】
		{
			UpdateAcceptAddr();
			if (ServLinkMgr::IsValidIP(_szIP)){
				OnConnect();            // 【2、AcceptEx完成后，绑定客户socket到完成端口，状态更新为连接】
			}
			else{
				OnInvalidMessage(Net_InvalidIP, 0, true);
				return;
			}
		}

		if (_eState == STATE_CONNECTED)  // 【3、AcceptEx完成后 绑定客户socket成功，回调至此】
		{
			char* buf = OnRead_DoneIO(dwNumberOfBytesTransferred);
			PostRecv(buf);
		}
		else{
			printf("Error_DoneIOCallback : NotConnect ID: %d \n", _nLinkID);
		}
	}
}
bool ServLink::CreateLinkAndAccept()
{
	if (_sClient != INVALID_SOCKET)
	{
		printf("Error_CreateLink：socket being used. ID:%d \n", _nLinkID);
		return false;
	}

	_ovSend.SetLink(this);
	_ovRecv.SetLink(this);

	_bCanWrite = true;

	_timeInvalid = 0;

	_sendBuf.clear();

#ifdef _Use_ArrayBuf
	_nRecvSize = 0;
	char* pBuf = &_recvBuf[0];
#else
	_recvBuf.clear();
	char* pBuf = _recvBuf.beginWrite();
#endif

	strcpy_s(_szIP, "Unknow");

	_bInvalid = false;
	_eState = STATE_ACCEPTING;	// unsafe
	_eLastError = Message_NoError;

	_sClient = socket(AF_INET, SOCK_STREAM, 0);
	if (_sClient == INVALID_SOCKET)
	{
		Err("ErrorAPI_createlink_socket");
		goto fail;
	}

	DWORD dwBytes(0);
	if (!AcceptEx(_pMgr->GetListener(), _sClient,
		pBuf,
		IN_BUFFER_SIZE - (sizeof(sockaddr_in) + 16) * 2,
		sizeof(sockaddr_in) + 16,
		sizeof(sockaddr_in) + 16,
		&dwBytes, &_ovRecv))
	{
		int err = WSAGetLastError();
		if (err != ERROR_IO_PENDING)
		{
			Err("ErrorAPI_createlink_Acceptex", err);
			closesocket(_sClient);
			goto fail;
		}
	}
	//OnConnect may happened here...
	_pMgr->LinkOnCreate(_nLinkID);

	_hEventClose = WSACreateEvent();
	if (_hEventClose == WSA_INVALID_EVENT)
	{
		Err("ErrorAPI_createlink_CreateEvent");
	}
	else if (WSAEventSelect(_sClient, _hEventClose, FD_CLOSE) == SOCKET_ERROR)
	{
		Err("ErrorAPI_createlink_EventSelect");
		CloseHandle(_hEventClose);
		_hEventClose = NULL;
	}

	printf("CreateLinkAndAccept socket IP:%s - ID:%d\n", _szIP, _nLinkID);
	return true;
fail:
	_bInvalid = true;
	_eState = STATE_DEAD;	// unsafe
	return false;
}
void ServLink::OnConnect()	// state Move from ACCEPTING to CONNECTED
{
	assert(_eState == STATE_ACCEPTING);
	m_dwLastHeart = -1;

	//if (m_msgSend.GetSize() != m_nSendSize)
	//{
	//	m_msgSend.ResizeBuffer(m_nSendSize);
	//}

	if (BindIoCompletionCallback((HANDLE)_sClient, DoneIO, 0)){
		_eState = STATE_CONNECTED;
		RecvIOTimeStart(_pMgr->_timeNow);
		_pMgr->LinkOnConnect(_nLinkID);
		printf("Connect -- id:%d, IP:%s \n", _nLinkID, _szIP);
	}else{
		int error = GetLastError();
		Err("ErrorAPI_onconnect_BindIO: %d - %d", error);
		OnInvalidMessage(Net_BindIO, error, false);
	}
}
bool ServLink::CloseLink()
{
	if (_sClient == INVALID_SOCKET)
	{
		Err("Error_CloseLink", 0);
		return false;
	}
	switch (_eState){
	case STATE_ACCEPTING:
		UpdateAcceptAddr();
		_pMgr->LinkOnAcceptClose(_nLinkID);
		printf("CloseAccept -- id:%d, IP:%s \n", _nLinkID, _szIP);
		break;
	case STATE_CONNECTED:
		_pMgr->LinkOnClose(_nLinkID);
		printf("Close -- id:%d, IP:%s \n", _nLinkID, _szIP);
		break;
	default:
		Err("Error_CloseUnknow", 0);
	}

	/*
		1、设置 l_onoff为0，则该选项关闭，l_linger的值被忽略，等于内核缺省情况
		   close调用会立即返回给调用者，如果可能将会传输任何未发送的数据
		2、设置 l_onoff为非0，l_linger为0，则套接口关闭时
		   TCP将丢弃保留在套接口发送缓冲区中的任何数据并发送一个RST给对方，而不是通常的四次握手终止，这避免了TIME_WAIT状态
	*/
	struct linger li = { 1, 0 };	// Default: SO_DONTLINGER
	if (setsockopt(_sClient, SOL_SOCKET, SO_LINGER, (char *)&li, sizeof(li)) == SOCKET_ERROR)
		Err("ErrorAPI_closelink_setsockopt");

	if (closesocket(_sClient) == SOCKET_ERROR)
		Err("ErrorAPI_closelink_closesocket");

	_sClient = INVALID_SOCKET;

	if (_hEventClose) {
		CloseHandle(_hEventClose);
		_hEventClose = NULL;
	}
	_eState = STATE_DEAD;
	_nCharID = -1; // 关闭链接时,将对应的角色id清除,防止关联到错误的角色上 NEX-18609

	//if (m_msgSend.GetSize() > OUT_BUFFER_SIZE)
	//{
	//	m_msgSend.ResizeBuffer(OUT_BUFFER_SIZE);
	//}

	return true;
}
void ServLink::UpdateAcceptAddr()
{
	int locallen, remotelen;
	sockaddr_in *plocal = NULL, *premote = NULL;

#ifdef _Use_ArrayBuf
	char* pBuf = &_recvBuf[0];
#else
	char* pBuf = _recvBuf.beginWrite();
#endif

	GetAcceptExSockaddrs(
		pBuf,	//传递给AcceptEx的那块内存
		IN_BUFFER_SIZE - (sizeof(sockaddr_in) + 16) * 2,
		sizeof(sockaddr_in) + 16,
		sizeof(sockaddr_in) + 16,
		(sockaddr **)&plocal,
		&locallen,
		(sockaddr **)&premote,
		&remotelen);

	memcpy(&_local, plocal, sizeof(sockaddr_in));
	memcpy(&_peer, premote, sizeof(sockaddr_in));

	strcpy_s(_szIP, inet_ntoa(_peer.sin_addr));
	//_port = _peer.sin_port;
}
const ServerConfig& ServLink::Config(){ return _pMgr->_config; }

//注意，在DoneIO的线程不能调用close，否则会死锁，只可以在服务器的maintain线程close
void ServLink::Maintain(time_t timenow)
{
	/* Check if client is an abuser. Abusing clients are:
	1. Clients that connect without sending data, thus not allowing AcceptEx to return.
	2. Clients that connect, send something, and remain connected for too long.
	(there are other kinds of abusive clients, but only these two kinds need to be looked for at a periodic manner.)*/

	if (IsInvalid() && (timenow - _timeInvalid > 1)) // 很奇怪的"> 1" ~囧？
	{
		//没有登陆，没有游戏，没有连接的时候才能CloseLink
		//还有一条，并且在1分钟以上没有发送消息才能CloseLink
		if (!IsConnected())
		{
			CloseLink();
		}
		else if (timenow - _timeInvalid > MAX_Invalid_Seconds) // 外网三分钟
		{
			if (IsConnected()) Err("TimeInvalid 180s", 0);

			CloseLink();
		}
		return;
	}

	int nSeconds = 0;
	int nBytes = sizeof(nSeconds);

	// See if client has attempted to connect without sending data.
	if (_eState == STATE_ACCEPTING)
	{
		if (0 == getsockopt(_sClient,
			SOL_SOCKET,
			SO_CONNECT_TIME,
			(char*)&nSeconds,
			&nBytes))
		{
			// 测试结果显示：客户端仅仅connect但不发送数据，不会触发DoneIO回调
			// Client has been connected for nSeconds so far
			if (nSeconds > MAX_Connect_Seconds)
			{
				OnInvalidMessage(Net_ConnectNotSend, 0, false);
			}
		}else{
			Err("ErrorAPI_mainten_getsockopt");
		}
	}
	else if (_eState == STATE_CONNECTED)
	{
/*
		if (false == GetDecodeFlag()
			&& Config().DecodeWaitTime
			&& GetConnectT() + Config().DecodeWaitTime < GetTickCount())
		{
			printf("Not Decode TimeOut%d - %d \n", GetConnectT(), _nLinkID);
			OnInvalidMessage(Net_IdleTooLong, 0, true);
		}
		else*/ if (Config().nDeadTime && RecvIOElapsed(timenow) > Config().nDeadTime)
		{
			printf("DeadTime%d - %d \n", RecvIOElapsed(timenow), _nLinkID);
			OnInvalidMessage(Net_IdleTooLong, 0, true);
		}
/*
		//【见DoneIOCallback中的优化】
		else if (RecvIOElapsed(timenow) > MAX_Silent_Seconds && _hEventClose)
		{
			// 目前是DoneIO“dwNumberOfBytesTransferred = 0”则shutdown成无效的，等待后CloseLink
			// 有些情况是：虽然数据为0了，但是网络不一定就断开，判0断开会误伤
			WSANETWORKEVENTS events;
			if (WSAEnumNetworkEvents(_sClient, _hEventClose, &events) == 0)
			{
				if (events.lNetworkEvents & FD_CLOSE)
				{
					printf("Maintenance FD_CLOSE shutdown socket ID:%d \n", _nLinkID);
					OnInvalidMessage(Net_Dead, 0, false);
				}
			}else{
				Err("ErrorAPI_WSAEnumNetworkEvents");
			}
		}*/
	}
}

void ServLink::OnInvalidMessage(InvalidMessageEnum e, int nErrorCode, bool bToClient, int nParam/* = 0*/)
{
	//当前有效并且要发给客户端
	if (!_bInvalid && bToClient)
	{
		stMsg msg;
		//msg.eReason = e;
		//msg.nErrorCode = nErrorCode;
		//msg.nParam = nParam;
		SendMsg(msg, sizeof(msg));
	}

	if (InterlockedExchange(&_bInvalid, 1) == 1) return; // 旧值已是无效的

	_eLastError = e;

	Invalid(e);
	{
		stMsg msg;
		//msg.eReason = e;
		HandleNetMessage(&msg, sizeof(msg));
	}
}

/*
	每次发包::WSASend()都会DoneIO回调至此：清除已发送的字节(已拷入网卡了)
	ServerRun_SendIO的循环可能会慢，所以在OnSend_DoneIO里补发剩余部分
*/
void ServLink::OnSend_DoneIO(DWORD dwNumberOfBytesTransferred)
{
	cLock lock(_csLock);

	_bCanWrite = true;

	if (_sendBuf.readableBytes() < dwNumberOfBytesTransferred) //消息长度小于实际操作的字节数
	{
		assert(0);
		OnInvalidMessage(Message_NoError, 0, false);
		return;
	}

	_sendBuf.readerMove(dwNumberOfBytesTransferred); //否则，移动消息指针，继续发送多余部分

	/*
		PostSend()只有三调用处：业务层SendMsg、辅助线程ServerRun_SendIO、DoneIO回调的补发
		补发会不会降低性能？毕竟PostSend()是一个个来的，其它两要等~
		有了辅助线程ServerRun_SendIO()，这里的补发貌似可以省掉
		否则，必不可少，比如：业务层发了最后一条msg，但一次io并未发完
	*/
	if (DWORD nLeft = _sendBuf.readableBytes())
	{
		PostSend(_sendBuf.beginRead(), nLeft);
	}
}
void ServLink::ServerRun_SendIO() //【brief.7】另辟线程定期发送所有buffer
{
	cLock lock(_csLock);

	DWORD nLen = _sendBuf.readableBytes();
	if (_bCanWrite && nLen > 0)
	{
		PostSend(_sendBuf.beginRead(), nLen);
	}
}
bool ServLink::PostSend(char* buffer, DWORD nLen)
{
	//Notice：长度过大塞满socket缓冲区，甚至[TCP Zerowindow]
	WSABUF wbuf;
	wbuf.buf = buffer;
	wbuf.len = nLen;
	DWORD dwBytes(0);
	//多线程BUG，在此之前不能改变标记状态
	int ret = WSASend(_sClient, &wbuf, 1, &dwBytes, 0, &_ovSend, 0);

	// 发送中不允许再发送，一次IO完成(DoneIO回调)置为true
	// 累计发送，若中间某个只发送了部分数据，就数据错乱了
	_bCanWrite = false;

	if (SOCKET_ERROR == ret)
	{
		int nLastError = WSAGetLastError();
		if (nLastError != ERROR_IO_PENDING)
		{
			printf("WSASendError %x - %d", nLastError, _nLinkID);
			OnInvalidMessage(Message_Write, nLastError, false);
			return false;
		}
	}
	return true;
}
bool ServLink::PostRecv(char* buf)
{
	if (_eState == STATE_CONNECTED)
	{
		//memset((char *)&_ovRecv, 0, sizeof(OVERLAPPED)); //清空ov头貌似没必要

		//Notice：len是固定的，如果buf指向的内存块实际没这么大，有内存越界风险（所以_recvBuf开了两倍大小）
		//Notice：长度太短的性能损失
		WSABUF wbuf;
		wbuf.buf = buf;
		wbuf.len = IN_BUFFER_SIZE;   //dont read so much...
		DWORD dwBytes(0), dwFlags(0);
		if (WSARecv(_sClient, &wbuf, 1, &dwBytes, &dwFlags, &_ovRecv, 0) == SOCKET_ERROR)
		{
			int nLastError = WSAGetLastError();
			if (nLastError != ERROR_IO_PENDING)
			{
				printf("ReadError %x - %d", nLastError, _nLinkID);
				OnInvalidMessage(Message_Read, nLastError, false);
			}
		}
		return true;
	}
	return false;
}

#ifdef _Use_ArrayBuf
char* ServLink::OnRead_DoneIO(DWORD dwBytesTransferred)
{
	LastRecvIOTime(_pMgr->_timeNow);

	_nRecvSize += dwBytesTransferred; // IO完成回调，接收字节递增
	const DWORD c_off = sizeof(DWORD);
	char* pPack = _recvBuf;
	while (_nRecvSize >= c_off)
	{
		const DWORD c_msgSize = *((DWORD*)pPack);   // 【网络包：头4字节为消息体大小】
		const DWORD c_packSize = c_msgSize + c_off; // 【一个消息包大小 = 长度字节 + 消息体大小】
		const char* pMsg = pPack + c_off;           // 【后移4字节得：消息体指针】

		// 1、检查消息大小
		if (Config().nMaxPackage && c_msgSize >= Config().nMaxPackage) //消息太大
		{
			_nRecvSize = 0;
			printf("TooHugePacket: Msg size %d Msg type %d", c_msgSize, *((DWORD*)pMsg)); // 【消息体：头4字节为消息类型】
			OnInvalidMessage(Message_TooHugePacket, 0, true);
			return _recvBuf;
		}
		// 2、是否接到完整包
		if (c_packSize > _nRecvSize) break;         // 【包未收完：接收字节 < 包大小】

		// 3、消息解码、处理 decode, unpack and ungroup
		RecvMsg(pMsg, c_msgSize);

		// 4、消息处理完毕，接收字节/包指针更新(处理下一个包)
		_nRecvSize -= c_packSize;
		pPack += c_packSize;
	}

	// 5、未处理的缓存内容(非完整包)，前移(抛弃处理掉的那些pack)
	if (pPack != _recvBuf && _nRecvSize > 0)
	{
		memmove(_recvBuf, pPack, _nRecvSize); // net::Buffer可节省memmove，只在内存不足时，才会将数据移动至头部，见Buffer::makeSpace
	}

	// 5、返回可用缓冲区(前面的已被写了)
	return _recvBuf + _nRecvSize;
}
#else
char* ServLink::OnRead_DoneIO(DWORD dwBytesTransferred)
{
	LastRecvIOTime(_pMgr->_timeNow);

	_recvBuf.writerMove(dwBytesTransferred); // IO完成回调，接收字节递增
	const DWORD c_off = sizeof(DWORD);
	char* pPack = _recvBuf.beginRead();
	while (_recvBuf.readableBytes() >= c_off)
	{
		const DWORD c_packSize = *((DWORD*)pPack);	// 【网络包：头4字节为包大小】
		const DWORD c_msgSize = c_packSize - c_off;	// 【消息体大小 = 网络包长 - 头长度】
		const char* pMsg = pPack + c_off;           // 【后移4字节得：消息体指针】

		// 1、检查消息大小
		if (Config().nMaxPackage && c_msgSize >= Config().nMaxPackage) //消息太大
		{
			_recvBuf.clear();
			printf("TooHugePacket: Msg size %d Msg type %d", c_msgSize, *((DWORD*)pMsg)); // 【消息体：头4字节为消息类型】
			OnInvalidMessage(Message_TooHugePacket, 0, true);
			return _recvBuf.beginWrite();
		}
		// 2、是否接到完整包
		if (c_packSize > _recvBuf.readableBytes()) break; // 【包未收完：接收字节 < 包大小】

		// 3、消息解码、处理 decode, unpack and ungroup
		RecvMsg(pMsg, c_msgSize);

		// 4、消息处理完毕，接收字节/包指针更新(处理下一个包)
		_recvBuf.readerMove(c_packSize);
		pPack += c_packSize;
	}
	return _recvBuf.beginWrite();
}
#endif
int ServLink::RecvMsg(const char* buffer, DWORD size)
{
	if (IsInvalid()) return -1;

	OnHeartMsg();

	stMsg* pMsg = (stMsg*)buffer;
	HandleClientMessage(pMsg, size);

	//【brief.4】限制client发包频率
	++_recvPacket;
	if (Config().nRecvPacketCheckTime && Config().nRecvPacketLimit &&		// 配置有效
		_pMgr->_timeNow - _recvPacketTime >= Config().nRecvPacketCheckTime) // 到检查时间了
	{
		if (_recvPacket < Config().nRecvPacketLimit)
		{
			_recvPacketTime = _pMgr->_timeNow;
			_recvPacket = 0;
		}else{
			printf("Recieve %d Packet in Time %d", _recvPacket, _pMgr->_timeNow - _recvPacketTime);
			OnInvalidMessage(Message_TooMuchPacket, 0, true);
			_recvPacketTime = _pMgr->_timeNow;
			_recvPacket = 0;
			return -1;
		}
	}
	return 0;
}