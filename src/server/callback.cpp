#include "stdafx.h"
#include "ServLink.h"


/*  注意：在DoneIO的线程不能调用close，否则会死锁，只可以在服务器的maintain线程close
	测试显示：
	·客户端仅仅connect但不发送数据，不会触发DoneIO回调 —— 只是进入“呼入连接请求队列”，Maintain()检查中因MAX_Connect_Seconds超时被shutdown为无效的
	·客户端主动close、关进程，有DoneIO回调，且“dwNumberOfBytesTransferred = 0”

	网上的例子“dwNumberOfBytesTransferred = 0”
	·在测试中也发现，调用WSARecv的时候如果缓冲区太小的话，当有大数据到来时，也会收到dwBytes为0的包
	·此时WSAGetLastError() == WSA_IO_PENDING
	·whireshark抓包看了下，收到为长度为0的数据时候，会出现[TCP Zerowindow]
*/
void CALLBACK DoneIO(DWORD dwErrorCode,
	DWORD dwNumberOfBytesTransferred, //实际操作的字节数【此字节数被拷贝进网卡，网卡慢慢发】
	LPOVERLAPPED lpOverlapped)
{
	if (lpOverlapped == NULL)
	{
		printf("DoneIO Null code:%x - bytes:%d \n", dwErrorCode, dwNumberOfBytesTransferred);
		return;
	}
	My_OVERLAPPED* ov = (My_OVERLAPPED*)lpOverlapped;
	ServLink* client = ov->client;

	if (0 != dwErrorCode && dwErrorCode != ERROR_HANDLE_EOF)
	{
		printf("DoneIO Errcode:%x - id:%d - bytes:%d \n", dwErrorCode, client->GetID(), dwNumberOfBytesTransferred);
		client->Invalid(DoneIO_Error);
		return;
	}
	client->DoneIOCallback(dwNumberOfBytesTransferred, ov->eType);
}