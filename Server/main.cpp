#include "stdafx.h"

#define _MY_Test
#include "..\src\server\ServLinkMgr.h"
#undef _MY_Test

int _tmain(int argc, _TCHAR* argv[])
{
	RunServerIOCP();

	system("pause");
	return 0;
}