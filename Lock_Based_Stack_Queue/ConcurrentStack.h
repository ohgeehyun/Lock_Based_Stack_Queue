#pragma once
#include "pch.h"

template<typename T>
class LockStack
{
public:
	LockStack() {};
	~LockStack() {};
	LockStack(const LockStack&) = delete;//���� ������ ����
	LockStack& operator =(const LockStack&) = delete;//���� ���� ������ ����

	void Push(T value)
	{
		lock_guard<mutex>lock(_mutex);
		_stack.push(std::move(value));
		_condVar.notify_one();
	}

	//���Ͻ�����ȯ���� ��쿡�� empty����� �����Ͽ� ������ ����ִ��� Ȯ���ؾ�������
	//��Ƽ ������ȯ�濡���� empty�� Ȯ���ϰ� pop�� �ϱ����� �ٸ� �����尡 �����ڿ��� ����Ҽ��� �ֱ⶧���� ū �ǹ̰���� pop�ϳ��� ����
	bool TryPop(T& value)
	{
		lock_guard<mutex>lock(_mutex);
		if (_stack.empty())
			return false;

		value = std::move(_stack.top());
		_stack.pop();
		return  true;
	}
	//TryPop�� ��� spinlock������� ���������� waitpop�ǰ�� ������ �Ϸ�ɶ����� �����尡 ��� lock�� ��ȯ�ϰ� sleep���·ε��ϴ�.
	void WaitPop(T& value)
	{
		unique_lock<mutex>lock(_mutex);
		_condVar.wait(lock, [this](){return _stack.empty() == false; });
		value = std::move(_stack.top());
		_stack.pop();
	}

private:
	stack<T> _stack;
	mutex _mutex;
	condition_variable _condVar;

};


//�Ϲ�������
template<typename T>
class LockFreeStack
{
	struct Node
	{
		Node(const T& value) : data(value), next(nullptr)
		{

		}
		T data;
		Node* next;
	};

public:
	//1) �� ��带 �����
	//2) �� ����� next = head
	//3) head = �� ���
	void Push(const T& value)
	{
		Node* node = new Node(value); 
		node->next = _head;

		while (_head.compare_exchange_weak(node->next, node) == false);
		//node->next�� _head;�� �ְ� ���� �ٸ� �����尡 �����Ͽ� ����_head�� ���� �ٸ� ���� �־������ �����������.
		//_head = node;
	}
	//1)head �б�
	//2)head->next �б�
	//3)head = head->next
	//4)data ������ ��ȯ 
	//5)������ ���� ���� 
	bool TryPop(T& value)
	{
		++_popCount;
		Node* oldHead = _head;
		while (oldHead && _head.compare_exchange_weak(oldHead, oldHead->next)== false);

		if (oldHead == nullptr)
		{
			--_popCount;
			return false;
		}
		value = oldHead->data;
		TryDelete(oldHead);
		//���� ��Ƽ������ȯ�濡�� delete oldhead�� ��  �����尡 �����ϰ� �ٸ������尡 oldhead �����͸� �����ҷ��� ���� ������ �߻��ϰԵ�
		//c# java�� ��쿡�� GC�� �ڵ��������ִµ�..
		//delete oldHead;

		return true;
	}

	void TryDelete(Node* oldHead)
	{
		//�� �ܿ� ���� �ִ°�?
		if (_popCount == 1)
		{
			//ȣ���� ȥ���� ���

			//�ٸ� ��������� ������ ����� �����͵鵵 ��������.
			Node* node = _pendingList.exchange(nullptr);
			if (--_popCount == 0)
			{
				//����� �ְ� ����->���� ����
				//�����ͼ� ����� ,�����ʹ� �и��ص� ����
				DeleteNodes(node);
			}
			else if (node) //�� üũ
			{
				//���� ���������� �ٽ� ���� ����
				ChainPendingNodeList(node);
			}
		
			delete oldHead;
		}
		else
		{
			//���� �ֳ�? �׷� ������������ ���� ���ุ
			chainPendingNode(oldHead);
			--_popCount;
		}
	}

	void ChainPendingNodeList(Node*first,Node*last)
	{
		last->next = _pendingList;
		while (_pendingList.compare_exchange_weak(last->next, first) == false);
	}

	void ChainPendingNodeList(Node* node)
	{
		Node* last = node;
		while (last->next)
			last = last->next;

		ChainPendingNodeList(node, last);
	}
	void chainPendingNode(Node* node)
	{
		ChainPendingNodeList(node, node);
	}

	static void DeleteNodes(Node* node)
	{
		while(node)
		{
			Node* next = node->next;
			delete node;
			node = next;
		}
	}

private:
	atomic<Node*> _head;

	atomic<uint32> _popCount = 0;//Pop�� �������� ������ ��
	atomic<Node*> _pendingList; //���� �Ǿ�� �� ����(ù��° ���)
};

//����Ʈ ������
// ���� ��ü�� atomic�� ���� ����Ʈ�����ͷ� �� �ҷ����ϴϱ� ��� �а� ���⸦ ���� atomic���� ���־���Ѵ�;;
// �ʹ� �ۼ��� ����Ű���;;
//main���� ����Ʈ�����Ͱ� �������� �����ϴ��� üũ�ϴ� �κ� Ȯ�� �Ƹ� ��κ��� false�� ����̰� �������� �ƴϴ� ��¥ ��������� ��
template<typename T>
class LockFreeStack1 {
	struct Node
	{
		Node(const T& value) : data(make_shared<T>(value)), next(nullptr)
		{

		}
		shared_ptr<T> data;
		shared_ptr<Node> next;
	};


public:
	void Push(const T& value)
	{
		shared_ptr<Node> node = make_shared<Node>(value);
		node->next = atomic_load(&_head);

		while (atomic_compare_exchange_weak(&_head, &node->next, node) == false);
	}

	shared_ptr<T> TryPop()
	{
		shared_ptr<Node> oldHead = atomic_load(&_head);
		while (oldHead && atomic_compare_exchange_weak(&_head, &oldHead, oldHead->next) == false); 
		

		if (oldHead == nullptr)
			return shared_ptr<T>();

		return oldHead->data;
	}




private:
	shared_ptr<Node> _head;
};


//������ �̱� �ѵ� �̰� Ȯ���� �������� ���ü� �� �����ϴ����ϸ� ��û�� �׽�Ʈ�� �غ����ҵ�;
//���� �̰� ���� ����Ѱͺ��� ������? �κе� �����ؾ��Ѵ�.
template<typename T>
class LockFreeStack2 {


	struct CountedNodePtr // ���������� ���� Ƚ���� ī�������ִ� ����ü
	{
		int32 externalCount = 0;
		Node* ptr = nullptr;
	};

	struct Node
	{
		Node(const T& value) : data(make_shared<T>(value)), next(nullptr)
		{

		}
		shared_ptr<T> data;
		atomic<int32> internalCount = 0;
		CountedNodePtr next;
	};


public:
	void Push(const T& value)
	{
		CountedNodePtr node;
		node.ptr = new Node(value);
		node.externalCount = 1;
		//[!]
		node.ptr->next = _head;
		while (_head.compare_exchange_weak(node.ptr->next, node) == false);
	}

	shared_ptr<T> TryPop()
	{
		CountedNodePtr oldHead = _head;
		while (true)
		{
			//������ ŉ�� (externalCount�� +1 �ְ� ŉ��)
			IncreaseHeadCount(oldHead);
			// externalCount >=2 ���ٴ� Ŭ�״� ����x(�����ϰ� ���ٰ���)
			Node* ptr = oldHead.ptr;

			//�����;���
			if (ptr == nullptr)
				return shared_ptr<T>();

			//������ ŉ�� (ptr->next �� head�� �ٲ�ġ�� �� �����尡 ŉ��)
			if (_head.compare_exchange_strong(oldHead,ptr->next))
			{
				shared_ptr<T> res;
				res.swap(ptr->data);

				//external : 1->2(+1)
				//internal : 0

				//�� ���� �����ִ°�?
				const int32 countIncrease = oldHead.externalCount - 2;

				if (ptr->internalCount.fetch_add(countIncrease) == -countIncrease)
					delete ptr;

				return res;
			}
			else if(ptr->internalCount.fetch_sub(1)==1)
			{
				//�������� ������� , �������� ���� 
				delete ptr;
			}


		}
	}
private:
	void IncreaseHeadCount(CountedNodePtr& oldCounter) {
		while (true) {
			CountedNodePtr newCounter = oldCounter
			newCounter.externalCount++;

			if(_head.compare_exchange_strong(oldCounter, newCounter))
			{
				oldCounter.externalCount = newCounter.externalCount;
				break;
			}
		}
	}

private:
	atomic<CountedNodePtr> _head;
};