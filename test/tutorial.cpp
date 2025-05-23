#include <stdio.h>
#include "coro_tutorial.h"

using namespace tutorial;

Awaiter co_task() {
    printf("  task 0\n");
    co_await std::suspend_always();
    printf("  task 1\n");
    co_return;
}

Awaiter co_empty() {
    co_await std::suspend_always();
}

Awaiter co_task_magic() {
	printf("  task 0\n");
	return Transform(co_empty(), [] {
        printf("  task 1\n");
    });
}


int main(int argc, char **argv) {
    setbuf(stdout, nullptr);

    printf("main 0\n");
   	Awaiter awaiter = co_task();
//   	Awaiter awaiter = co_task_magic();
    printf("main 1\n");
    awaiter.run();
    printf("main 2\n");
    awaiter.run();
    printf("end.\n");
    return 0;
}
