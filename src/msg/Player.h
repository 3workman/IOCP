#pragma once
#include "..\server\define.h"

#define Msg_Declare(e) void HandleMsg_##e(stMsg&);
#define Msg_Realize(e) void Player::HandleMsg_##e(stMsg& msg)

class Player {
public:
#include "PlayerMsg.h"
};
