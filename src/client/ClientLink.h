/***********************************************************************
* @ �ͻ���IOCP
* @ brief
	1����Net/client�е��ļ����𹤳̣����ɲ���
	2���Ա�server�˵Ĵ���ṹ���������
* @ author zhoumf
* @ date 2016-7-19
************************************************************************/
#pragma  once
//////////////////////////////////////////////////////////////////////////
// ʹ��winsock2  ����ͬwinsock��ͻ
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

	void HandleServerMessage(stMsg* p, DWORD size){ printf("Echo: %s\n", (char*)p); /*������ѭ����Ϣ����*/ }

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
// ʾ��
#ifdef _MY_Test
struct TestMsg : public stMsg
{
	char data[128];
	int size(){ return offsetof(TestMsg, data) + strlen(data) + 1; }
};
void RunClientIOCP()
{
	cout << "������������������������ RunClientIOCP ������������������������" << endl;
	TestMsg msg;
	ClientLinkConfig config;
	ClientLink link(config);

	ClientLink::InitWinsock();

	link.CreateLinkAndConnect();

	while (!link.IsConnect()) Sleep(1000); // �ȴ�ConnectEx����������ɵĻص���֮����ܷ�����

	// ��������һ�����ݣ���ʱ�����������˵�AcceptExDoneIOCallback
	// ���Խ����ʾ���ͻ��˽���connect�����������ݣ����ᴥ��������DoneIO�ص�
	// ��ʵ�������ǻᷢ���ݵģ�����ֻConnect
	// �ͻ���connect���������ֳɹ����ڶԶ˱����롰��������������С�����δ���û����̽ӹܣ���client����Ѿ��ܷ�������
	strcpy_s(msg.data, "���ѣ������������ô��");
	link.SendMsg(msg, msg.size());
	cout << "�����뷢������..." << endl;

	while (true)
	{
		cin >> msg.data;
		link.SendMsg(msg, msg.size());
	}
	link.CloseClient(0);
}
#endif