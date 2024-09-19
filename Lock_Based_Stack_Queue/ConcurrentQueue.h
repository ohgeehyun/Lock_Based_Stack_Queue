#pragma once
#include "pch.h"

template<typename T>
class LockQueue
{
public:
	LockQueue() {};
	~LockQueue() {};
	LockQueue(const LockQueue&) = delete;//복사 생성자 삭제
	LockQueue& operator =(const LockQueue&) = delete;//복사 대입 연산자 삭제
	//멀티스레드 환경에서 복사하는 부분은 취약

	void Push(T value)
	{
		lock_guard<mutex>lock(_mutex);
		_queue.push(std::move(value));
		_condVar.notify_one();
	}

	//단일스레드환경일 경우에는 empty기능을 제공하여 스택이 비어있는지 확인해야하지만
	//멀티 스레드환경에서는 empty를 확인하고 pop을 하기전에 다른 스레드가 공유자원을 사용할수도 있기때문에 큰 의미가없어서 pop하나로 통일
	bool TryPop(T& value)
	{
		lock_guard<mutex>lock(_mutex);
		if(_queue.empty())
			return false;

		value = std::move(_queue.front());
		_queue.pop();
		return  true;
	}

	//TryPop의 경우 spinlock방식으로 동작하지만 waitpop의경우 조건의 완료될때까지 스레드가 잠시 lock을 반환하고 sleep상태로들어갑니다.
	void WaitPop(T& value)
	{
		unique_lock<mutex>lock(_mutex);
		_condVar.wait(lock, [this]() {return _queue.empty() == false; });
		value = std::move(_queue.front());
		_queue.pop();
	}

private:
	queue<T> _queue;
	mutex _mutex;
	condition_variable _condVar;

};

