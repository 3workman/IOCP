#pragma once


struct ClientLinkConfig
{
	std::string strIP = "127.0.0.1";
	WORD wServerPort = 4567;
	DWORD nFreq = 0;			//ÿ������ʱ�䷢��һ��������Ϣ��
	DWORD nMaxPackageSend = 1024 * 20;
	DWORD nRecvBuffer = 1024 * 8;
	DWORD nSendBuffer = 1024 * 4;
	bool bConnectSync = false;	//ͬ���ķ�ʽ����,Ĭ���Ƿ������ķ�ʽ
};