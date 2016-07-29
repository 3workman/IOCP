#include "stdafx.h"
#include "ServLink.h"


/*  ע�⣺��DoneIO���̲߳��ܵ���close�������������ֻ�����ڷ�������maintain�߳�close
	������ʾ��
	���ͻ��˽���connect�����������ݣ����ᴥ��DoneIO�ص� ���� ֻ�ǽ��롰��������������С���Maintain()�������MAX_Connect_Seconds��ʱ��shutdownΪ��Ч��
	���ͻ�������close���ؽ��̣���DoneIO�ص����ҡ�dwNumberOfBytesTransferred = 0��

	���ϵ����ӡ�dwNumberOfBytesTransferred = 0��
	���ڲ�����Ҳ���֣�����WSARecv��ʱ�����������̫С�Ļ������д����ݵ���ʱ��Ҳ���յ�dwBytesΪ0�İ�
	����ʱWSAGetLastError() == WSA_IO_PENDING
	��whiresharkץ�������£��յ�Ϊ����Ϊ0������ʱ�򣬻����[TCP Zerowindow]
*/
void CALLBACK DoneIO(DWORD dwErrorCode,
	DWORD dwNumberOfBytesTransferred, //ʵ�ʲ������ֽ��������ֽ�����������������������������
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