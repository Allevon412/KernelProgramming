#pragma once
struct Process
{
	ULONG ProcessId;
	std::wstring ImageName;

	bool operator==(const Process& other) const
	{
		return ProcessId == other.ProcessId;
	}
	bool operator!=(const Process& other) const
	{
		return ProcessId != other.ProcessId;
	}
};

template <typename T>
struct Node {
	T data;
	Node* next;

	Node(const T& value) : data(value), next(nullptr) {}

};

template <typename T>
class LinkedList
{
private:
	Node<T>* head;

public:
	LinkedList() : head(nullptr) { Length = 1; }
	int Length;


	void insert(const T& value)
	{
		Node<T>* newNode = new Node<T>(value);
		if (!head)
			head = newNode;
		else {
			Node<T>* current = head;
			while (current->next)
			{
				current = current->next;
			}
			current->next = newNode;
		}
		Length++;
	}


	/*void Display()
	{
		Node<T>* current = head;
		while (current)
		{
			std::count << current->data << " ";
			current = current->next;
		}
		std::count << std:;
	}*/

	~LinkedList()
	{
		while (head)
		{
			Node<T>* temp = head;
			head = head->next;
			delete temp;
		}
	}
	Node<T>* retreiveNode(const T& value)
	{
		//if list is empty return nothing
		if (!head)
		{
			return nullptr;
		}

		//if front of list is equal to the value we want return it.
		if (head->data == value)
		{
			return head;
		}

		//else search for node.
		Node<T>* current = head;
		while (current && current->data != value)
		{

			current = current->next;
			//return found node.
			return current;
		}

		//otherwise node is not found.
		return nullptr;

	}

	void removeNode(const T& value)
	{
		if (!head)
		{
			return;
		}

		if (head->data == value)
		{
			Node<T>* temp = head;
			head = head->next;
			delete temp;
		}

		Node<T>* current = head;
		Node<T>* previous = nullptr;
		while (current && current->data != value)
		{
			previous = current;
			current = current->next;
		}

		if (current && previous)
		{
			previous->next = current->next;
			delete current;

		}
		Length--;
	}

	int getLength()
	{
		return Length;
	}

};