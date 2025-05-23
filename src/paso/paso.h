#pragma once
#include <paso/Task.h>
#include <paso/TaskPool.h>
#include <functional>
#include <type_traits>
#include <concepts>
#include <chrono>

extern "C" uint64_t micros();

namespace paso {

inline void spawm(Task<> &&task) {
    TaskPool::INST->spawm(std::move(task));
}

inline void start_loop() {
    TaskPool::INST->start_loop();
}

struct Micros {
    int64_t tick;

    constexpr Micros(int64_t tick) : tick(tick) {}

    constexpr int64_t micros() const { return tick;}

    constexpr int64_t millis() const { return tick / 1000; }

    constexpr int64_t seconds() const { return tick / 1000000; }
};

struct Millis {
    int64_t tick;

    constexpr Millis(int64_t tick) : tick(tick) {}

    constexpr int64_t micros() const { return tick * 1000; }

    constexpr int64_t millis() const { return tick; }

    constexpr int64_t seconds() const { return tick / 1000; }
};

struct Seconds {
    int64_t tick;

    constexpr Seconds(int64_t tick) : tick(tick) {}

    constexpr int64_t micros() const { return tick * 1000000; }

    constexpr int64_t millis() const { return tick * 1000; }

    constexpr int64_t seconds() const { return tick; }
};

template<typename T>
concept TimeUnit = (std::is_same_v<T, Micros> || std::is_same_v<T, Millis> || std::is_same_v<T, Seconds>);

template<typename T>
struct ToTimeUnitS;

template<TimeUnit T>
struct ToTimeUnitS<T> {
    using Type = T;
};

template<std::integral T>
struct ToTimeUnitS<T> {
    using Type = Millis;
};

template<typename T>
using ToTimeUnit = typename ToTimeUnitS<T>::Type;

template<typename T, TimeUnit U=ToTimeUnit<T>>
inline Async<> sleep_until(T endtime) {
    while (micros() < U(endtime).micros()) {
        co_await std::suspend_always();
    }
}

template<typename R, typename T, TimeUnit U=ToTimeUnit<T>>
inline Async<R> sleep_until_and_return(R value, T endtime) {
    return Trans<R>(sleep_until(endtime), [value] { return value; });
}

template<typename T, TimeUnit U=ToTimeUnit<T>>
inline Async<> sleep(T time) {
    return sleep_until<Micros>(micros() + U(time).micros());
}

template<typename R, typename T, TimeUnit U=ToTimeUnit<T>>
inline Async<R> sleep_and_return(R value, T time) {
    return sleep_until_and_return(value, Micros(micros() + U(time).micros()));
}

inline Async<> wait(std::function<bool()> &&condition) {
    while (!condition()) {
        co_await std::suspend_always();
    }
}

template<typename R>
inline Async<R> wait_and_return(R value, std::function<bool()> &&condition) {
    return Trans<R>(wait(std::move(condition)), [value] { return value; });
}

template<typename T, TimeUnit U=ToTimeUnit<T>>
inline Async<> sleep_until_and_wait(T endtime, std::function<bool()> &&condition) {
    while (micros() < U(endtime).micros() || !condition()) {
        co_await std::suspend_always();
    }
}

template<typename T, TimeUnit U=ToTimeUnit<T>>
inline Async<> sleep_and_wait(T time, std::function<bool()> &&condition) {
    return sleep_until_and_wait(Micros(micros() + U(time).micros()), std::move(condition));
}

template<typename T, TimeUnit U=ToTimeUnit<T>>
inline Async<> sleep_until_then(T endtime, std::function<void()> &&action) {
    return Trans<>(sleep_until(endtime), std::move(action));
}

template<typename T, TimeUnit U=ToTimeUnit<T>>
inline Async<> sleep_then(T time, std::function<void()> &&action) {
    return sleep_until_then(Micros(micros() + U(time).micros()), std::move(action));
}

} // end of namespace paso
