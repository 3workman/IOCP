#pragma once

#define LOCAL_MACHINE_IP "127.0.0.1"
#define DEFAULT_MAC_LIMIT 5
#define MAX_IP 16

struct ServerConfig
{
	std::string strIP = "127.0.0.1";
	WORD  wPort = 4567;
	DWORD nRecvPacketCheckTime = 10;
	DWORD nRecvPacketLimit = 20;
	DWORD dwAssistLoopMs = 10;
	DWORD nMaxPackage = 1024;
	int   nDeadTime = 300;
	DWORD nTimeLoop = 10;		//���̵߳�����£�����ʱ��������е�socket,�����Send_Groupһ��ʹ��
	DWORD nInBuffer = 2048;
	DWORD nPackSize = 512;
	DWORD DecodeWaitTime = 1000;	//connect��ɵ�decode�����ʱ��(�������ʱ�仹û��decode ����ߵ�)  ms��
	DWORD dwMaxLink = 1/*20000*/;
	int   nPreLink = 1;			//Ԥ�ȴ�����Link
	int	  nPreAccept = 1;		//Ԥ��Ͷ�ݵ�AcceptEx
};

enum InvalidMessageEnum{
	Message_NoError,
	Message_InvalidPacket	= 1,
	Message_TypeError		= 2,
	Message_SizeError		= 3,
	Message_NotConnect		= 4,
	Net_InvalidIP			= 5,
	Net_HeartKick			= 6,
	Message_Overflow7		= 7,
	Net_BindIO				= 9,
	Net_ConnectNotSend		= 10,
	Net_IdleTooLong			= 11,
	Net_Dead				= 12,
	Message_AHServerKick	= 13,
	Message_Write			= 14,
	Message_Read			= 15,
	Message_TooHugePacket	= 16,
	DoneIO_Error			= 17,
	Message_TooMuchPacket   = 18,
	Setsockopt_Error		= 19,

	MessagInvalide_Num
};