#ifndef COMMON_CLOCK_H
#define COMMON_CLOCK_H

#include <cstdint>
#include <ctime>

// Single shared source of monotonic timestamps (A18: arrival/start/finish
// must come from CLOCK_MONOTONIC, not the wall clock).
inline uint64_t now_monotonic_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL + static_cast<uint64_t>(ts.tv_nsec);
}

#endif  // COMMON_CLOCK_H
