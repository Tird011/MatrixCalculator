#pragma once
// Splits rows across threads and records who did what.
// Only calculations should include this file (it pulls in <thread>).
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

#include "vectors/result.h"

namespace ml {

inline unsigned hardwareThreads() { return std::max(1u, std::thread::hardware_concurrency()); }

// Runs fn(first, last) over the row range [0, n), cut into at most `tasks` contiguous chunks,
// one thread per chunk (chunk 0 runs on the calling thread). Chunks never overlap, so fn may
// write its own rows without locks. Returns one Step per chunk.
template <class Fn>
std::vector<Step> runRows(std::size_t n, unsigned tasks, Fn fn) {
    if (n == 0) return {};
    const std::size_t want = std::max(1u, tasks);
    const std::size_t chunk = (n + want - 1) / want;
    const std::size_t count = (n + chunk - 1) / chunk;  // chunks actually needed
    std::vector<Step> steps(count);

    auto runTask = [&](std::size_t t) {
        const std::size_t first = t * chunk;
        const std::size_t last = std::min(n, first + chunk);
        const auto t0 = std::chrono::steady_clock::now();
        fn(first, last);
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();

        Step& s = steps[t];  // each thread writes only its own slot
        s.thread = static_cast<unsigned>(t);
        s.firstRow = first;
        s.lastRow = last;
        s.ms = ms;
        char buf[96];
        std::snprintf(buf, sizeof buf, "thread %llu: rows %llu-%llu (%.3f ms)",
                      static_cast<unsigned long long>(t), static_cast<unsigned long long>(first + 1),
                      static_cast<unsigned long long>(last), ms);
        s.text = buf;
    };

    std::vector<std::thread> pool;
    for (std::size_t t = 1; t < count; ++t) pool.emplace_back(runTask, t);
    runTask(0);
    for (std::thread& th : pool) th.join();
    return steps;
}

}  // namespace ml