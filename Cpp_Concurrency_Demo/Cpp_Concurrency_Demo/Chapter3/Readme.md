# 第三章笔记
## 3.1 Mutex保护共享数据
1. 互斥量通常会与保护的数据放在同一个类中，而不是定义成全局变量。这是面向对象设计的准则：将其放在一个类中，就可让他们联系在一起，也可对类的功能进行封装，并进行数据保护。
2. 在确保成员函数不会传出指针或引用的同时，检查成员函数是否通过指针或引用的方式来调用也是很重要的(尤其是这个操作不在你的控制下时)。函数可能没在互斥量保护的区域内，存储着指针或者引用，这样就很危险。更危险的是：将保护数据作为一个运行时参数。

### 3.1.1 实现一个线程安全的Stack
假设有一个 `stack<vector<int>>` ，vector是一个动态容器，当拷贝一个vetcor，标准库会从堆上分配很多内存来完成这次拷贝。当这个系统处在重度负荷，或有严重的资源限制的情况下，这种内存分配就会失败，所以vector的拷贝构造函数可能会抛出一个 `std::bad_alloc` 异常。当vector中存有大量元素时，这种情况发生的可能性更大。当pop()函数返回“弹出值”时(也就是从栈中将这个值移除)，会有一个潜在的问题：这个值被返回到调用函数的时候，栈才被改变；但当拷贝数据的时候，调用函数抛出一个异常会怎么样？ 如果事情真的发生了，要弹出的数据将会丢失；它的确从栈上移出了，但是拷贝失败了！ `std::stack `的设计人员将这个操作分为两部分：先获取顶部元素(`top()`)，然后从栈中移除(`pop()`)。这样，在不能安全的将元素拷贝出去的情况下，栈中的这个数据还依旧存在，没有丢失。当问题是堆空间不足，应用可能会释放一些内存，然后再进行尝试。不幸的是，这样的分割却制造了本想避免或消除的条件竞争。

#### 解决方案：
1. 传入引用
   ```c++
   std::vector<int> result; 
   some_stack.pop(result);
   ```
   * 优点：在大多数情况下，可以通过临时构造的堆中类型实例接收目标值。避免数据在异常时丢失。 
   * 缺点：需要临时构造出一个堆中类型的实例，用于接收目标值。对于一些类型，这样做是不现实的，因为临时构造一个实例，从时间和资源的角度上来看，都是不划算。对于其他的类型，这样也不总能行得通，因为构造函数需要的一些参数，在代码的这个阶段不一定可用。最后，需要可赋值的存储类型，这是一个重大
限制：即使支持移动构造，甚至是拷贝构造(从而允许返回一个值)，很多用户自定义类型可能都不支持赋值操作。
2. 移动、拷贝构造器禁用异常抛出</br>
 &#160;&#160;&#160;&#160;&#160;&#160;&#160;&#160;使用 `std::is_no_throw_copy_constructible `与`std::is_nothrow_move_constructible` 类型特征，让拷贝或移动构造函数不抛出异常，但是这种方式的局限性太强。很多用户定义类型有可抛出异常的拷贝构造函数，没有移动构造函数；或是，都不抛出异常的构造函数(这种改变会随着C++11中左值引用，越来越为大众所用)。
3. 返回指向Pop值的指针</br>
    &#160;&#160;&#160;&#160;&#160;&#160;&#160;&#160;使用 `std::shared_ptr` 是个不错的选择；不仅能避免内存泄露(因为当对象中指针销毁时，对象也会被销毁)，而且标准库能够完全控制内存分配方案，也就不需要`new`和`delete`操作。这种优化是很重要的：因为堆栈中的每个对象，都需要用`new`进行独立的内存分配，相较于非线程安全版本，这个方案的开销相当大。
   * 优点：返回一个指向弹出元素的指针，而不是直接返回值。指针的优势是自由拷贝，并且不会产生异常，
   * 缺点：返回一个指针需要对对象的内存分配进行管理，对于简单数据类型(比如：int)，内存管理的开销要远大于直接返回
值。

    **Demo中采用1+3的方式实现**

``` c++
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

```

## 3.2 死锁
&#160;&#160;&#160;&#160;&#160;&#160;&#160;&#160;死锁的基础知识参考 [死锁基础](https://blog.csdn.net/jyy305/article/details/70077042)
### 3.2.1 锁住所有的互斥量  
&#160;&#160;&#160;&#160;&#160;&#160;&#160;&#160;只要将互斥量作为参数传递给std::lock()，std::lock()就能够锁住多个互斥量。std::lock()并未指定解锁和上锁的顺序，其能够保证：
* std::lock()执行成功时，所有互斥量都已经被上锁，并且没有死锁问题
* std::lock()执行失败时，已被其上锁的互斥量都会被解锁

``` c++
#include <iostream>       // std::cout
#include <thread>         // std::thread
#include <mutex>          // std::mutex, std::lock
 
class some_big_object
{
public:
    some_big_object(int a) :x(a) {}
    void Print(){ std::cout << x << std::endl; }
 private:
    int x;
};
class X
{
private:
  some_big_object& some_detail;
  std::mutex m;
public:
  X(some_big_object & sd):some_detail(sd){}
  friend void swap(X& lhs, X& rhs)
  {
    if(&lhs==&rhs)
      return;
    std::lock(lhs.m,rhs.m);
    std::lock_guard<std::mutex> lock_a(lhs.m,std::adopt_lock); 
    std::lock_guard<std::mutex> lock_b(rhs.m,std::adopt_lock); 
    std::swap(lhs.some_detail,rhs.some_detail);
    } 
};
 
template<class T>
void swap(T& lhs,T& rhs);
 
template<>
void swap<some_big_object>(some_big_object &x, some_big_object &y)
{
    X a(x), b(y);
    swap(a, b);
}
 
int main ()
{
    some_big_object a(1),b(2);
    a.Print(), b.Print();
    swap(a,b);
    a.Print(), b.Print();
    return 0;
}

```
&#160;&#160;&#160;&#160;&#160;&#160;&#160;&#160;`std::lock()`函数锁住两个互斥量，`std::lock_guard`负责unlock两个互斥量，如果不调用`std::lock_guard()`，需要手动`unlock()`。`std::adopt_lock`参数表示互斥量已经上锁，这里仅仅是不会重复上锁。下面两个例子起到相同作用。
``` c++
// example 1
std::mutex mtx;
std::lock(mtx); // have to lock before the next sentence
std::lock_guard<std::mutex> guard(mtx, std::adopt_lock);
 
// example 2
std::mutex mtx;
std::lock(mtx);
mtx.unlock();
```
> 注：互斥对象管理类模板的加锁策略
> | 策略 | tag type | 描述 |
> | ------ | ------ | ------ |
> | (默认) | 无 | 请求锁，阻塞当前线程直到成功获得锁。|
> | std::defer_lock |	std::defer_lock_t |	不请求锁。 |
> | std::try_to_lock | std::try_to_lock_t	| 尝试请求锁，但不阻塞线程，锁不可用时也会立即返回。 |
> | std::adopt_lock |	std::adopt_lock_t |	假定当前线程已经获得互斥对象的所有权，所以不再请求锁。 |



### 3.2.2 避免死锁的进阶指导
1. 避免嵌套锁</br>
   &#160;&#160;&#160;&#160;&#160;&#160;&#160;&#160;首先的建议是，不要重复锁定。如果你已经锁定了就不要再次锁定。如果想同时对多个对象进行锁定操作，那么使用std::lock()函数。
2. 避免在持有锁的时候调用用户提供的代码  
   &#160;&#160;&#160;&#160;&#160;&#160;&#160;&#160;很多时候你无法判断用户提供的代码究竟做了什么，他有可能做任何事，包括请求一个锁操作。如果你已经获取并保持了一个锁状态，此时你调用的用户代码如果再去请求一个锁定操作，那么就违反了第一条准则。如果你不能避免调用用户代码，那么你需要一个新的准则。   
3. 使用固定顺序获取锁  
   &#160;&#160;&#160;&#160;&#160;&#160;&#160;&#160;如果你不得不请求多个锁，而且你无法使用`std::lock()`函数，那么此时你需要做的就是在每个线程中按固定的顺序去请求这些锁。但是有时这种方法也会出问题，例如**双向链表**，一种可能的保护策略是为每个节点配置一个`mutex`，如果要**删除**一个节点，你**必须锁定包括要删除的节点以及其前一个以及后一个节点在内的3个节点**。如果想要**遍历**这个list，那么你需要在**锁定当前节点的同时获取下一个节点**，这样做的目的是为了保证下一个节点没有被修改，当获取到下一个节点后，之前的节点就可以解锁了。这种策略正常工作的前提是，不同的线程必须按同样的顺序锁定节点。如果两个线程按相反的方向遍历list，那么就会产生死锁。或者，如果一个线程试图删除一个节点，另一线程试图遍历节点，仍然会产生死锁。  
   &#160;&#160;&#160;&#160;&#160;&#160;&#160;&#160;一种解决方案就是规定一个遍历的顺序以避免死锁，当然这也付出了相应的代价：不允许反向遍历。类似的情况也会发生在其他数据结构中。
4. 使用自定义的层级锁  
   &#160;&#160;&#160;&#160;&#160;&#160;&#160;&#160;当代码试图对一个互斥量上锁，在该层锁已被低层持有时，上锁是不允许的。  
   &#160;&#160;&#160;&#160;&#160;&#160;&#160;&#160;一个层级锁规定只能按由高到低的顺序执行锁操作。可以为每个层级分配一个序号，并可在运行时检查和记录不同层级锁之间的操作顺序。下面列出一个层级锁的使用程序：
   ``` c++
    hierarchical_mutex high_level_mutex(10000);
    hierarchical_mutex middle_level_mutex(7000); 
    hierarchical_mutex low_level_mutex(5000); 
    
    int do_low_level_stuff();
    int low_level_func()
    {
      std::lock_guard<hierarchical_mutex> lk(low_level_mutex); 
      return do_low_level_stuff();
    }
    
    void high_level_stuff(int some_param);
    void high_level_func()
    {
      std::lock_guard<hierarchical_mutex> lk(high_level_mutex); 
      high_level_stuff(low_level_func()); 
    }
    
    void middle_level_stuff(int some_param);
    void middle_level_func()
    {
      std::lock_guard<hierarchical_mutex> lk(middle_level_mutex); 
      middle_level_stuff(high_level_stuff()); 
    }


    int main()
    {
        high_level_func();
        middle_level_func();
    }

   ```
   &#160;&#160;&#160;&#160;&#160;&#160;&#160;&#160;按照层次锁的原则，`high_level_func()`能够正确执行，而`middle_level_func()`不能正确执行：
   * high_level_func()先获取到高层级的锁，然后获取到低层级的锁，符合原则
   * middle_level_func()先获取低层级的锁，然后获取到高层级的锁，不符合原则  
  &#160;&#160;&#160;&#160;&#160;&#160;&#160;&#160;使用层级锁不太可能会导致死锁，因为在任意时刻你不能保持两个同级的锁，因此，之前所说的在链表中逆序遍历也会自动被避免。实现一个层级锁不是很难的事情。  
   &#160;&#160;&#160;&#160;&#160;&#160;&#160;&#160;为了使用`std::lock_guard<>`，我们需要提供三个函数：`lock()`、`unlock()`、`try_lock()`。下面的程序给出一个简单的实现，其中没有直接使用`try_lock()`函数，try_lock的功能是在非阻塞的情况下试图锁定指定的mutex，如果成功返回true，失败返回false。

   ``` c++
   class hierarchical_mutex
    {
        std::mutex internal_mutex;
        unsigned long const hierarchy_value; //mutex所在的层次
        unsigned long previous_hierarchy_value;//记录前一个mutex的层次，用于解锁时恢复线程的层次

        //thread_local 声明一个变量为线程本地变量,表示当前线程的层级
        //只用于声明全局变量或者代码块中的static变量。
        static thread_local unsigned long this_thread_hierarchy_value;  

        void check_for_hierarchy_violation()//检查当前mutex是否小于线程层次，不是则抛出异常
        {
          if(this_thread_hierarchy_value <= hierarchy_value)
          {
            throw std::logic_error(“mutex hierarchy violated”);
          }
        }

        void update_hierarchy_value()//更新线程的层次
        {
          previous_hierarchy_value=this_thread_hierarchy_value;
          this_thread_hierarchy_value=hierarchy_value;
        }
    public:
        explicit hierarchical_mutex(unsigned long value):
            hierarchy_value(value),
            previous_hierarchy_value(0)
        {}

        void lock() //对mutex加锁
        {
            check_for_hierarchy_violation();
            internal_mutex.lock();
            update_hierarchy_value();
        }

        void unlock()//对mutex解锁
        {
          this_thread_hierarchy_value=previous_hierarchy_value; //用记录的previous_hierarchy_value恢复线程层次
          internal_mutex.unlock();
        }

        bool try_lock()//尝试加锁，若mutex已被其它上锁则返回false
        {
          check_for_hierarchy_violation();
          if(!internal_mutex.try_lock())
            return false;
          update_hierarchy_value();
          return true;
        } 
    };

    //无符号long型的最大值，初始情况下的所有线程都可以对mutex上锁
    //thread_local 保证每个线程都有其拷贝的副本
    thread_local unsigned long hierarchical_mutex::this_thread_hierarchy_value(ULONG_MAX); 

   ```