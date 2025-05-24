#include <stdio.h>
#include <paso/paso.h>

using namespace paso;

Async<> co_empty() {
    printf("empty 0\n");
    co_return;
}

Async<int> co_sync(int i) {
    printf("sync 0.%d\n", i);
    return Sync<int>(i);
}

Async<std::string> co_string(int i) {
    printf("string 0.%d\n", i);
    co_await std::suspend_always();
    printf("string 1.%d\n", i);
    char s[20];
    ltoa(i, s, 10);
    co_return s;
}

Async<int> co_str2int(int i) {
    printf("str2int 0.%d\n", i);
    return co_string(i*10).trans<int>([](std::string s) -> int {
        int i = strtol(s.c_str(), nullptr, 10);
        printf("conv '%s' to %d\n", s.c_str(), i);
        return i;
    });
}

Async<int> co_int(int i) {
    printf("int 0.%d\n", i);
    co_await std::suspend_always();
    co_return i;
}

Lazy co_test_str2int() {
    printf("test_str2int 0\n");
    int i = co_await co_str2int(9);
    printf("test_str2int 1.%d\n", i);
}

Async<> co_trace_lower() {
    printf("trace lower 0\n");
    co_await std::suspend_always();
    printf("trace lower 1\n");
    TaskPool::INST->print_trace();
}

Async<> co_trace_upper() {
    co_await co_trace_lower();
}

Async<> co_call() {
    printf("call 0\n");
    co_await co_empty();
    for (int i = 1; i <= 3; ++i) {
        printf("call 1.%d\n", i);
        co_await co_sync(i);
    }
    for (int i = 1; i <= 3; ++i) {
        printf("call 2.%d\n", i);
        co_await co_int(i);
    }
    printf("call 3\n");
    int i = co_await co_str2int(11);
    printf("call 4.%d\n", i);
    co_await co_trace_upper();
    printf("call 5\n");
}

int main(int argc, char **argv) {
    setbuf(stdout, nullptr);

#if 1
    TaskPool pool;
    spawm(co_call());
    start_loop();
#else
    printf("main 0\n");
    Task<> task = co_call();
    if (!task.is_done()) task.handle.promise().set_trace("co_call", std::source_location::current());
    printf("main 1\n");
    task.run();
    printf("main 2\n");
    task.run();
    printf("main 3\n");
    task.run();
    printf("main 4\n");
    task.run();
    printf("end.\n");
#endif

}
