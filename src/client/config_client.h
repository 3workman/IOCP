#pragma once

struct stMsg{
};

struct ClientLinkConfig
{
	std::string strIP = "127.0.0.1";
	WORD  wServerPort = 4567;
	DWORD dwAssistLoopMs = 10;	//ÿ������ʱ�䷢��һ��������Ϣ��
	DWORD nMaxPackageSend = 1024 * 20;
};