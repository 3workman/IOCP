#include "stdafx.h"
#include "ServLink.h"
#include "ServLinkMgr.h"

#pragma comment (lib,"Ws2_32.lib")
#pragma comment(lib,"Mswsock.lib")

#define MAX_Connect_Seconds	3  // �������Ӳ��ϣ��ߵ�
#define MAX_Silent_Seconds	5  // ����û�յ����ݣ�����Ƿ���socket close
#define MAX_Invalid_Seconds 5  // ������Ч�������룬closesocket������������Ͽ�����ʱ����shutdown����Ч���ӣ��ٲ���������socket��

// closesocket && shutdown
/*
	1��������Ҫ����һ�¹ر�socket�͹ر�TCP���ӵ�����
		���ر�TCP������ָTCPЭ���Ķ�������������TCP��֮�佻����һЩЭ�����FIN��RST�ȣ�
		�����ر�socket��ָ�ر��û�Ӧ�ó����е�socket������ͷ������Դ�����ǵ��û��ر�socket���ʱ�������Ĵ���TCP���ӵĹرչ���

	2��TCP���ӵĹرչ��������֣�һ�������Źرգ�graceful close����һ����ǿ�ƹرգ�hard close��abortive close��
		�����Źر���ָ��������ͻ����л�������δ�������䷢��ȥ�������յ��������ݵ�ACK֮�󣬷���FIN������ʼ�رչ���
		��ǿ�ƹر���ָ��������л������ݣ�����Щ���ݶ�����������Ȼ����RST����ֱ������TCP����

	3��shutdown()���ڹر�TCP���ӣ��������ر�socket������ڶ�������ָ���رյ��ǣ�����/����ͨ��

	4��closesocket()���ڹر�socket��������ͷ������Դ��socketѡ�SO_LINGER �����䴥����TCP�ر�������/ǿ��

	5��ΪʲôҪ��shutdown�ٵȴ���closesocket��Ϊ��֤�������ݷ���ȥ��
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
	, _recvBuf(2 * IN_BUFFER_SIZE) //_recvBuf��������С��������ջ��岻������PostRecv()��Notice
#endif
	, _pMgr(pMgr)
{
	_ovSend.eType = IO_Write;
	_ovRecv.eType = IO_Read;

	_nLinkID = ++s_nID; // LinkID��1��ʼ

	InitializeCriticalSection(&_csLock);
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

	//��brief.6������buf������������󳤶ȣ�����������ʱ���ڴ�ռ��
	_sendBuf.append(msgSize);
	_sendBuf.append(&msg, msgSize);
	//if (!_sendBuf.append(pAddedBuffer, dwAddedSize))
	//{
	//	OnInvalidMessage(Message_Overflow11, 0, true);
	//	return false;
	//}

	//��brief.7�������Ż���ͬʱҪ�������̶߳��ڷ����������ݣ�������Ϣ��ʱ
	if (_sendBuf.readableBytes() < _sendBuf.size() / 4) return true;

	if (_bCanWrite && msgSize > 0)
	{
		return PostSend(_sendBuf.beginRead(), _sendBuf.readableBytes());
	}
	return true;
}

void ServLink::DoneIOCallback(DWORD dwNumberOfBytesTransferred, EnumIO type)
{
	//���Ż�Maintain�����ѯFD_CLOSE��
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

	if (type == IO_Write){ // ����дIO����ɻص�

		if (_eState == STATE_CONNECTED) OnSend_DoneIO(dwNumberOfBytesTransferred); // ������ʱ��Ҫ�����ͻ���ĸ���(�ж����ѷ�����)

	}else if (type == IO_Read){  // �����IO����ɻص�
		if (IsInvalid())
		{
			(_eState == STATE_ACCEPTING) ? printf("Error_DoneIOCallback : Accepting ID: %d \n", _nLinkID) : printf("Error_DoneIOCallback : Connect ID: %d \n", _nLinkID);
			return;
		}

		if (_eState == STATE_ACCEPTING)  // ��1��AcceptEx��ɺ󣬻ص����ˡ�
		{
			UpdateAcceptAddr();
			if (ServLinkMgr::IsValidIP(_szIP)){
				OnConnect();            // ��2��AcceptEx��ɺ󣬰󶨿ͻ�socket����ɶ˿ڣ�״̬����Ϊ���ӡ�
			}
			else{
				OnInvalidMessage(Net_InvalidIP, 0, true);
				return;
			}
		}

		if (_eState == STATE_CONNECTED)  // ��3��AcceptEx��ɺ� �󶨿ͻ�socket�ɹ����ص����ˡ�
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
		printf("Error_CreateLink��socket being used. ID:%d \n", _nLinkID);
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
	bool noDelay = true;
	if (setsockopt(_sClient, IPPROTO_TCP, TCP_NODELAY, (char*)&noDelay, sizeof(noDelay)) == SOCKET_ERROR)
	{
		printf("setsockopt() failed with TCP_NODELAY");
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

	printf("CreateLinkAndAccept socket - ID:%d\n", _nLinkID);
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
		1������ l_onoffΪ0�����ѡ��رգ�l_linger��ֵ�����ԣ������ں�ȱʡ���
		   close���û��������ظ������ߣ�������ܽ��ᴫ���κ�δ���͵�����
		2������ l_onoffΪ��0��l_lingerΪ0�����׽ӿڹر�ʱ
		   TCP�������������׽ӿڷ��ͻ������е��κ����ݲ�����һ��RST���Է���������ͨ�����Ĵ�������ֹ���������TIME_WAIT״̬
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
	_nCharID = -1; // �ر�����ʱ,����Ӧ�Ľ�ɫid���,��ֹ����������Ľ�ɫ�� NEX-18609

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
		pBuf,	//���ݸ�AcceptEx���ǿ��ڴ�
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

//ע�⣬��DoneIO���̲߳��ܵ���close�������������ֻ�����ڷ�������maintain�߳�close
void ServLink::Maintain(time_t timenow)
{
	/* Check if client is an abuser. Abusing clients are:
	1. Clients that connect without sending data, thus not allowing AcceptEx to return.
	2. Clients that connect, send something, and remain connected for too long.
	(there are other kinds of abusive clients, but only these two kinds need to be looked for at a periodic manner.)*/

	if (IsInvalid() && (timenow - _timeInvalid > 1)) // ����ֵ�"> 1" ~�壿
	{
		//û�е�½��û����Ϸ��û�����ӵ�ʱ�����CloseLink
		//����һ����������1��������û�з�����Ϣ����CloseLink
		if (!IsConnected())
		{
			CloseLink();
		}
		else if (timenow - _timeInvalid > MAX_Invalid_Seconds) // ����������
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
			// ���Խ����ʾ���ͻ��˽���connect�����������ݣ����ᴥ��DoneIO�ص�
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
		//����DoneIOCallback�е��Ż���
		else if (RecvIOElapsed(timenow) > MAX_Silent_Seconds && _hEventClose)
		{
			// Ŀǰ��DoneIO��dwNumberOfBytesTransferred = 0����shutdown����Ч�ģ��ȴ���CloseLink
			// ��Щ����ǣ���Ȼ����Ϊ0�ˣ��������粻һ���ͶϿ�����0�Ͽ�������
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
	//��ǰ��Ч����Ҫ�����ͻ���
	if (!_bInvalid && bToClient)
	{
		stMsg msg;
		//msg.eReason = e;
		//msg.nErrorCode = nErrorCode;
		//msg.nParam = nParam;
		SendMsg(msg, sizeof(msg));
	}

	if (InterlockedExchange(&_bInvalid, 1) == 1) return; // ��ֵ������Ч��

	_eLastError = e;

	Invalid(e);
	{
		stMsg msg;
		//msg.eReason = e;
		HandleNetMessage(&msg, sizeof(msg));
	}
}

/*
	ÿ�η���::WSASend()����DoneIO�ص����ˣ�����ѷ��͵��ֽ�(�ѿ���������)
	ServerRun_SendIO��ѭ�����ܻ�����������OnSend_DoneIO�ﲹ��ʣ�ಿ��
*/
void ServLink::OnSend_DoneIO(DWORD dwNumberOfBytesTransferred)
{
	cLock lock(_csLock);

	_bCanWrite = true;

	if (_sendBuf.readableBytes() < dwNumberOfBytesTransferred) //��Ϣ����С��ʵ�ʲ������ֽ���
	{
		assert(0);
		OnInvalidMessage(Message_NoError, 0, false);
		return;
	}

	_sendBuf.readerMove(dwNumberOfBytesTransferred); //�����ƶ���Ϣָ�룬�������Ͷ��ಿ��

	/*
		PostSend()ֻ�������ô���ҵ���SendMsg�������߳�ServerRun_SendIO��DoneIO�ص��Ĳ���
		�����᲻�ή�����ܣ��Ͼ�PostSend()��һ�������ģ�������Ҫ��~
		���˸����߳�ServerRun_SendIO()������Ĳ���ò�ƿ���ʡ��
		���򣬱ز����٣����磺ҵ��㷢�����һ��msg����һ��io��δ����
	*/
	if (DWORD nLeft = _sendBuf.readableBytes())
	{
		PostSend(_sendBuf.beginRead(), nLeft);
	}
}
void ServLink::ServerRun_SendIO() //��brief.7������̶߳��ڷ�������buffer
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
	//Notice�����ȹ�������socket������������[TCP Zerowindow]
	WSABUF wbuf;
	wbuf.buf = buffer;
	wbuf.len = nLen;
	DWORD dwBytes(0);
	//���߳�BUG���ڴ�֮ǰ���ܸı���״̬
	int ret = WSASend(_sClient, &wbuf, 1, &dwBytes, 0, &_ovSend, 0);

	// �����в������ٷ��ͣ�һ��IO���(DoneIO�ص�)��Ϊtrue
	// �ۼƷ��ͣ����м�ĳ��ֻ�����˲������ݣ������ݴ�����
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
		//memset((char *)&_ovRecv, 0, sizeof(OVERLAPPED)); //���ovͷò��û��Ҫ

		//Notice��len�ǹ̶��ģ����bufָ����ڴ��ʵ��û��ô�����ڴ�Խ����գ�����_recvBuf����������С��
		//Notice������̫�̵�������ʧ
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

	_nRecvSize += dwBytesTransferred; // IO��ɻص��������ֽڵ���
	const DWORD c_off = sizeof(DWORD);
	char* pPack = _recvBuf;
	while (_nRecvSize >= c_off)
	{
		const DWORD c_msgSize = *((DWORD*)pPack);   // ���������ͷ4�ֽ�Ϊ��Ϣ���С��
		const DWORD c_packSize = c_msgSize + c_off; // ��һ����Ϣ����С = �����ֽ� + ��Ϣ���С��
		const char* pMsg = pPack + c_off;           // ������4�ֽڵã���Ϣ��ָ�롿

		// 1�������Ϣ��С
		if (Config().nMaxPackage && c_msgSize >= Config().nMaxPackage) //��Ϣ̫��
		{
			_nRecvSize = 0;
			printf("TooHugePacket: Msg size %d Msg type %d", c_msgSize, *((DWORD*)pMsg)); // ����Ϣ�壺ͷ4�ֽ�Ϊ��Ϣ���͡�
			OnInvalidMessage(Message_TooHugePacket, 0, true);
			return _recvBuf;
		}
		// 2���Ƿ�ӵ�������
		if (c_packSize > _nRecvSize) break;         // ����δ���꣺�����ֽ� < ����С��

		// 3����Ϣ���롢���� decode, unpack and ungroup
		RecvMsg(pMsg, c_msgSize);

		// 4����Ϣ������ϣ������ֽ�/��ָ�����(������һ����)
		_nRecvSize -= c_packSize;
		pPack += c_packSize;
	}

	// 5��δ����Ļ�������(��������)��ǰ��(�������������Щpack)
	if (pPack != _recvBuf && _nRecvSize > 0)
	{
		memmove(_recvBuf, pPack, _nRecvSize); // net::Buffer�ɽ�ʡmemmove��ֻ���ڴ治��ʱ���ŻὫ�����ƶ���ͷ������Buffer::makeSpace
	}

	// 5�����ؿ��û�����(ǰ����ѱ�д��)
	return _recvBuf + _nRecvSize;
}
#else
char* ServLink::OnRead_DoneIO(DWORD dwBytesTransferred)
{
	LastRecvIOTime(_pMgr->_timeNow);

	_recvBuf.writerMove(dwBytesTransferred); // IO��ɻص��������ֽڵ���
	const DWORD c_off = sizeof(DWORD);
	char* pPack = _recvBuf.beginRead();
	while (_recvBuf.readableBytes() >= c_off)
	{
		const DWORD c_msgSize = *((DWORD*)pPack);	// ���������ͷ4�ֽ�Ϊ��Ϣ���С��
		const DWORD c_packSize = c_msgSize + c_off;	// ��������� = ��Ϣ���С + ͷ���ȡ�
		const char* pMsg = pPack + c_off;           // ������4�ֽڵã���Ϣ��ָ�롿

		// 1�������Ϣ��С
		if (Config().nMaxPackage && c_msgSize >= Config().nMaxPackage) //��Ϣ̫��
		{
			_recvBuf.clear();
			printf("TooHugePacket: Msg size %d Msg type %d", c_msgSize, *((DWORD*)pMsg)); // ����Ϣ�壺ͷ4�ֽ�Ϊ��Ϣ���͡�
			OnInvalidMessage(Message_TooHugePacket, 0, true);
			return _recvBuf.beginWrite();
		}
		// 2���Ƿ�ӵ�������
		if (c_packSize > _recvBuf.readableBytes()) break; // ����δ���꣺�����ֽ� < ����С��

		// 3����Ϣ���롢���� decode, unpack and ungroup
		RecvMsg(pMsg, c_msgSize);

		// 4����Ϣ������ϣ������ֽ�/��ָ�����(������һ����)
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

	//��brief.4������client����Ƶ��
	++_recvPacket;
	if (Config().nRecvPacketCheckTime && Config().nRecvPacketLimit &&		// ������Ч
		_pMgr->_timeNow - _recvPacketTime >= Config().nRecvPacketCheckTime) // �����ʱ����
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