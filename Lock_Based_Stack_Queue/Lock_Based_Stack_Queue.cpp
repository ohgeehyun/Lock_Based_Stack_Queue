#include "pch.h"
#include "ConcurrentStack.h"
#include "ConcurrentQueue.h"

LockQueue<int32> q;
LockStack<int32> s;


void Push() {
    while (true)
    {
        int32 value = rand() % 100;
        q.Push(value);

        this_thread::sleep_for(10ms);
    }
}

//spinlock방식으로 queue또는 stack이 비어있다면 false를 줌으로써 계속 반복중
void Pop() {
    while (true) 
    {
        int32 data = 0;
        if(q.TryPop(OUT data))
            cout << data << endl;
    }
}
//TryPop의 경우 spinlock방식으로 동작하지만 waitpop의경우 조건의 완료될때까지 스레드가 잠시 lock을 반환하고 sleep상태로들어갑니다.
void WaitPop() {
    while (true) 
    {
        int32 data = 0;
        q.WaitPop(data);
    }
}

int main()
{
    shared_ptr<int32>ptr;
    bool value = atomic_is_lock_free(&ptr); //스마트 포인터가 락 프리로 동작하고있는지 체크

    thread t1(Push);
    thread t2(Pop);
    thread t3(WaitPop);

    t1.join();
    t2.join();
    t3.join();
}


