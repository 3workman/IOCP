/***********************************************************************
* @ 内存池
* @ brief
	1、预先申请一大块内存，按固定大小分页，页头地址给外界使用
	2、多涉及operator new、operator delete

* @ Notice
    1、cPool_Index 内存池里的对象，有m_index数据，实为内存索引
    2、被外界保存时，可能对象已经历消亡、复用，那么通过保存的idx找到的就是错误指针了
    3、比如保存NpcID，那个Npc已经死亡，内存恰好被新Npc复用，此时通过NpcID找到的就不知道是啥了
    4、可以在对象里加个自增变量，比如“uint16 autoId;”，同m_index合并成uint32，当唯一id。避免外界直接使用m_index

    void MyClass::_CreateUniqueId() //仅在对象新建时调用，其它地方直接用 m_unique_id
    {
        static uint16 s_auto_id = 0;

        m_unique_id = ((++s_auto_id) << 16) + m_index; //对象数16位就够用，不够的话封个64位的union吧
    }
    MyClass* MyClass::FindGroup(uint32 uniqueId)
    {
        int idx = uniqueId & 0xFFFF;

        if (MyClass* ret = FindByIdx(idx))
            if (ret->m_unique_id == uniqueId)
                return ret;
        return NULL;
    }

* @ author zhoumf
* @ date 2014-11-21
************************************************************************/
#pragma once
#include <queue>
#include "cLock.h"

// 检查一段内存是否越界(头尾设标记)
#define CHECKNU 6893    // 除0外任意值
#define PRECHECK_FIELD(i) int __precheck##i;
#define POSCHECK_FIELD(i) int __poscheck##i;
#define INIT_CHECK(o, i) { (o)->__precheck##i = CHECKNU; (o)->__poscheck##i = CHECKNU; }
#define CHECK(o, i){\
if ((o)->__precheck##i != CHECKNU || (o)->__poscheck##i != CHECKNU){\
	printf("%s:%d, memory access out of range with checknu pre %d,pos %d", \
	__FILE__, __LINE__, (o)->__precheck##i, (o)->__poscheck##i);}\
}

class CPoolPage{//线程安全的
	cMutex            m_csLock;
	const size_t	  m_pageSize;
	const size_t	  m_pageNum;
	std::queue<void*> m_queue;

    bool Double(){ // 可设置Double次数限制
        // 无初始化，外界要operator new或调用new(ptr)
        char* p = (char*)malloc(m_pageSize * m_pageNum); // 溢出风险：m_pageSize * m_pageNum
        if (!p) return false;

        // 这里改写m_queue就不必加锁了，Alloc里已经加过，另外构造函数中不必加
        for (size_t i = 0; i < m_pageNum; ++i)
        {
            m_queue.push(p);
            p += m_pageSize;
        }
        return true;
    }
public:
	CPoolPage(size_t pageSize, size_t pageNum) : m_pageSize(pageSize), m_pageNum(pageNum){
        /*  对象构造的线程安全：
            1、不要在ctor中注册回调
            2、此时也不要把this传给跨线程的对象
            3、注意调用的成员函数也可能干坏事
            4、即便在构造函数的最后一行也不行
        */
		Double();
	}
	void* Alloc(){
		cLock lock(m_csLock);
		if (m_queue.empty() && !Double()) return NULL; // 空STL容器调front()、pop()直接宕机
		void* p = m_queue.front();
		m_queue.pop();
		return p;
	}
	void Dealloc(void* p){
		cLock lock(m_csLock);
		m_queue.push(p);
	}
};

// 潜在Bug：m_index被外界保存，但象已经消亡，该内存块又恰巧被新对象复用……此时逻辑层定位到的指针就错乱了
#define VOID_POOL_ID -1
template <class T>
class PoolIndex{ // 自动编号，便于管理(对象要含有m_index变量，记录其内存id)，【非线程安全】
	T**    m_arrPtr;
	int    m_num;
	std::queue<int> m_queue;
public:
    PoolIndex(int num) : m_num(num){
		m_arrPtr = (T**)malloc(m_num * sizeof(T*));
		if (!m_arrPtr) return;

        //T* pObj = ::new T[m_num]; // 若类没operator new，就用全局的new(确保初始化)
        T* pObj = (T*)malloc(m_num * sizeof(T));
        if (!pObj) return;	         // 若类operator new，此处用new，会多次调用构造函数

        for (int i = 0; i < m_num; ++i) {
            m_arrPtr[i] = pObj++;
            m_queue.push(i);
        }
	}
	bool Double(){
		T** temp = (T**)malloc(m_num * 2 * sizeof(T*));
		if (!temp) return false;

        T* pObj = (T*)malloc(m_num * sizeof(T)); // 开辟新内存块
        if (!pObj) return false;

		memcpy(temp, m_arrPtr, m_num * sizeof(T*));
		free(m_arrPtr);	m_arrPtr = temp;

        for (int i = 0; i < m_num; ++i) {
            m_arrPtr[m_num + i] = pObj++;
            m_queue.push(m_num + i);
        }
		m_num *= 2;
		return true;
	}
	T* Alloc(){
		if (m_queue.empty() && !Double()) return NULL;
		int id = m_queue.front();
		m_queue.pop();
		m_arrPtr[id]->m_index = id; // 分配时设置内存id
		return m_arrPtr[id];
	}
	void Dealloc(T* p){
		m_queue.push(p->m_index);
		p->m_index = VOID_POOL_ID; // 回收后置空内存id
	}
	T* GetByID(int id){
		if (id < 0 || id >= m_num) return NULL;
		return VOID_POOL_ID == m_arrPtr[id]->m_index ? NULL : m_arrPtr[id];
	}
};
#define Pool_Index_Define(T, size) \
        static CPoolIndex<T>& _Pool(){ static CPoolIndex<T> pool(size); return pool; } \
        public: \
	    int m_index; \
	    void* operator new(size_t /*size*/){ return _Pool().Alloc(); }\
	    void* operator new(size_t /*size*/, const char* file, int line){ return _Pool().Alloc(); }\
	    void operator delete(void* p, const char* file, int line){ return _Pool().Dealloc((T*)p); }\
	    void operator delete(void* p, size_t) { return _Pool().Dealloc((T*)p); }\
        static T* FindByID(int id){ return _Pool().GetByID(id); }


template <class T>
class CPoolObj{
	CPoolPage	m_pool;
public:
	CPoolObj(size_t num) : m_pool(sizeof(T), num){}
	T* Alloc(){ return (T*)m_pool.Alloc(); }
	void Dealloc(T* p){ m_pool.Dealloc(p); }
};
#define Pool_Obj_Define(T, size) \
        static CPoolObj<T>& _Pool(){ static CPoolObj<T> pool(size); return pool; } \
        public: \
		void* operator new(size_t /*size*/){ return _Pool().Alloc(); }\
		void* operator new(size_t /*size*/, const char* file, int line){ return _Pool().Alloc(); }\
		void operator delete(void* p, const char* file, int line){ return _Pool().Dealloc((T*)p); }\
		void operator delete(void* p, size_t) { return _Pool().Dealloc((T*)p); }