#include "scheduler.h"

IScheduler* make_scheduler(const std::string& policy) {
    if (policy == "sjf") return make_scheduler_sjf();
    if (policy == "rr") return make_scheduler_rr();
    if (policy == "drr") return make_scheduler_drr();
    return make_scheduler_fcfs();  // "fcfs"
}
