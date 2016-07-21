#include "stdafx.h"
#include "ServLinkMgr.h"
#include "define.h"
#include "ServLink.h"
#include "..\..\tool\thread.h"

#pragma comment (lib,"Ws2_32.lib")

#define CHECK_CYCLE		1000

ServLinkMgr::ServLinkMgr(const ServerConfig& info) : _config(info)
{
	_pThread = NULL;
}
ServLinkMgr::~ServLinkMgr()
{
	if (_pThread) delete _pThread;
}

bool ServLinkMgr::InitWinsock()
{
	WSADATA wsaData = { 0 };
	int nError = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (nError != 0) return false;

	if (LOBYTE(wsaData.wVersion) != 2 ||
		HIBYTE(wsaData.wVersion) != 2)
	{
		WSACleanup();
		return false;
	}
	return true;
}
bool ServLinkMgr::CleanWinsock()
{
	int nError = WSACleanup();
	return nError == 0;
}
bool ServLinkMgr::CreateServer()
{
	int nMaxLink = _config.dwLinks;
	//nMaxLink = (nMaxLink + 4) / 5 * 5; //保持5的倍数
	if (_config.dwPort == 0 || nMaxLink == 0 || nMaxLink >= c_nMaxLink)
	{
		printf("ServerConfig Error！");
		return false;
	}

	_sListener = socket(AF_INET, SOCK_STREAM, 0);
	if (_sListener == INVALID_SOCKET)
	{
		printf("创建socket错误，请检查socket是否被初始化！");
		return false;
	}
	SOCKADDR_IN addr;
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr(_config.strIP.c_str());
	addr.sin_port = htons((short)_config.dwPort);

	if (0 != ::bind(_sListener, (SOCKADDR *)&addr, sizeof(addr)))
	{
		printf("bind错误！");
		return false;
	}
	if (0 != listen(_sListener, 5)) // 三次握手完成，呼入连接请求队列的最大长度
	{
		printf("listen错误，请检查端口是否已经被使用！");
		return false;
	}
	if (0 == BindIoCompletionCallback((HANDLE)_sListener, DoneIO, 0))
	{
		printf("BindIoCompletionCallback错误，请检查系统资源是否耗尽！");
		return false;
	}

	// 先投递几个AcceptEx，m_nAccept会在sClient投递完毕后更新
	_nMaxLink = nMaxLink;
	_nInvalid = nMaxLink;
	_nAccept = 0;
	_nConnect = 0;
	memset(_arrLink, 0, sizeof(_arrLink));
	for (int i = 0; i < _nMaxLink; ++i)
	{
		_arrLink[i] = new ServLink(this, _config.nSendBuffer); // 创建所有link
		if (_nAccept < _config.nPreCreate)
		{
			if (!_arrLink[i]->CreateLinkAndAccept())  // 【里面会创建客户端socket，并AcceptEx(m_hListener, sClient...)】
			{
				printf("创建link错误，请检查参数是否正确！");
				return false;
			}
		}
	}
	return true;
}
bool ServLinkMgr::Close()
{
	if (_pThread)
	{
		_pThread->EndThread();
		delete _pThread;
		_pThread = NULL;
	}
	printf("CloseListen");
	closesocket(_sListener);

	for (int i = 0; i < _nMaxLink; ++i)
	{
		if (_arrLink[i] && _arrLink[i]->IsSocket())
		{
			_arrLink[i]->CloseLink();
		}
	}
	CleanWinsock();
	Sleep(2000);
	for (int i = 0; i < _nMaxLink; ++i)
		delete _arrLink[i];
	return true;
}


static void ServerThread(LPVOID pParam)
{
	ServLinkMgr* p = (ServLinkMgr*)pParam;
	p->RunThread();
}
bool ServLinkMgr::ThreadStart()
{
	_pThread = new Thread;
	return _pThread->RunThread(ServerThread, this);
}
bool ServLinkMgr::RunThread()
{
	time(&_timeNow);
	DWORD dwInitTime = GetTickCount();
	DWORD dwElaspedTime = 0;
	while (_pThread && _pThread->WaitKillEvent(10) == WAIT_TIMEOUT)
	{
		DWORD tempNow = GetTickCount();
		DWORD tempElasped = tempNow - dwInitTime;
		dwInitTime = tempNow;

		time(&_timeNow);

		for (int i = 0; i < _nMaxLink; ++i)
		{
			if (_arrLink[i]->IsConnected())
			{
				_arrLink[i]->ServerRun_SendIO(); //【brief.7】另辟线程定期发送所有buffer
			}
		}

		// 每格CHECK_CYCLE时间跑一次
		dwElaspedTime += tempElasped;
		if (dwElaspedTime > CHECK_CYCLE)
		{
			dwElaspedTime = 0;
			Maintain(_timeNow); //检查维护serverLink
		}
	}
	return true;
}
void ServLinkMgr::Maintain(time_t timenow)
{
	for (int i = 0; i < _nMaxLink; ++i)
	{
		if (_arrLink[i]->IsSocket())
			_arrLink[i]->Maintain(timenow);
		else if (_nAccept < _config.nPreCreate)
			_arrLink[i]->CreateLinkAndAccept();
	}
}

void ServLinkMgr::BroadcastMsg(stMsg& msg, DWORD msgSize)
{
	for (int i = 0; i < _nMaxLink; ++i)
	{
		if (_arrLink[i]->IsConnected())
			_arrLink[i]->SendMsg(msg, msgSize);
	}
}