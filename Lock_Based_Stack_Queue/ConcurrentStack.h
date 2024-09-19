#pragma once
#include "pch.h"

template<typename T>
class LockStack
{
public:
	LockStack() {};
	~LockStack() {};
	LockStack(const LockStack&) = delete;//복사 생성자 삭제
	LockStack& operator =(const LockStack&) = delete;//복사 대입 연산자 삭제

	void Push(T value)
	{
		lock_guard<mutex>lock(_mutex);
		_stack.push(std::move(value));
		_condVar.notify_one();
	}

	//단일스레드환경일 경우에는 empty기능을 제공하여 스택이 비어있는지 확인해야하지만
	//멀티 스레드환경에서는 empty를 확인하고 pop을 하기전에 다른 스레드가 공유자원을 사용할수도 있기때문에 큰 의미가없어서 pop하나로 통일
	bool TryPop(T& value)
	{
		lock_guard<mutex>lock(_mutex);
		if (_stack.empty())
			return false;

		value = std::move(_stack.top());
		_stack.pop();
		return  true;
	}
	//TryPop의 경우 spinlock방식으로 동작하지만 waitpop의경우 조건의 완료될때까지 스레드가 잠시 lock을 반환하고 sleep상태로들어갑니다.
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


//일반포인터
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
	//1) 새 노드를 만들고
	//2) 새 노드의 next = head
	//3) head = 새 노드
	void Push(const T& value)
	{
		Node* node = new Node(value); 
		node->next = _head;

		while (_head.compare_exchange_weak(node->next, node) == false);
		//node->next에 _head;를 넣고 만약 다른 스레드가 접근하여 먼저_head의 값을 다른 값을 넣어버리면 문제가생긴다.
		//_head = node;
	}
	//1)head 읽기
	//2)head->next 읽기
	//3)head = head->next
	//4)data 추출후 반환 
	//5)추출한 노드는 삭제 
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
		//만약 멀티스레드환경에서 delete oldhead를 이  스레드가 실행하고 다른스레드가 oldhead 데이터를 참조할려는 순간 문제가 발생하게됨
		//c# java의 경우에는 GC가 자동으로해주는데..
		//delete oldHead;

		return true;
	}

	void TryDelete(Node* oldHead)
	{
		//나 외에 누가 있는가?
		if (_popCount == 1)
		{
			//호출자 혼자인 경우

			//다른 스레드들이 없으니 예약된 데이터들도 삭제하자.
			Node* node = _pendingList.exchange(nullptr);
			if (--_popCount == 0)
			{
				//끼어든 애가 없음->삭제 진행
				//이제와서 끼어들어도 ,데이터는 분리해둔 상태
				DeleteNodes(node);
			}
			else if (node) //널 체크
			{
				//누가 끼어들었으니 다시 갖다 놓자
				ChainPendingNodeList(node);
			}
		
			delete oldHead;
		}
		else
		{
			//누가 있네? 그럼 삭제하지말고 삭제 예약만
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

	atomic<uint32> _popCount = 0;//Pop을 실행중인 쓰레드 수
	atomic<Node*> _pendingList; //삭제 되어야 할 노드들(첫번째 노드)
};

//스마트 포인터
// 변수 자체를 atomic도 없이 스마트포인터로 다 할려고하니까 모든 읽고 쓰기를 전부 atomic으로 해주어야한다;;
// 너무 작성이 힘든거같음;;
//main에서 스마트포인터가 락프리로 동작하는지 체크하는 부분 확인 아마 대부분은 false가 뜰것이고 락프리가 아니다 가짜 락프리라는 뜻
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


//락프리 이긴 한데 이게 확실히 락프리로 가시성 을 보장하느냐하면 엄청난 테스트를 해봐야할듯;
//과연 이게 락을 사용한것보다 빠를까? 부분도 생각해야한다.
template<typename T>
class LockFreeStack2 {


	struct CountedNodePtr // 내부적으로 참조 횟수를 카운팅해주는 구조체
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
			//참조권 흭득 (externalCount를 +1 애가 흭득)
			IncreaseHeadCount(oldHead);
			// externalCount >=2 보다는 클테니 삭제x(안전하게 접근가능)
			Node* ptr = oldHead.ptr;

			//데이터없음
			if (ptr == nullptr)
				return shared_ptr<T>();

			//소유권 흭득 (ptr->next 로 head를 바꿔치기 한 스레드가 흭득)
			if (_head.compare_exchange_strong(oldHead,ptr->next))
			{
				shared_ptr<T> res;
				res.swap(ptr->data);

				//external : 1->2(+1)
				//internal : 0

				//나 말고 누가있는가?
				const int32 countIncrease = oldHead.externalCount - 2;

				if (ptr->internalCount.fetch_add(countIncrease) == -countIncrease)
					delete ptr;

				return res;
			}
			else if(ptr->internalCount.fetch_sub(1)==1)
			{
				//참조권은 얻었으나 , 소유권은 실패 
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