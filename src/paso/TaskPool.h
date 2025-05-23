#pragma once
#include <list>
#include <paso/Task.h>

namespace paso {

class TaskPool {
public:
    static TaskPool *INST;

    TaskPool();

    ~TaskPool();

    void spawm(Task<> &&task) {
        if (!task.is_done()) tasks.emplace_back(std::move(task));
    }

    void start_loop();

    void print_trace() const;

private:
    std::list<Task<>> tasks;
    const Task<> *current_task;
};

} // end of namespace paso
