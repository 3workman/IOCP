#pragma once


struct ClientLinkConfig
{
	std::string strIP = "127.0.0.1";
	WORD wServerPort = 4567;
	DWORD nFreq = 0;			//每隔多少时间发送一次所有消息！
	DWORD nMaxPackageSend = 1024 * 20;
	DWORD nRecvBuffer = 1024 * 8;
	DWORD nSendBuffer = 1024 * 4;
	bool bConnectSync = false;	//同步的方式连接,默认是非阻塞的方式
};