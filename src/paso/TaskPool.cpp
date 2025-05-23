#include <paso/TaskPool.h>

namespace paso {

TaskPool *TaskPool::INST = nullptr;

TaskPool::TaskPool() {
    INST = this;
}

TaskPool::~TaskPool() {
    INST = nullptr;
}

void TaskPool::start_loop() {
    while (!tasks.empty()) {
        for (auto it = tasks.begin(); it != tasks.end();) {
            current_task = &*it;
            if (!it->run()) it = tasks.erase(it);
            else ++it;
        }
    }
}

void TaskPool::print_trace() const {
    int i = 0;
    for (const BaseTask &task : tasks) {
        printf("corotine-%d:\n", i);
        const BaseTask *curr = &task;
        do {
            printf("    %s:%d\n", curr->get_func_name(), curr->get_line_no());
            curr = curr->next;
        } while (curr != &task);
        ++i;
    }
}

} // end of namespace paso

