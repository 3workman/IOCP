/***********************************************************************
* @ IOCP�����
* @ brief
	1��Client�����Ͽ����ܱ�֤�յ�socket close��
		���ͻ�������close���ؽ��̣���DoneIO�ص����ҡ�dwNumberOfBytesTransferred = 0��
	2��Clientǿɱ���̣��������
		���ֶ�ɱ���̣�����ϵͳTCPģ��ᴥ���رգ��Զ˿��յ�socket close
		��ֱ�ӹػ����Զ�ʲô����֪���ˣ��뿿������
	3������Ч����֤(��С����ʽ������)
	4������Client����Ƶ��
	5��ĳ������Ϣ�ѻ��������߳�
	6�����粨����������ʱ�����ͻ���ѻ����ڴ���
	7����Ϳ�����ۺ������������TCP_NODELAY����Nagle�㷨��������Լ��������ݷ���
		����Ϣ���۳���������ŵ��ײ�API
		������߳�(��)����(50ms)��������buffer
	8��SYN��������ֹ�������ӱ���Чռ��
	9���м�����Ӳ�������socket close������·����ը�ˡ����߱��������
		�����ڷ�ҵ���߼��쳣��һ��ʱ���ᴥ��TCP��RST���ã���Ӧ��socketҲ�������ˣ�����DoneIO�ص�
		���Ƿ�ͬ���������������غϣ��������ĶϿ�ʱ�޳�Щ(60s)�������쳣������̺ܶ�(5s)
	*�������������ϣ�
* @ author zhoumf
* @ date 2016-7-15
************************************************************************/
#pragma once
//////////////////////////////////////////////////////////////////////////
// ʹ��winsock2  ����ͬwinsock��ͻ
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
//////////////////////////////////////////////////////////////////////////

const int c_nMaxLink = 2/*50000*/;

class ServLink;
class Thread;
struct ServerConfig;
struct stMsg;

class ServLinkMgr{
public:
	ServLinkMgr(const ServerConfig& info);
	~ServLinkMgr();

	const ServerConfig& _config;

	time_t _timeNow;

	static bool InitWinsock();
	static bool CleanWinsock();
	static bool IsValidIP(LPCSTR szIP){ return true; }

	// ��������socket������Ͷ�ݼ���AcceptEx
	bool CreateServer();
	bool Close();

	bool ThreadStart();	//create a thread to run !
	bool RunThread();
	void BroadcastMsg(stMsg& msg, DWORD msgSize);

	// ���sClient(ServerLink)����Accept��������(��������socketʱԤ��Ͷ���˼���Accept)����������
	void Maintain(time_t timenow);

	ServLink* GetLink(DWORD id){ return id < (DWORD)_nMaxLink ? _arrLink[id - 1] : NULL; }
	SOCKET GetListener(){ return _sListener; }

	// ԭ���Բ��������connect��accept��invalid��������ԭ�Ӳ����м��о�̬�ģ��᲻���Bug��
	void LinkOnCreate(int id){
		InterlockedIncrement(&_nAccept);
		InterlockedDecrement(&_nInvalid);
	}
	void LinkOnConnect(int id){
		InterlockedIncrement(&_nConnect);
		InterlockedDecrement(&_nAccept);
	}
	void LinkOnAcceptClose(int id){
		InterlockedIncrement(&_nInvalid);
		InterlockedDecrement(&_nAccept);
	}
	void LinkOnClose(int id){
		InterlockedIncrement(&_nInvalid);
		InterlockedDecrement(&_nConnect);
	}
private:
	int	_nMaxLink;
	LONG _nInvalid;
	LONG _nAccept;     // ���ڵȴ����ӵ�socket����
	LONG _nConnect;    // �����ӵ�socket����

	Thread* _pThread;

	SOCKET	_sListener;
	ServLink* _arrLink[c_nMaxLink];
};

/************************************************************************/
// ʾ��
#ifdef _MY_Test
#include "define.h"
void RunServerIOCP()
{
	cout << "������������������������ RunServerIOCP ������������������������" << endl;
	ServerConfig config;
	config.strIP = "127.0.0.1";
	config.dwPort = 4567;

	ServLinkMgr mgr(config);
	ServLinkMgr::InitWinsock();
	mgr.CreateServer();

	mgr.ThreadStart();
	Sleep(1000 * 600);
}
#endif