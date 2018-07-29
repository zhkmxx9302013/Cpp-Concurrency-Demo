#pragma once
/************************************************************************/
/* 削减接口可以获得最大程度的安全,甚至限制对栈的一些操作。
 * 栈是不能直接赋值的，赋值操作已经删除
 * 没有swap()函数。
 * 栈可以拷贝的，假设栈中的元素可以拷贝。当栈为空时，pop()函数会抛出一个empty_stack异常，所以在empty()函数被调用后，其他部件还能正常工作。
 * 如选项3描述的那样，使用 std::shared_ptr 可以避免内存分配管理的问题，并避免多次使用new和delete操作。
 * 堆栈中的五个操作，现在就剩下三个：push(), pop()和empty()(这里empty()都有些多余)。
 * 简化接口更有利于数据控制，可以保证互斥量将一个操作完全锁住。                                                                     */
/************************************************************************/
#include <exception> 
#include <memory>
#include <mutex>
#include <stack>

struct empty_stack: std::exception 
{
	const char* what() const throw() 
	{
		return "This is a empty stack";
	}
};


template<typename T>
class threadsafe_stack
{
public:
	threadsafe_stack() : data(std::stack<T>()){}

	//仅提供在拷贝构造器中执行拷贝
	threadsafe_stack(const threadsafe_stack& other)
	{
		std::lock_guard<std::mutex> lock(other.m);
		data = other.data;	
	}

	threadsafe_stack& operator=(const threadsafe_stack&) = delete;
	~threadsafe_stack();

	void push(T new_value)
	{
		std::lock_guard<std::mutex> lock(m);
		data.push(new_value);
	}

	// 方法3的pop实现
	std::shared_ptr<T> pop()
	{
		std::lock_guard<std::mutex> lock(m);
		if (data.empty()) throw empty_stack();

		//在出栈前先分配共享动态内存，保留出栈值
		std::shared_ptr<T> const pop_result_ptr(std::make_shared<T>(data.top()));

		//出栈
		data.pop();

		//Return
		return pop_result_ptr;
	}

	//方法1 的pop实现
	void pop(T& value)
	{
		std::lock_guard<std::mutex> lock(m);
		if (data.empty()) throw empty_stack();

		//在出栈前保留出栈值到引用
		value = data.top();
		data.pop();
	}


	bool empty() const
	{
		std::lock_guard<std::mutex> lock(m);
		return data.empty();
	}

private:
	std::stack<T> data;
	mutable std::mutex m;
};

