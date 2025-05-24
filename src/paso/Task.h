#pragma once
#include <stdint.h>
#include <coroutine>
#include <variant>
#include <functional>
#include <source_location>
#include <memory>
#include <type_traits>
#include <concepts>

//#define pasov(FMT, ...) printf(FMT, ##__VA_ARGS__)
#define pasov(...)

namespace paso {

class BaseTask;
class BasePromise;
using Handle = std::coroutine_handle<BasePromise>;

class BasePromise {
public:
    BasePromise() : task(nullptr) {
        pasov("new promise %p\n", this);
    }

    constexpr auto initial_suspend() noexcept { return std::suspend_never(); }

    constexpr auto final_suspend() noexcept { return std::suspend_never(); }

    constexpr void unhandled_exception() {}

    constexpr void rethrow_if_unhandled_exception() {}

    // Task<void> get_return_object() defined in sub-class Promise

    // void return_void() or void return_object(T) defined in sub-class Promise

protected:
    BaseTask *task;

    friend class BaseTask;
};

class BaseTask {
public:
    BaseTask(const BaseTask &b) = delete;

    BaseTask(BaseTask &&b) : state(DONE), handle(nullptr), callback(nullptr), prev(b.prev), next(b.next), func_name(nullptr), line_no(0) {
        std::swap(state, b.state);
        std::swap(handle, b.handle);
        std::swap(callback, b.callback);
        if (is_handle()) handle.promise().task = this;
        if (b.prev == &b) prev = this;
        else b.prev->next = this;
        if (b.next == &b) next = this;
        else b.next->prev = this;
        std::swap(func_name, b.func_name);
        std::swap(line_no, b.line_no);
        pasov("move task %p from %p\n", this, &b);
    }

    ~BaseTask() {
        if (callback) delete callback;
        pasov("destory task %p\n", this);
    }

    bool await_ready() { return is_done(); }

    template <std::convertible_to<BasePromise> PM>
    void await_suspend(std::coroutine_handle<PM> outer) noexcept {
        if (!is_done()) outer.promise().task->append(this);
        pasov("append task %p->%p\n", outer.promise().task, this);
    }

    // T await_resume() defined in sub-class Task

    void append(BaseTask *head) {
        BaseTask *tail = head->prev;
        tail->next = this->next;
        this->next->prev = tail;
        this->next = head;
        head->prev = this;
    }

    void remove() {
        state = DONE;
        prev->next = next;
        next->prev = prev;
        prev = next = this;
    }

    bool is_done() const { return state == DONE; }

    bool is_handle() const { return state == HANDLE; }

    bool is_callback() const { return state == CALLBACK; }

    bool run() {
        for (BaseTask *last = prev; !last->is_done(); ) {
            switch (last->state) {
                default: return false;
                case HANDLE:
                    last->handle.resume();
                    break;
                case CALLBACK:
                    last->callback->call(last);
                    break;
            }
            if (last == prev) return true; // last suspended
            else last = prev; // last completed, continue to run *last
        }
        return false; // all completed
    }

    void set_trace(std::source_location loc) {
        func_name = loc.function_name();
        line_no = loc.line();
    }

    const char* get_func_name() const { return func_name; }

    int get_line_no() const { return line_no; }

protected:
    enum State { DONE, HANDLE, CALLBACK };

    struct Callback {
        virtual ~Callback() {}
        virtual void call(BaseTask *task) = 0;
    };

    State state;
    Handle handle;
    Callback *callback;
    BaseTask *prev, *next;
    const char *func_name;
    int line_no;

    explicit BaseTask() : state(DONE), handle(nullptr), callback(nullptr), prev(this), next(this), func_name(nullptr), line_no(0) {
        pasov("new task %p\n", this);
    }

    explicit BaseTask(Handle handle) : state(HANDLE), handle(handle), callback(nullptr), prev(this), next(this), func_name(nullptr), line_no(0) {
        pasov("new handle task %p\n", this);
    }

    friend class TaskPool;
};

template<typename T=void>
class Task : public BaseTask {
public:
    Task(Task &&b) : BaseTask(std::move(b)), value(std::move(b.value)) {}

    explicit Task() : BaseTask() {}

    explicit Task(Handle handle) : BaseTask(handle) {}

    Task(const Task &b) = delete;

    T value;

    template<typename To>
    class [[nodiscard]] Trans : public Task<To> {
    public:
        Trans(Task<T> &&f, std::function<To(T)> &&conv) : Task<To>() {
            if (f.is_done()) {
                if constexpr (std::is_void_v<To>) conv(f.value);
                else this->value = conv(f.value);
            }
            else {
                Task<T> *from = new Task<T>(std::move(f));
                this->append(from);
                this->callback = new TransCallback(from, std::move(conv));
                this->state = CALLBACK;
            }
        }

        Trans(Task<To> &&b) : Task<To>(std::move(b)) {}

    protected:
        struct TransCallback : public BaseTask::Callback {
            Task<T> *from;
            std::function<To(T)> conv;

            TransCallback(Task<T> *from, std::function<To(T)> &&conv) : from(from), conv(std::move(conv)) {}

            virtual void call(BaseTask *task) override {
                Task<To> *to = static_cast<Task<To>*>(task);
                if constexpr (std::is_void_v<To>) conv(from->value);
                else to->value = conv(from->value);
                to->remove();
                delete from;
            }
        };
    };

    template<typename To>
    Trans<To> trans(std::function<To(T)> &&conv) {
        return Trans<To>(std::move(*this), std::move(conv));
    }
};

template<>
class Task<void> : public BaseTask {
public:
    Task(Task &&b) : BaseTask(std::move(b)) {}

    explicit Task() : BaseTask() {}

    explicit Task(Handle handle) : BaseTask(handle) {}

    Task(const Task &b) = delete;

    template<typename To>
    class [[nodiscard]] Trans : public Task<To> {
    public:
        Trans(Task<> &&f, std::function<To()> &&conv) : Task<To>() {
            if (f.is_done()) {
                if constexpr (std::is_void_v<To>) conv();
                else this->value = conv();
            }
            else {
                Task<> *from = new Task<>(std::move(f));
                this->append(from);
                this->callback = new TransCallback(from, std::move(conv));
                this->state = CALLBACK;
            }
        }

        Trans(Task<> &&b) : Task<>(std::move(b)) {}

    protected:
        struct TransCallback : public BaseTask::Callback {
            Task<> *from;
            std::function<To()> conv;

            TransCallback(Task<> *from, std::function<To()> &&conv) : from(from), conv(std::move(conv)) {}

            virtual void call(BaseTask *task) override {
                Task<To> *to = static_cast<Task<To>*>(task);
                if constexpr (std::is_void_v<To>) conv();
                else to->value = conv();
                task->remove();
                delete from;
            }
        };
    };

    template<typename To>
    Trans<To> trans(std::function<To()> &&conv) {
        return Trans<To>(std::move(*this), std::move(conv));
    }
};

template<typename T=void>
class Promise : public BasePromise {
public:
    Task<T> get_return_object(std::source_location loc=std::source_location::current()) {
        Task<T> task(Handle::from_promise(*this));
        task.set_trace(loc);
        this->task = &task;
        return task;
    }

    void return_value(T v) {
        static_cast<Task<T>*>(task)->value = v;
        task->remove();
    }
};

template<>
class Promise<void> : public BasePromise {
public:
    Task<void> get_return_object(std::source_location loc=std::source_location::current()) {
        Task<void> task(Handle::from_promise(*this));
        task.set_trace(loc);
        this->task = &task;
        return task;
    }

    void return_void() {
        task->remove();
    }
};

class LazyPromise : public Promise<void> {
public:
    constexpr auto initial_suspend() noexcept { return std::suspend_always(); }
};

template<typename T=void>
class [[nodiscard]] Async : public Task<T> {
public:
    using promise_type = Promise<T>;

    Async(Task<T> &&b) : Task<T>(std::move(b)) {}

    T await_resume() { return Task<T>::value; }
};

template<>
class [[nodiscard]] Async<void> : public Task<void> {
public:
    using promise_type = Promise<void>;

    Async(Task<void> &&b) : Task<void>(std::move(b)) {}

    constexpr void await_resume() {}
};

class [[nodiscard]] Lazy : public Task<> {
public:
    using promise_type = LazyPromise;

    Lazy(Task<> &&b) : Task<>(std::move(b)) {}
};

template<typename T=void>
class [[nodiscard]] Sync : public Task<T> {
public:
    Sync(T value) : Task<T>() { Task<T>::value = value; }

    Sync(Task<T> &&b) : Task<T>(std::move(b)) {}

    T await_resume() { return Task<T>::value; }
};

template<>
class [[nodiscard]] Sync<void> : public Task<void> {
public:
    Sync() : Task<void>() {}

    Sync(Task<void> &&b) : Task<void>(std::move(b)) {}

    void await_resume() {}
};

template<typename T>
inline Sync<T> sync(T value) { return {value}; }

inline Sync<> sync() { return {}; }

} // namespace paso
