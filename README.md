# 背景

用上C++20之后，我对里面的协程(coroutine)有点兴趣。初步学习之后参考例程写了一个协程库 `paso`。同时我把手上的所有项目都重构为协程架构。不同的项目采用了协程库也不同：
- PC 命令行及服务类程序用了 `boost::asio`：老牌库功能齐全，从中能学到不少先进经验。
- PC QT 图形化程序用了 `qCoro`：无痛绑定到 Qt 的消息中，够用。
- 嵌入式程序用了我自己写的 `paso`。

嵌入式程序，一般对程序体积比较敏感，用了协程会增加程序空间占用。我一直想着能不能优化一下。最近有空，有打算深入研究一下C++20 coroutine的实现原理，看看能不能以此为切入给程序减减肥。

# 基础

## 如何使用

- 函数中出现 `co_await` 或者 `co_return` ，则这个函数会被编译器转化为协程函数。
- 协程函数的返回值必需实现 `Awaiter` 接口函数。
- 调用协程函数**未必**是协程。因为程序入门的 `main` 函数就不是协程，第一个调用协程函数的必定不是协程。
- `co_await` 调用的**未必**是协程。系统提供的两个类 `std::suspend_always` 和 `std::suspend_never` 就只是一个对象。
- 提这两点是因为都说协程有传染性，C++ coroutine也大致如此。但只要手段高明，传染性可以打破。

用户需要实现 `Promise` 和 `Awaiter` 两个类：
```c++
class Awaiter;

class Promise {
public:
    using Handle = std::coroutine_handle<Promise>;

    auto initial_suspend() { return std::suspend_never(); }

    auto final_suspend() { return std::suspend_never(); }

    void unhandled_exception() {}

    void rethrow_if_unhandled_exception() {}

    Awaiter get_return_object();

    void return_void() {}
};

class Awaiter {
public:
    using promise_type = Promise;

    bool await_ready() { return true; }

    void await_suspend(Promise::Handle outer) {}

    void await_resume() {}
};

Awaiter Promise::get_return_object() {
    return Handle::from_promise(*this);
}
```
其中 `Awaiter` 是外部调用者访问的接口，`Promise` 是协程函数内部访问的接口。

协程函数通过其返回值 `Awaiter::promise_type` 决定 `Promise` 类型，并通过 `Promise::get_return_object()` 构建 `Awaiter`。如果只把 `Awaiter` 做为普通对象返回，`promise_type` 也不是必需的。

协程函数本质上是一个分段的、可多次进入的函数。被调用之后遇到段间中断点会保存当前状态后返回。

通过 `handle.resume()` 再次进入后从上次中断的位置继续执行。
而通过 `co_awaiter` 调用一个协程函数会导致调用者即父协程函数中断返回。需要用户确保被调用者即子协程函数完成之后再调用父协程的 `handle.resume()` 方法继续执行。

这就是C++ coroutine的无栈协程。

## 有栈协程

要把C++ coroutine改成有栈协程需要做一些工作
- 做一个协程池，把所有顶层的协程 `Awaiter` 保存起来。
- 当一个协程函数因为调用子协程中断时，在`Awaiter::await_suspend`方法中将子协程的 `Awaiter` 挂载成为父协程 `Awaiter` 的子节点。
- 执行一个协程 `Awaiter` 的时候采用后序遍历，只有当子节点结束才执行父节点。
- 协程执行结束时会调用 `Promise::return_void` 或 `Promise::return_object` 方法，可以在这里面通知 `Awaiter` 协程结束。
- 顶层协程执行结束后从协程池中删除。
- 轮流执行协程池中的顶层协程。

逻辑上可以认为每次 `co_await` 调用协程函数就是挂起旧协程，启动一个新的协程。新协程是旧协程的子协程。子协程结束后父协程继续执行。

有栈协程池的代码不复杂：
```c++
class TaskPool {
public:
    std::list<Awaiter> tasks;

    void loop() {
        while (!tasks.empty()) {
            for (auto it = tasks.begin(); it != tasks.end(); ) {
                it->run();
                if (it->is_done()) it = tasks.erase(it);
                else ++it;
            }
        }
    }
};

class Awaiter {
public:
    /* ... */
    Handle handle;
    Awaiter *next;

    Awaiter(Handle handle) : handle(handle), next(nullptr) {}

    Awaiter(Awaiter &&b) {
        /* ... */
        handle.promise().awaiter = this;
    }

    void is_done() const { return handle == nullptr; }

    void await_suspend(Handle outer) {
        if (!is_done()) outer.promise().awaiter->next = this;
    }

    void run() {
        // find last node
        Awaiter *prev = nullptr, *last = this;
        while (last->next) prev = last, last = last->next;
        last->handle.resume();
        if (last->is_done() && prev) prev->next = nullptr;
    }
};

class Promise {
public:
    /* ... */
    Awaiter *awaiter;

    Awaiter get_return_object() {
        Awaiter awaiter = Handle::from_promise(*this);
        this->awaiter = &awaiter;
        return awaiter;
    }

    void return_void() {
        awaiter->handle = nullptr;
    }
};
```
`Awaiter` 禁止复制只能移动，并在移动时把对应 `Promise::awaiter` 重新指到自己，所以在`get_return_object()`中可以把`awaiter`指针指到一个局部变量上。

# 实现

为了C++ coroutine的实现原理，我写了一个简单的测试程序，将其编译到arm汇编，再通过AI反汇编为C语言。提示AI不要使用 `co_wait` 和 `co_return` 关键字。

测试代码：
```c++
/* Awaiter, Promise ... */

Awaiter co_task() {
    printf("  task 0\n");
    co_await std::suspend_always();
    printf("  task 1\n");
    co_return;
}

int main() {
    printf("main 0\n");
    Awaiter awaiter = co_task();
    printf("main 1\n");
    awaiter.run();
    printf("end.\n");
}
```
执行结果：
```sh
main 0
  task 0
main 1
  task 1
end.
```
编译后反汇编的C代码，我将其整理成C++类的代码如下：
```c++
class Frame_co_task {
public:
    void actor();
    void destory();

    int state;
    bool finished;
    bool allocated;

    Awaiter::promise_type promise;

    std::suspend_never w0;
    std::suspend_always w2;
    std::suspend_never w4;
}

Awaiter co_task() {
    Frame_co_task *frame = new Frame_co_task();
    frame->state = 0;
    frame->finished = false;
    frame->allocated = true;
    Awaiter awaiter = frame->promise.get_return_object();
    frame->actor();
    return awaiter;
}
```
- `co_task`函数中有`co_await`，被编译器转换成了新的协程函数，里面就是一些格式化的代码。
- `Frame_co_task` 是编译器为`co_task`生成的类，包含：
    - 两个方法
    - 状态机状态
    - 用户通过`Awaiter::promise_type`定义的`Promise`对象
    - 原`co_task`中的部分局部变量。若局部变量生命周期跨多个状态，则被转移到`Frame`类在堆中保存。
- 用户调用 `co_task()` 就是调用转换后的 `co_task()` 函数，而`handle.resume()`则是调用 `frame->actor()` 方法。`co_task()`方法只会调用一次，`frame->actor()`方法可多次调用。

用户的代码都在`Frame_co_task::actor()`里面
```c++
void Frame_co_task::actor() {
    switch (state) {
        default: // error
            return;

        case 0: // initialize
            w0 = promise.initial_suspend();  // std::suspend_never()
            state = 2;
            if (!w0.await_ready()) {
                w0.await_suspend(this);
                return;
            }
            [[ fallthrough ]]
        case 2:
            w0.await_resume();

            printf("  task 0\n");

            w2 = std::suspend_always();
            state = 4;
            if (!w2.await_ready()) {
                w2.await_suspend(this);
                return;
            }
            [[ fallthrough ]]
        case 4:
            w2.await_resume();

            printf("  task 1\n");

            w4 = promise.final_suspend();  // std::suspend_never()
            state = 6;
            if (!w4.await_ready()) {
                w4.await_suspend(this);
                return;
            }
            [[ fallthrough ]]
        case 6:
            w4.await_resume();

            promise.return_void();

            break;

        case 1:
        case 3:
        case 5:
        case 7:
            /* after handle.destory() */
            ......

    }
    if (allocated) delete this;
}
```
- `handle` 是对 `Frame` 指针的封装，`handle.resume()`就是调用 `Frame::actor()`，`handle.destory()` 就是调用 `Frame::destroy()`
- `Promise::initial_suspend()` 和 `Promise::final_suspend()` 可以在函数的入口和出口中断一次。
- 通过在`Promise::initial_suspend`可以达到类似`Java`里面的`@PostConstruct`延迟加载的效果。
- `state`都是偶数，每次执行，`state`切换一次。奇数被用在了`destory()`之后的错误处理上。
- 只有 `Awaiter::await_ready()` 为 `true` 才会调用 `Awaiter::await_suspend(outer)`，用户可以在这里面建立父子协程的关联。

`Frame::destroy()` 的代码很简单：
```
void Frame_co_task::destroy() {
    state |= 1;
    actor();
}
```
- 用户可以调用 `handle.destroy()` 中止协程，这个功能可以用来做多个协程之间`wait_for_all`和`wait_for_one`这样的组合操作。

C++ coroutine 只提供了协程的基础架构，实现很简单，也很符合C++的零成本抽象原则。但是它给用户和库开发者提供了足够的想象空间，我认为是一个优秀的设计。

# 探索

了解了协程的实现就回到我最开始的问题：如何优化协程的空间和时间占用？
我得出的结论是：**只要协程函数每一条路径上少于一次中断，都可以换成非协程的实现方式**。

## 例1：返回同步数据

我在做命令行解析的时候会用到一个统一的函数接口，为了支持协程，这个接口返回的是一个 `Awaiter`。比如：
```c++
Awaiter process(Args &args);
```
但不是所有的命令都需要用到协程，比如：
```c++
Awaiter help(Args &args) {
    printf("help messages.....");
    co_return;
}
```
这个函数会转换成协程，浪费大量的空间和时间。我们只需要让`Awaiter::await_ready()`返回`true`就可以避免这样的浪费：
```c++
class Sync : public Awaiter {
public:
    Sync() : Awaiter(nullptr) {} // Awaiter::handle = nullptr
}

Awaiter help(Args &args) {
    printf("help messages....");
    return Sync();
}
```
`Sync`移动到 `Awaiter` 的时候把 `Awaiter::handle` 设置为 `nullptr`，`Awaiter::await_ready()` 即可返回 `false`。
之后的`Awaiter::await_suspend()`不会执行，`co_await help()`方法调用就和普通函数一样的开销。

## 例2：直接返回其它协程

命令行解析的时候会有一个命令分配，根据不用的命令调用不同的函数：
```c++
Awaiter dispatch(std::string cmd) {
    Args args = cmd_to_args(cmd);
    if (args[0] == "help") co_return co_await help(args);
    if (args[1] == "version") co_return co_await version(args);
    ....
    printf("illegal command\n");
    co_return;
}
```
这里面所有的`co_await`都在程序的出口最后一句。

我们可以把所有的`co_await`去除，所有的`co_return`换成`return`即可避免转换成协程。

这里可以借用函数尾递归的概念可以理解，当一个程序最后调用的协程被中断，恢复时只需要执行子程序的剩余程序。子程序结束即可视为父程序结束。

也可以想象把所有父程序代码都搬到子程序的初始化部分，两者是等价的。

## 例3：调用协程之后还有代码
```c++
Awaiter power_on(Args &args) {
    co_await pwr_ctrl->open();
    printf("power open\n");
    co_return;
}
```
在 `co_await` 子协程结束之后，父函数还需要做一些同步的收尾工作。

在不改变函数接口的前提下，我们可以在子协程`Awaiter`之前插入一个`Awaiter`节点，这个节点的`Awaiter::run()`方法不是执行`handle.resume()`而是我们指定的其它方法：
```c++
class Awaiter {
public:
    /* ... */
    using Call = std::function<void()>;
    enum Type { DONE, HANDLE, CALL } type;
    Handle handle;
    Call call;
    Awaiter *from;
    Awaiter *next;

    Awaiter(Handle handle) : type(HANDLE), handle(handle), from(nullptr), next(nullptr) {}

    Awaiter() : type(DONE), handle(nullptr), from(nullptr), next(nullptr) {

    bool is_done() const { return type == DONE; }

    void run() {
        /* ... */
        switch (type) {
            case HANDLE:
                handle->resume();
                break;
            case CALL:
                call();
                type = DONE;
                delete from;
                break;
        }
    }
};

class Promise {
public:
    /* ... */
    void return_void() {
        awaiter->type = DONE;
    }
};

class Transform : public Awaiter {
public:
    Transform(Awaiter &&from, Call call) : Awaiter() {
        if (from.is_done()) call();
        else {
            type = CALL;
            this->call = call;
            next = this->from = new Awaiter(std::move(from));
        }
    }
};

Awaiter power_on(Args &args) {
    return Transform(pwr_ctrl->open(), [] {
        printf("power open\n");
    });
}
```
- 此时我们相当于实现了一个只有单步的协程函数体，功能肯定不如C++ coroutine，但胜在体积小，速度快。
- 这个函数体只能调用一次，结束后立即设置协程结束。
- 子协程的 `Awaiter` 只是一个临时变量，父协程需要放在堆里，在父协程结束时销毁。
- 我们也可以继续加强这个`Transform`，让它支持多段协程。但这样最终就是把C++ coroutine自己实现一遍，效果还肯定不如它。目前就没有必要再做这个轮子了。

## 例4：协程返回值类型转换
```C++
Awaiter<int> co_int() {
    co_return 100;
}

Awaiter<std::string> co_string() {
    int i = co_await co_int();
    co_return int2str(i);
}
```
这样的需求本质上和例3一样，都是子协程执行完成之后多执行一段程序。需要扩展 `Awaiter` 为模板支持返回值，再把 `Transform` 类增加类型转换功能。最后大概是：
```c++
Awaiter<std::string> co_string() {
    return Transform<int, std::string>(co_int(), [](int i) -> std::string { return int2str(i); });
}
```
代码太多，就不引用了。直接看我的 `paso` 库就好。

# `paso` on github

[https://github.com/panjh/paso.git](https://github.com/panjh/paso.git)

# `paso` on gitee
[https://gitee.com/edisons/paso.git](https://gitee.com/edisons/paso.git)
