/***********************************************************************
* @ ��Ϣ��
* @ brief
    1��ת�������buffer�е����ݣ����棬�Դ����߼�ѭ������
    2��ֱ�Ӵ�������buffer�����ݣ��Ǿ�����IO�߳����߼��ˣ��ܿ��ܳ������ܷ���

* @ Notice
    1��Insert()�ӿڣ����������ã��Ƕ��̵߳ģ�������Ϣ�ر����̰߳�ȫ
    2������Ұָ�룺�����л�����Player*��Ӧ�����߼�Handle()����� delete player

* @ author zhoumf
* @ date 2016-12-12
************************************************************************/
#pragma once
#include "MsgEnum.h"
#include "..\..\tool\Mempool.h"

class Player;
struct stMsg;
class MsgPool {
    typedef void(Player::*HandleMsgFunc)(stMsg&);
    friend class MsgDefine;

    CPoolPage           _pool;
    HandleMsgFunc       _func[MSG_MAX_CNT];
    cMutex              _mutex;
    std::queue< std::pair<Player*, stMsg*> >  _queue; //Notice��Ϊ���⻺��ָ��Ұ������ѭ��HandleMsg֮�󣬴���ǳ��߼�
public:
    static MsgPool& Instance(){ static MsgPool T; return T; }
    MsgPool();

    void Insert(Player* player, stMsg* msg, DWORD size); //Notice���뿼���̰߳�ȫ
    void Handle(); //��ѭ����ÿ֡��һ��
};
#define sMsgPool MsgPool::Instance()
