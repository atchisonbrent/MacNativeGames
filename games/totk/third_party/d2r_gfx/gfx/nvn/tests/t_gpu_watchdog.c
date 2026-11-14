/* The GPU completion watchdog's rule (../gpu_watchdog_rule.h), without the thread or a GPU. */
#include "../gpu_watchdog_rule.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    const uint64_t s = 1000000000ull, limit = 10 * s;
    uint64_t waited = 0;
    /* Nothing outstanding, however old the last completion. */
    assert(!gpu_watchdog_hung(5, 5, 1 * s, 1 * s, 100 * s, limit, &waited));
    /* A completion read after the commit count (completed above committed) is not outstanding work. */
    assert(!gpu_watchdog_hung(5, 6, 1 * s, 1 * s, 100 * s, limit, &waited));
    /* Outstanding, a completion within the limit. */
    assert(!gpu_watchdog_hung(6, 5, 95 * s, 1 * s, 100 * s, limit, &waited));
    assert(!gpu_watchdog_hung(6, 5, 90 * s, 1 * s, 100 * s, limit, &waited));   /* exactly the limit */
    /* Outstanding, nothing completed for longer than the limit: a hang, timed from the last completion. */
    assert(gpu_watchdog_hung(6, 5, 89 * s, 1 * s, 100 * s, limit, &waited) && waited == 11 * s);
    /* The first commit after an idle stretch starts the wait: an old completion does not count against it. */
    assert(!gpu_watchdog_hung(6, 5, 20 * s, 95 * s, 100 * s, limit, &waited));
    assert(gpu_watchdog_hung(6, 5, 20 * s, 80 * s, 100 * s, limit, &waited) && waited == 20 * s);
    /* A commit counted after a long pause at the gate (bg_gate, then cbw_commit_begin): timed from that commit. */
    assert(!gpu_watchdog_hung(1, 0, 0, 3600 * s, 3601 * s, limit, &waited));
    /* A completion or commit that landed after the clock was read: no hang of 18446744074 s. */
    assert(!gpu_watchdog_hung(6, 5, 100 * s + 5000, 1 * s, 100 * s, limit, &waited));
    assert(!gpu_watchdog_hung(6, 5, 1 * s, 100 * s + 5000, 100 * s, limit, &waited));
    /* Before any commit or completion was timed. */
    assert(!gpu_watchdog_hung(1, 0, 0, 0, 100 * s, limit, &waited));
    puts("gpu watchdog rule: 12 checks, 0 failed");
    return 0;
}
