/* The GPU completion watchdog's rule (nvn_mtl.m gpu_watchdog_main), apart from the thread so that it can be tested
   (tests/t_gpu_watchdog.c). committed / completed: watched command buffers counted at commit and at completion.
   last_done_ns / last_commit_ns: the last completion, and the commit the outstanding work started with (both 0 before
   the first). Hung: work outstanding and neither a completion nor that first commit within limit_ns. A reference later
   than now (a completion or commit that landed after the caller read the clock) is not a hang. */
#ifndef GPU_WATCHDOG_RULE_H
#define GPU_WATCHDOG_RULE_H
#include <stdint.h>
static inline int gpu_watchdog_hung(uint64_t committed, uint64_t completed, uint64_t last_done_ns, uint64_t last_commit_ns,
                                    uint64_t now_ns, uint64_t limit_ns, uint64_t *waited_ns) {
    if (committed <= completed) return 0;
    uint64_t ref = last_done_ns > last_commit_ns ? last_done_ns : last_commit_ns;   // the newer of the two
    if (!ref || now_ns <= ref || now_ns - ref <= limit_ns) return 0;
    if (waited_ns) *waited_ns = now_ns - ref;
    return 1;
}
#endif
