/***********************************************************************
* @ 客户端IOCP
* @ brief
	1、将Net/client中的文件另起工程，即可测试
	2、对比server端的代码结构，加深理解
* @ author zhoumf
* @ date 2016-7-19
************************************************************************/
#pragma  once
//////////////////////////////////////////////////////////////////////////
// 使用winsock2  避免同winsock冲突
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
//////////////////////////////////////////////////////////////////////////
#include <mswsock.h>
#include "..\..\tool\cLock.h"
#include "..\..\tool\buffer.h"
#include "config_client.h"

class ClientLink;

struct stMsg{
};

enum EnumIO{ IO_Write, IO_Read };

struct My_OVERLAPPED : public OVERLAPPED
{
	ClientLink*	client;
	EnumIO		eType;

	void SetLink(ClientLink* p)
	{
		memset((char *)this, 0, sizeof(OVERLAPPED));
		client = p;
	}
};

class ClientLink{
	enum EStatus { State_Close, State_Connecting, State_Connected };
public:
	static void CALLBACK DoneIO(DWORD, DWORD, LPOVERLAPPED);

	ClientLink(const ClientLinkConfig& info);
	~ClientLink();

	static bool InitWinsock();
	static bool CleanWinsock();

	void DoneIOCallback(DWORD dwNumberOfBytesTransferred, EnumIO eFlag);

	bool CreateLinkAndConnect();
	BOOL ConnectEx();
	void OnConnect();
	bool IsConnect(){ return _eState == State_Connected; }
	void CloseClient(int nErrorCode);

	bool PostSend(char* buffer, DWORD nLen);
	bool PostRecv(char* buffer, DWORD nLen);

	void OnSend_DoneIO(DWORD dwBytesTransferred);
	void OnRead_DoneIO(DWORD dwBytesTransferred);
	void RecvMsg(const char* buffer, DWORD size);

	void SendMsg(stMsg& msg, DWORD size);

	void HandleServerMessage(stMsg* p, DWORD size){ printf("Echo: %s\n", (char*)p); /*放入主循环消息队列*/ }

private:
	EStatus _eState = State_Close;
	SOCKET _sClient = INVALID_SOCKET;
	My_OVERLAPPED _ovRecv;
	My_OVERLAPPED _ovSend;

	net::Buffer _recvBuf;
	net::Buffer _sendBuf;

	bool _bCanWrite = true;

	CRITICAL_SECTION _csRead;
	CRITICAL_SECTION _csWrite;

	const ClientLinkConfig& _config;
};

/************************************************************************/
// 示例
#ifdef _MY_Test
struct TestMsg : public stMsg
{
	char data[128];
	int size(){ return offsetof(TestMsg, data) + strlen(data) + 1; }
};
void RunClientIOCP()
{
	cout << "———————————— RunClientIOCP ————————————" << endl;
	TestMsg msg;
	ClientLinkConfig config;
	ClientLink link(config);

	ClientLink::InitWinsock();

	link.CreateLinkAndConnect();

	while (!link.IsConnect()) Sleep(1000); // 等待ConnectEx三次握手完成的回调，之后才能发数据

	// 立即发送一条数据，及时触发服务器端的AcceptExDoneIOCallback
	// 测试结果显示：客户端仅仅connect但不发送数据，不会触发服务器DoneIO回调
	// 真实环境下是会发数据的，不会只Connect
	// 客户端connect，三次握手成功后，在对端被放入“呼入连接请求队列”，尚未被用户进程接管，但client这边已经能发数据了
	strcpy_s(msg.data, "道友，你可听过醉寒江么？");
	link.SendMsg(msg, msg.size());
	cout << "请输入发送内容..." << endl;

	while (true)
	{
		cin >> msg.data;
		link.SendMsg(msg, msg.size());
	}
	link.CloseClient(0);
}
#endif