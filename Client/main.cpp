#include "stdafx.h"

#define _MY_Test
#include "..\src\client\ClientLink.h"
#undef _MY_Test

int _tmain(int argc, _TCHAR* argv[])
{
	RunClientIOCP();

	system("pause");
	return 0;
}