#pragma once
#include <coroutine>
#include <functional>

namespace tutorial {

class Awaiter;

class Promise {
public:
    using Handle = std::coroutine_handle<Promise>;

    Awaiter *awaiter;

    Promise() : awaiter(nullptr) {}

    constexpr auto initial_suspend() noexcept { return std::suspend_never(); }

    constexpr auto final_suspend() noexcept { return std::suspend_never(); }

    constexpr void unhandled_exception() {}

    constexpr void rethrow_if_unhandled_exception() {}

    Awaiter get_return_object();

     void return_void();
};

class Awaiter {
public:
    using promise_type = Promise;
    using Handle = Promise::Handle;
    using Call = std::function<void()>;

    enum Type {DONE, HANDLE, CALL} type;
    Handle handle;
    Call call;
    Awaiter *from;
    Awaiter *next;

    Awaiter() : type(DONE), handle(nullptr), from(nullptr), next(nullptr) {}

    Awaiter(Handle handle) : type(HANDLE), handle(handle), from(nullptr), next(nullptr) {}

    Awaiter(Awaiter &&b) : Awaiter(nullptr) {
        std::swap(type, b.type);
        std::swap(handle, b.handle);
        std::swap(next, b.next);
        std::swap(from, b.from);
        std::swap(call, b.call);
        if (type == HANDLE) handle.promise().awaiter = this;
    }

    bool is_done() const { return type == DONE; }

    bool await_ready() { return true; }

    void await_suspend(Promise::Handle outer);

     void await_resume() {}

     void run();
};

inline Awaiter Promise::get_return_object() {
    Awaiter awaiter = Handle::from_promise(*this);
    this->awaiter = &awaiter;
    return awaiter;
}

inline void Promise::return_void() {
    awaiter->type = Awaiter::DONE;
}

inline void Awaiter::await_suspend(Promise::Handle outer) {
    if (!is_done()) outer.promise().awaiter->next = this;
}

inline void Awaiter::run() {
    // find last node
    Awaiter *prev = nullptr, *last = this;
    while (last->next) prev = last, last = last->next;
    switch (last->type) {
        case HANDLE:
            last->handle.resume();
            break;
        case CALL:
            last->call();
            type = DONE;
            delete from;
            break;
        default:
            break;
    }
    if (last->is_done() && prev) prev->next = nullptr;
}

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

} // namespace tutorial
