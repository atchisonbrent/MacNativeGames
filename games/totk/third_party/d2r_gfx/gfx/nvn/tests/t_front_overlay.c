#include <assert.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static pthread_mutex_t *allocation_mutex;
static _Atomic int allocations;
static int fail_allocation;
static void check_unlocked(void) {
    if (allocation_mutex) {
        assert(pthread_mutex_trylock(allocation_mutex) == 0);
        pthread_mutex_unlock(allocation_mutex);
    }
}
static void *checked_malloc(size_t bytes) {
    check_unlocked();
    allocations++;
    return fail_allocation ? NULL : malloc(bytes);
}
static void checked_free(void *p) { check_unlocked(); free(p); }
#define malloc checked_malloc
#define free checked_free
#include "../front_overlay.h"
#undef malloc
#undef free

static void staging(void) {
    front_overlay_stage s = FRONT_OVERLAY_STAGE_INIT;
    front_overlay_slot slots[4] = {0};
    front_overlay_change change;
    unsigned char pixels[24] = {1,2,3,4,5,6,7,8,99,99,99,99,9,10,11,12,13,14,15,16};
    const unsigned char expected[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    allocation_mutex = &s.mu;
    pthread_mutex_lock(&s.mu);
    assert(front_overlay_snapshot(&s, &slots[0]) == 0); // Never used: no lock or allocation.
    pthread_mutex_unlock(&s.mu);
    assert(allocations == 0 && s.pixels == NULL);
    assert(!front_overlay_set(&s, pixels, INT_MAX, 2, INT_MAX, 0, 0, &change));
    assert(!front_overlay_set(&s, pixels, 2, 2049, 8, 0, 0, &change));
    assert(!front_overlay_set(&s, pixels, 2, 2, 7, 0, 0, &change));
    assert(!front_overlay_set(&s, pixels, 2, 2, INT_MIN, 0, 0, &change));
    assert(allocations == 0 && atomic_load(&s.generation) == 0);
    assert(front_overlay_set(&s, pixels, 2, 2, 12, -3, 7, &change));
    assert(change.kind == FRONT_OVERLAY_ARRIVED);
    memset(pixels, 0, sizeof pixels); // The caller can immediately reuse its memory.
    for (int i = 0; i < 4; i++) {
        assert(front_overlay_snapshot(&s, &slots[i]) == 1);
        assert(slots[i].generation == 1 && slots[i].box.x == -3 && slots[i].box.y == 7);
        assert(!memcmp(slots[i].pixels, expected, sizeof expected));
        pthread_mutex_lock(&s.mu);
        assert(front_overlay_snapshot(&s, &slots[i]) == 0);
        pthread_mutex_unlock(&s.mu);
    }
    assert(front_overlay_set(&s, pixels, 2, 2, 8, 15, -9, &change));
    assert(change.kind == FRONT_OVERLAY_UNCHANGED);
    assert(front_overlay_snapshot(&s, &slots[0]) == 1 && slots[0].generation == 2);
    assert(slots[0].box.x == 15 && slots[1].generation == 1);
    fail_allocation = 1;
    assert(!front_overlay_set(&s, pixels, 1, 1, 4, 0, 0, &change));
    assert(atomic_load(&s.generation) == 2);
    front_overlay_slot empty = {0};
    assert(front_overlay_snapshot(&s, &empty) == -1 && empty.generation == 0);
    fail_allocation = 0;
    assert(front_overlay_set(&s, pixels, 1, 1, 4, 1, 2, &change));
    assert(change.kind == FRONT_OVERLAY_RESIZED);
    assert(front_overlay_set(&s, NULL, INT_MAX, INT_MAX, -1, 0, 0, &change));
    assert(change.kind == FRONT_OVERLAY_REMOVED && change.box.w == 1 && change.box.x == 1);
    for (int i = 0; i < 4; i++) {
        assert(front_overlay_snapshot(&s, &slots[i]) == 1);
        assert(slots[i].box.w == 0 && slots[i].generation == 4);
    }
    assert(front_overlay_set(&s, pixels, 0, INT_MAX, -1, 0, 0, &change));
    assert(change.kind == FRONT_OVERLAY_UNCHANGED);
    assert(front_overlay_set(&s, pixels, 1, INT_MIN, -1, 0, 0, &change));
    unsigned char *large = calloc(2048 * 2048, 4);
    assert(large);
    large[2048 * 2048 * 4 - 1] = 123;
    assert(front_overlay_set(&s, large, 2048, 2048, 8192, INT_MAX, INT_MIN, &change));
    assert(change.kind == FRONT_OVERLAY_ARRIVED);
    assert(front_overlay_snapshot(&s, &slots[0]) == 1);
    assert(slots[0].pixels[2048 * 2048 * 4 - 1] == 123);
    free(large);
    free(s.pixels);
    for (int i = 0; i < 4; i++) free(slots[i].pixels);
    allocation_mutex = NULL;
    pthread_mutex_destroy(&s.mu);
}

static void clipping(void) {
    front_overlay_clip c;
    assert(!front_overlay_clip_box((front_overlay_box){2048,2048,INT_MIN,0}, 100,100,&c));
    assert(!front_overlay_clip_box((front_overlay_box){2048,2048,INT_MAX,0}, 100,100,&c));
    assert(!front_overlay_clip_box((front_overlay_box){1,1,0,INT_MIN}, 100,100,&c));
    assert(!front_overlay_clip_box((front_overlay_box){1,1,0,INT_MAX}, 100,100,&c));
    assert(!front_overlay_clip_box((front_overlay_box){1,1,0,0}, 0,100,&c));
    // Compare clipping with individual pixel visibility on all edges and corners.
    for (int w = 1; w <= 8; w++) for (int h = 1; h <= 8; h++)
    for (int x = -9; x <= 9; x++) for (int y = -9; y <= 9; y++) {
        int count = 0;
        for (int j = 0; j < h; j++) for (int i = 0; i < w; i++)
            count += x+i >= 0 && x+i < 7 && y+j >= 0 && y+j < 5;
        int visible = front_overlay_clip_box((front_overlay_box){w,h,x,y}, 7,5,&c);
        assert(visible == (count > 0));
        if (!visible) continue;
        assert(c.w*c.h == count && c.x >= 0 && c.y >= 0 && c.x+c.w <= 7 && c.y+c.h <= 5);
        assert(c.sx == c.x-x && c.sy == c.y-y && c.sx+c.w <= w && c.sy+c.h <= h);
        float uv[8];
        front_overlay_uv(c, 16,16,uv);
        for (int j = 0; j < c.h; j++) for (int i = 0; i < c.w; i++) {
            // Nearest sampling must select the corresponding source texel, including one-pixel clips.
            float u = uv[0] + ((i+0.5f)/c.w)*uv[2], v = uv[1] + ((j+0.5f)/c.h)*uv[3];
            assert((int)(u*16) == c.sx+i && (int)(v*16) == c.sy+j);
            assert(u >= uv[4] && u <= uv[6] && v >= uv[5] && v <= uv[7]);
        }
    }
}

static front_overlay_stage concurrent = FRONT_OVERLAY_STAGE_INIT;
static _Atomic int writers_done;
static void *writer(void *arg) {
    unsigned char pixels[8*8*4];
    int offset = (int)(intptr_t)arg;
    for (int i = 0; i < 3000; i++) {
        int value = 1 + (i+offset)%250, w = 1+value%8, h = 1+(value/8)%8;
        memset(pixels, value, sizeof pixels);
        front_overlay_change change;
        assert(front_overlay_set(&concurrent, pixels, w,h,w*4,value,-value,&change));
        if (i%13 == 0) assert(front_overlay_set(&concurrent,NULL,0,0,0,0,0,&change));
    }
    atomic_fetch_add(&writers_done, 1);
    return NULL;
}
static void *reader(void *arg) {
    (void)arg;
    front_overlay_slot slot = {0};
    do {
        uint64_t before = slot.generation;
        assert(front_overlay_snapshot(&concurrent, &slot) >= 0);
        assert(slot.generation >= before);
        if (!slot.box.w) continue;
        int value = slot.box.x;
        assert(slot.box.y == -value && slot.box.w == 1+value%8 && slot.box.h == 1+(value/8)%8);
        for (int i = 0; i < slot.box.w*slot.box.h*4; i++) assert(slot.pixels[i] == value);
    } while (atomic_load(&writers_done) != 2);
    free(slot.pixels);
    return NULL;
}
int main(void) {
    staging();
    clipping();
    pthread_t threads[6];
    for (int i = 0; i < 4; i++) assert(!pthread_create(&threads[i],NULL,reader,NULL));
    for (int i = 4; i < 6; i++) assert(!pthread_create(&threads[i],NULL,writer,(void *)(intptr_t)i));
    for (int i = 0; i < 6; i++) assert(!pthread_join(threads[i],NULL));
    free(concurrent.pixels);
    pthread_mutex_destroy(&concurrent.mu);
    puts("frontend overlay: staging, generations, clipping, sampling and concurrency passed");
    return 0;
}
