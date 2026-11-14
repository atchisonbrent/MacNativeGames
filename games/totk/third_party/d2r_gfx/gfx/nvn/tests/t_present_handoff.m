// GPU/window/game-free test of the entire production backend, as in mk8d's
// t_present_rescue.m. Run each scenario in a new process: link_sync has statics.
#import <Foundation/Foundation.h>
#import <QuartzCore/CAMetalDisplayLink.h>
#import <QuartzCore/CAMetalLayer.h>
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>

@interface SilentDisplayLink : NSObject
@property CAFrameRateRange preferredFrameRateRange;
@property float preferredFrameLatency;
@property (weak) id<CAMetalDisplayLinkDelegate> delegate;
@property BOOL paused;
@property BOOL invalidated;
- (instancetype)initWithMetalLayer:(CAMetalLayer *)layer;
- (void)addToRunLoop:(NSRunLoop *)loop forMode:(NSRunLoopMode)mode;
- (void)invalidate;
@end
@implementation SilentDisplayLink
- (instancetype)initWithMetalLayer:(CAMetalLayer *)layer { (void)layer; return [super init]; }
- (void)addToRunLoop:(NSRunLoop *)loop forMode:(NSRunLoopMode)mode { (void)loop; (void)mode; }
- (void)invalidate { self.invalidated = YES; }
@end

@interface SilentUpdate : NSObject
@property (readonly) id<CAMetalDrawable> drawable;
@property (readonly) CFTimeInterval targetPresentationTimestamp;
@end
@implementation SilentUpdate
- (id<CAMetalDrawable>)drawable { return nil; }
- (CFTimeInterval)targetPresentationTimestamp { return 0; }
@end

// Only link-thread scheduling is virtualized. Execute link_main itself, with a
// scripted run loop, so publication and teardown can be separated deterministically.
static void *link_main(void *);
static int test_create(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
static int test_detach(pthread_t);
static void test_run_loop(void);
static void test_perform(CFRunLoopRef, CFTypeRef, void (^)(void));
static void test_stop(CFRunLoopRef);
static void test_wake(CFRunLoopRef);
static int test_cond_wait(pthread_cond_t *, pthread_mutex_t *);
static int test_cond_timedwait(pthread_cond_t *, pthread_mutex_t *, const struct timespec *);
#define CAMetalDisplayLink SilentDisplayLink
#define CAMetalDisplayLinkUpdate SilentUpdate
#define pthread_create test_create
#define pthread_detach test_detach
#define pthread_cond_wait test_cond_wait
#define pthread_cond_timedwait test_cond_timedwait
#define CFRunLoopRun test_run_loop
#define CFRunLoopPerformBlock test_perform
#define CFRunLoopStop test_stop
#define CFRunLoopWakeUp test_wake
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything" // Existing backend diagnostics, not test diagnostics.
#include "../nvn_mtl.m"
#pragma clang diagnostic pop
#undef CAMetalDisplayLink
#undef CAMetalDisplayLinkUpdate
#undef pthread_create
#undef pthread_detach
#undef pthread_cond_wait
#undef pthread_cond_timedwait
#undef CFRunLoopRun
#undef CFRunLoopPerformBlock
#undef CFRunLoopStop
#undef CFRunLoopWakeUp

static uint64_t test_now = 10000000000ull;
static NSObject *fake_layer;
uint64_t now_ns(void) { return test_now; }
void *front_metal_layer(void) { return (__bridge void *)fake_layer; }
// No texture: present_now takes its normal !src return path, before any GPU work.
// ring 0 is nil; these counters are the only external services reached on that path.
void fps_overlay_note(uint64_t n, double t) { (void)n; (void)t; }
void fps_overlay_note_gen(uint64_t n, double t) { (void)n; (void)t; }
void fps_board_note(uint64_t n, double t) { (void)n; (void)t; }
void fps_board_note_gen(double t) { (void)t; }
int front_hdr_supported(void) { return 0; }
float front_hdr_headroom(void) { return 1; }
_Atomic int g_totk_sr_on;
uint64_t g_vr_cam_blocks[8];
vr_cam_state *g_vr_headlook;
extern int test_fps;

static int checks, failed, starts, stops;
#define CHECK(name, ok) do { checks++; if (!(ok)) { failed++; printf("FAIL: %s\n", name); } } while (0)
#define REQUIRE(name, ok) do { CHECK(name, ok); if (!(ok)) return; } while (0)
static void (^run_script)(void), (^teardown)(void);
static int pending_start;
static int test_create(pthread_t *th, const pthread_attr_t *a, void *(*fn)(void *), void *arg) {
    (void)a; (void)arg;
    if (fn != link_main) { fprintf(stderr, "unexpected production thread creation\n"); abort(); }
    starts++; pending_start++; *th = pthread_self(); return 0;
}
static int test_detach(pthread_t th) { (void)th; return 0; }
static void test_run_loop(void) { if (!run_script) abort(); run_script(); }
static void test_perform(CFRunLoopRef rl, CFTypeRef mode, void (^block)(void)) {
    (void)rl; (void)mode; if (teardown) abort(); teardown = [block copy];
}
static void test_stop(CFRunLoopRef rl) { (void)rl; stops++; }
static void test_wake(CFRunLoopRef rl) { (void)rl; }
static void drain_teardown(void) { if (teardown) { void (^b)(void) = teardown; teardown = nil; b(); } }
static void publish_link(void (^script)(void)) {
    REQUIRE("one pending link start", pending_start == 1);
    pending_start--; run_script = script; link_main(NULL); run_script = nil;
}

// Instrument entry into real pthread waits, without changing their semantics.
static _Thread_local int waiter_thread, presenter_thread;
static _Atomic int waiter_blocked, waiter_done, presenter_blocked;
static void note_wait(void) {
    if (waiter_thread) atomic_store(&waiter_blocked, 1);
    if (presenter_thread) atomic_store(&presenter_blocked, 1);
}
static int test_cond_wait(pthread_cond_t *cv, pthread_mutex_t *mu) {
    note_wait(); return pthread_cond_wait(cv, mu);
}
static int test_cond_timedwait(pthread_cond_t *cv, pthread_mutex_t *mu, const struct timespec *ts) {
    note_wait(); return pthread_cond_timedwait(cv, mu, ts);
}
static int await_flag(_Atomic int *flag, int ms) {
    for (int i = 0; i < ms; i++) { if (atomic_load(flag)) return 1; usleep(1000); }
    return atomic_load(flag);
}
static void *wait_ticket(void *arg) {
    waiter_thread = 1; mtl_present_wait((uintptr_t)arg); atomic_store(&waiter_done, 1); return NULL;
}
static void *run_presenter(void *arg) {
    presenter_thread = 1; return present_main(arg);
}
static uint64_t queue_ticket(void) {
    pthread_mutex_lock(&g_pq_mu);
    uint64_t tk = ++g_pq_in;
    g_pq[(tk - 1) % PQ] = (pq_item){ .interp = -1, .ring = 0 };
    pthread_cond_broadcast(&g_pq_cv); pthread_mutex_unlock(&g_pq_mu); return tk;
}
static void start_link(void) { link_sync(); CHECK("link start is pending", starts == 1 && g_link_started && !g_link); }

static void stale_applied(void) {
    start_link(); mtl_present_link_host(0); link_sync();
    CHECK("wish becomes mode 0 before object appears", g_dm_present_link == 0 && !g_link);
    publish_link(^{
        CHECK("real link_main owns layer", g_link_owns_layer && g_link != nil);
        SilentDisplayLink *old = g_link; link_sync();
        CHECK("stale applied schedules teardown", teardown != nil && !g_link);
        CHECK("ownership stays until teardown", g_link_owns_layer && !old.invalidated);
        drain_teardown();
        CHECK("teardown invalidates and releases layer", old.invalidated && !g_link_owns_layer && stops == 1);
    });
    CHECK("real link_main clears started on exit", !g_link_started);
}
static void delegate_mode_zero(void) {
    g_dm_present_link = 0; g_link_owns_layer = 1; g_link_tick_ns = test_now;
    uint64_t tk = queue_ticket(); pthread_t th;
    pthread_create(&th, NULL, wait_ticket, (void *)(uintptr_t)tk);
    CHECK("waiter enters mtl_present_wait", await_flag(&waiter_blocked, 500));
    CHECK("waiter blocked before callback", !atomic_load(&waiter_done));
    NvMetalDisplayLinkDelegate *delegate = [NvMetalDisplayLinkDelegate new];
    [delegate metalDisplayLink:(id)[SilentDisplayLink new] needsUpdate:(id)[SilentUpdate new]];
    CHECK("mode 0 delegate takes queued ticket", g_pq_read == tk && g_pq_done == tk);
    CHECK("mode 0 waiter returns within 300 ms", await_flag(&waiter_done, 300));
    CHECK("callback completes without stall fallback", !g_stall_drop && !g_rescue_mode && g_present_n == 1);
    // Even a reverted callback cannot leave the test hanging.
    pthread_mutex_lock(&g_pq_mu); atomic_store(&g_present_discard, 1); pthread_cond_broadcast(&g_pq_cv); pthread_mutex_unlock(&g_pq_mu);
    pthread_join(th, NULL);
}
static void rescue_wish(void) {
    start_link();
    publish_link(^{
        link_sync(); test_now += LINK_RESCUE_NS;
        mtl_present_wait(queue_ticket());
        CHECK("500 ms silence enters rescue", g_rescue_mode == 1 && g_dm_present_link == 0 && g_rescue_n == 1);
        mtl_present_link_host(0); link_sync(); drain_teardown();
        CHECK("wish during rescue does not end it", g_rescue_mode == 1 && g_dm_present_link == 0);
    });
    test_now = g_rescue_back_at; link_sync();
    CHECK("rescue expiry applies wish in same call", !g_rescue_mode && g_dm_present_link == 0);
    CHECK("rescue expiry starts no link", starts == 1 && !g_link_started && !pending_start);
    mtl_present_link_host(-1); link_sync();
    CHECK("rescue did not overwrite saved mode with 0", g_dm_present_link == 1);
}
static void wish_holds(void) {
    mtl_present_link_host(0); link_sync();
    CHECK("wish first applies", g_dm_present_link == 0);
    g_dm_present_link = 1; link_sync();
    CHECK("standing wish corrects outside mode write", g_dm_present_link == 0 && starts == 0);
}
static void configured(void) {
    int mode = g_dm_present_link;
    mtl_present_link_host(0); link_sync(); CHECK("configured mode yields to wish", g_dm_present_link == 0);
    mtl_present_link_host(-1); link_sync(); CHECK("wish restores configured mode", g_dm_present_link == mode);
    CHECK("only nonzero configured mode starts a link", starts == (mode != 0));
}
static void power_saving(void) {
    mtl_present_link_host(0); link_sync(); CHECK("wish starts in mode 0", g_dm_present_link == 0);
    test_fps = 30; CHECK("real mtl_fps_target reads 30", mtl_fps_target() == 30); link_sync();
    CHECK("30 fps gives configured mode back", g_dm_present_link == 1 && g_link_mode == 3 && starts == 1);
    if (pending_start == 1) publish_link(^{
        CHECK("link_main configures 30 Hz", g_link.preferredFrameRateRange.preferred == 30);
        link_sync(); test_fps = 60; link_sync();
        CHECK("wish reapplies after 30 fps ends", g_dm_present_link == 0 && teardown != nil);
        drain_teardown(); CHECK("power-saving link releases layer", !g_link_owns_layer);
    });
}
static void fresh_stall_clock(void) {
    g_dm_present_link = 0; link_sync();
    g_link_tick_ns = test_now; g_stall_t0 = test_now; test_now += 60000000000ull;
    g_dm_present_link = 1; start_link();
    CHECK("link start resets both stall clocks", !g_stall_t0 && g_link_tick_ns == test_now);
    publish_link(^{
        test_now += 150000000ull;
        // At the delayed first tick, the waiting frame may be dropped at 100 ms,
        // but 150 ms of silence must not count as the old minute-long stall.
        mtl_present_wait(queue_ticket());
        CHECK("150 ms first tick delay does not rescue", !g_rescue_mode && g_dm_present_link == 1);
        NvMetalDisplayLinkDelegate *d = [NvMetalDisplayLinkDelegate new];
        [d metalDisplayLink:(id)[SilentDisplayLink new] needsUpdate:(id)[SilentUpdate new]];
        CHECK("first tick clears stall and records new time", !g_stall_t0 && g_link_tick_ns == test_now);
        mtl_present_link_host(0); link_sync(); drain_teardown();
    });
}
static void winding_down(void) {
    start_link();
    publish_link(^{
        link_sync(); mtl_present_link_host(0); link_sync();
        REQUIRE("teardown pending while old thread alive", teardown != nil && !g_link && g_link_started);
        mtl_present_link_host(-1); link_sync();
        CHECK("no second thread while old link winds down", starts == 1 && pending_start == 0 && g_link_started);
        mtl_present_link_host(0); link_sync();
        CHECK("consecutive wish 0 -1 0 remains mode 0", g_dm_present_link == 0 && starts == 1);
        pthread_t presenter; pthread_create(&presenter, NULL, run_presenter, NULL);
        uint64_t tk = queue_ticket();
        CHECK("present_main waits for ownership", await_flag(&presenter_blocked, 500));
        pthread_mutex_lock(&g_pq_mu); CHECK("present thread has not stolen ticket", g_pq_read < tk && g_link_owns_layer); pthread_mutex_unlock(&g_pq_mu);
        drain_teardown();
        CHECK("teardown releases ownership before thread exits", !g_link_owns_layer && g_link_started);
        int done = 0;
        for (int i = 0; i < 300 && !done; i++) { pthread_mutex_lock(&g_pq_mu); done = g_pq_done >= tk; pthread_mutex_unlock(&g_pq_mu); if (!done) usleep(1000); }
        CHECK("real present_main consumes ticket within 300 ms", done);
        // present_main has no shutdown API; the isolated scenario process owns it.
    });
    CHECK("old link thread exits without another start", !g_link_started && starts == 1);
}
int main(int argc, char **argv) { @autoreleasepool {
    alarm(5); setbuf(stdout, NULL);
    if (argc != 2) return 2;
    unsetenv("DM_FPS"); unsetenv("DM_PRESENT_HZ"); unsetenv("TOTK_REALTIME");
    fake_layer = [NSObject new]; g_use = USE_OFF;
    const char *mode = getenv("DM_PRESENT_LINK"); g_dm_present_link = mode ? atoi(mode) : 1;
    if (!strcmp(argv[1], "stale")) stale_applied();
    else if (!strcmp(argv[1], "delegate")) delegate_mode_zero();
    else if (!strcmp(argv[1], "rescue")) rescue_wish();
    else if (!strcmp(argv[1], "holds")) wish_holds();
    else if (!strcmp(argv[1], "configured")) configured();
    else if (!strcmp(argv[1], "power")) power_saving();
    else if (!strcmp(argv[1], "clock")) fresh_stall_clock();
    else if (!strcmp(argv[1], "winding")) winding_down();
    else return 2;
    printf("%s (mode %s): %d checks, %d failed\n", argv[1], mode ? mode : "1", checks, failed);
    _exit(failed != 0);
} }
