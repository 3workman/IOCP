#include "stdafx.h"

#define _MY_Test
#include "..\src\server\ServLinkMgr.h"
#undef _MY_Test
#include "..\src\msg\Player.h"
#include "..\src\msg\MsgPool.h"

int _tmain(int argc, _TCHAR* argv[])
{
	RunServerIOCP();

    //Player player;
    //stMsg msg; msg.msgId = Login;
    //sMsgPool.Insert(&player, &msg, sizeof(msg));
    //sMsgPool.Handle();

	system("pause");
	return 0;
}