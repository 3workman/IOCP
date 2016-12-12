#include "stdafx.h"
#include "MsgPool.h"
#include "..\server\define.h"
#include "Player.h"


MsgPool::MsgPool() : _pool(512, 4096)
{
    memset(_func, 0, sizeof(_func));

#undef Msg_Declare
#define Msg_Declare(e) _func[e] = &Player::HandleMsg_##e;
#include "PlayerMsg.h"

#ifdef _DEBUG
    for (auto& it : _func) assert(it);
#endif
}
void MsgPool::Insert(Player* player, stMsg* msg, DWORD size)
{
    stMsg* pMsg = (stMsg*)_pool.Alloc();
    memcpy(pMsg, msg, size);
    cLock lock(_mutex);
    _queue.push(std::make_pair(player, pMsg));
}
void MsgPool::Handle()
{
    if (_queue.empty()) return;
    auto& data = _queue.front();
    stMsg* pMsg = data.second;
    (data.first->*_func[pMsg->msgId])(*pMsg);
    _queue.pop();
}