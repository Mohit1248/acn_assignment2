#ifndef SERVER_SCHEDULER_H
#define SERVER_SCHEDULER_H

#include <cstddef>
#include <cstdint>
#include <string>

#include "../common/request.h"

class IScheduler {
public:
    virtual ~IScheduler() = default;

    virtual void enqueue(Request* req) = 0;

    virtual Request* next() = 0;

    virtual void requeue(Request* req) = 0;

    virtual size_t queue_depth() const = 0;

    virtual void shutdown() = 0;
};

enum class Policy { FCFS, SJF, RR, DRR };

Policy parse_policy(const std::string& name);

struct SliceParams {
    Policy policy = Policy::FCFS;
    uint64_t quantum = 0;
    int p_lines = 1;
};

enum class SliceResult { DONE, PREEMPTED, FAILED };

SliceResult serve_slice(Request* req, int fd, const SliceParams& params);

void release_request(Request* req);

IScheduler* make_scheduler(const std::string& policy);

IScheduler* make_scheduler_fcfs();
IScheduler* make_scheduler_sjf();
IScheduler* make_scheduler_rr();
IScheduler* make_scheduler_drr();

#endif
