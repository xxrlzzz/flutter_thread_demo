# thread in flutter

本文来简要分析一下`flutter`中线程的事件执行机制,并适当对其某些逻辑做了简化.

![](https://img2020.cnblogs.com/blog/1603233/202104/1603233-20210430174851219-197670123.png)

根据类图可以看出主要的结构,Thread 负责构建 std::thread,和启动 TaskRunner,MessageLoop 向当前线程暴露接口.(flutter 中有更为常用的 TaskRunners,保存四大线程的 TaskRuπnner).

客户代码使用时通常通过 TaskRunner 暴露的 PostTask 接口,该接口发送一个 closure，并根据期望运行时间参数的不同分为三类

```cpp
// 运行时间指定为现在
virtual void PostTask(const fml::closure& task) override;
// 运行时间为指定时间点
virtual void PostTaskForTime(const fml::closure& task, fml::TimePoint target_time);
// 运行时间指定为现在+delay
virtual void PostDelayedTask(const fml::closure& task, fml::TimeDelta delay);
```

在使用的过程中,并不需要考虑线程的创建,同步,等问题,它们在底层的 MessageLoopImpl 与 MessageLoopTaskQueues 中自动的被完成了.

## Initialization

`Thread`在构造函数创建一个新的线程,并在该线程启动`messageloop`和`taskrunner`

```cpp

// 构造
Thread::Thread(const std::string &name) : joined_(false) {
  fml::AutoResetWaitableEvent latch;
  fml::RefPtr<fml::TaskRunner> runner;
  thread_ = std::make_unique<std::thread>([&latch, &runner, name]() -> void {
    SetCurrentThreadName(name);
    // 在当前线程让 MessageLoop 去创建 MessageLoopImpl
    fml::MessageLoop::EnsureInitializedForCurrentThread();
    auto &loop = MessageLoop::GetCurrent();
    runner = loop.GetTaskRunner();
    latch.Signal();
    loop.Run();
  });
  // 阻塞等待线程完成创建
  latch.Wait();
  // 获取 task_runner_
  task_runner_ = runner;
}

// 析构
Thread::~Thread() { Join(); }
void Thread::Join() {
  if (joined_) {
    return;
  }
  joined_ = true;
  // 向taskrunner 提交 Terminate() 任务, 然后join线程.
  task_runner_->PostTask([]() { MessageLoop::GetCurrent().Terminate(); });
  thread_->join();
}

```

`MessageLoop` 有一个 ThreadLocal 对象, 它的构造函数会创建 `MessageLoopImpl`, 并用它创建`TaskRunner`

```cpp

FML_THREAD_LOCAL ThreadLocalUniquePtr<MessageLoop> tls_message_loop;

MessageLoop::MessageLoop()
    : loop_(MessageLoopImpl::Create()),
      task_runner_(fml::MakeRefCounted<fml::TaskRunner>(loop_)) {
  FML_CHECK(loop_);
  FML_CHECK(task_runner_);
}

```

其余的方法都是转发给 impl\_完成,可以说 MessageLoop 只有两个作用:

1. 提供当前线程的 TaskRunner
2. 转发 impl\_的方法

## Cross platform abstraction

跨平台部分的实现在`MessageLoopImpl`和`MessageLoopTaskQueues`. 一个管理循环一个管理任务  
MessageLoopImpl 是抽象类,不同平台提供不同的实现类,先考虑公共方法和抽象方法的定义.

首先来看 MessageLoopTaskQueues,这是一个单例模式类,底层通过 map 将 TaskQueueId(全局自增计数器)和 TaskQueueEntry 对应,其中 TaskQueueEntry 表示单个线程对应的任务队列(小根堆存储,key 是 targettime),MessageLoop 在转发时只需要提供自己的 Id,就可以对应到自己的 Entry.  
与平台相关实现的通信是通过`Wakeable`接口的`WakeUp`方法,让消息循环在 task 指定的 targettime 醒来

TaskQueues 的使用流程:

1. 生成新的 taskqueue
2. 向 taskqueue 注册 taskobserver
3. 向 taskqueue 注册 task
4. 获取 task 和 observers(**获取 task 时会 weak 对应的 wakeupable**)
5. 销毁 taskqueue

本质上只是对容器的使用,需要注意的点

1. 由于是单例,方法有加锁的逻辑(不加锁的方法有 unlock 后缀)
2. 每执行一个 task 都会执行所有的 observer,因此不能是耗时的操作(flutter 里的使用我只找到了`UIDartState::AddOrRemoveTaskObserver`,用来执行 dartvm 上的任务)
3. 代码里还有 taskqueue 合并的逻辑,应用场景有限,在我的仓库中被删掉了

```cpp

class MessageLoopImpl : public Wakeable,
                        public fml::RefCountedThreadSafe<MessageLoopImpl> {
 public:
  // ------------------
  // :1
  static fml::RefPtr<MessageLoopImpl> Create();

  virtual ~MessageLoopImpl();

  virtual void Run() = 0;

  virtual void Terminate() = 0;

  void DoRun();

  void DoTerminate();
  // ------------------
  // :2
  void PostTask(const fml::closure& task, fml::TimePoint target_time);

  void AddTaskObserver(intptr_t key, const fml::closure& callback);

  void RemoveTaskObserver(intptr_t key);
  // ------------------
  // :3
  virtual TaskQueueId GetTaskQueueId() const;

  void RunExpiredTasksNow();
};

```

这里的方法我分类了 3 种,

1. 生命周期型  
   Create 会根据平台创建不同的 Impl,在构造函数中会调用 taskqueues:1,绑定好 taskqueue  
   DoRun,DoTerminate 是 Run,Terminate 的包装,通过 atomic flag 防止重入和多次 terminate, Run()返回与析构函数会做清理工作 (taskqueues:5)
2. taskqueue 交互型  
   转发给 taskqueue 的方法
3. 是暴露自身属性或能力  
   GetTaskQueueId()向上层暴露了自己的 TaskQueueId  
   RunExpiredTasksNow()供实现类调用,会真正获取 targettime 小于当前时间点的 task 并运行(taskqueues:4),分 single 和 all,single 最多运行一个,all 会把所有超时任务全部运行.

## Implement On Linux

经过上文的分析可知,平台层的实现只需要有`Run(),Terminate(),WakeUp()`. 本质上就是如何高效的监听`WakeUp`请求然后通知`MessageLoopImpl`来执行即可

具体平台考虑 linux 的情况.  
MessageLoopLinux: 采用 epoll+timerfd 实现 messageloop

调用流程见时序图

![](https://img2020.cnblogs.com/blog/1603233/202104/1603233-20210430175103085-1679401103.png)

分析两个 fd 的行为可以加深理解

### epoll_fd

构造函数时通过`epoll_create()`创建 epoll_fd, 通过`epoll_ctl()`将 epoll_fd 监听 timer_fd read()方法的可用性.

Run()循环中`epoll_wait()`等待 timer_fd 被激活,如果没有异常则执行`OnEventFired()`

析构函数中通过`epoll_ctl()`解绑 timer_fd

### timer_fd

`timerfd_create()`创建

`WakeUp()`中设置在指定时间点被激活

`Terminate()`中置否`Run()`的循环变量,并在当前时间点触发一次`WakeUp()`

`OnEventFired()`, read()读取 timer_fd 确保 timer 被激活,然后调用`RunExpiredTasksNow()`通知基类运行 task.

hint: 调用 POSIX 方法时会使用`FML_HANDLE_EINTR`宏,作用是在发生内部错误时自动重试

## Summary

以上便是对 flutter 内部消息循环的简单分析,基于 flutter-engine master 的四月初版本(看提交记录时发现又有一些变化,等我加深完理解再来更新把...)

[剥取无关代码的 message_loop](https://github.com/xxrlzzz/flutter_thread_demo)
