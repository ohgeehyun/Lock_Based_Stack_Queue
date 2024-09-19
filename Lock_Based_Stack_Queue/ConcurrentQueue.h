#pragma once
#include "pch.h"

template<typename T>
class LockQueue
{
public:
	LockQueue() {};
	~LockQueue() {};
	LockQueue(const LockQueue&) = delete;//���� ������ ����
	LockQueue& operator =(const LockQueue&) = delete;//���� ���� ������ ����
	//��Ƽ������ ȯ�濡�� �����ϴ� �κ��� ���

	void Push(T value)
	{
		lock_guard<mutex>lock(_mutex);
		_queue.push(std::move(value));
		_condVar.notify_one();
	}

	//���Ͻ�����ȯ���� ��쿡�� empty����� �����Ͽ� ������ ����ִ��� Ȯ���ؾ�������
	//��Ƽ ������ȯ�濡���� empty�� Ȯ���ϰ� pop�� �ϱ����� �ٸ� �����尡 �����ڿ��� ����Ҽ��� �ֱ⶧���� ū �ǹ̰���� pop�ϳ��� ����
	bool TryPop(T& value)
	{
		lock_guard<mutex>lock(_mutex);
		if(_queue.empty())
			return false;

		value = std::move(_queue.front());
		_queue.pop();
		return  true;
	}

	//TryPop�� ��� spinlock������� ���������� waitpop�ǰ�� ������ �Ϸ�ɶ����� �����尡 ��� lock�� ��ȯ�ϰ� sleep���·ε��ϴ�.
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

