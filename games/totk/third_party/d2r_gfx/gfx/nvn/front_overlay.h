#pragma once
/* CPU staging for a frontend's premultiplied BGRA overlay. No Metal dependencies.
 * A slot has one consumer; producers may call front_overlay_set concurrently.
 * Allocation, freeing, logging and GPU uploads must stay outside the stage lock. */
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct { int w, h, x, y; } front_overlay_box;
typedef struct {
    pthread_mutex_t mu;
    _Atomic uint64_t generation;
    unsigned char *pixels;
    front_overlay_box box;
} front_overlay_stage;
#define FRONT_OVERLAY_STAGE_INIT { .mu = PTHREAD_MUTEX_INITIALIZER }

typedef struct {
    uint64_t generation;
    unsigned char *pixels;
    size_t capacity;
    front_overlay_box box;
} front_overlay_slot;
enum { FRONT_OVERLAY_UNCHANGED, FRONT_OVERLAY_ARRIVED, FRONT_OVERLAY_RESIZED, FRONT_OVERLAY_REMOVED };
typedef struct { int kind; front_overlay_box box; } front_overlay_change;

/* Removal takes precedence over other checks. Failure leaves the stage intact. */
static inline int front_overlay_set(front_overlay_stage *s, const void *bgra, int w, int h,
                                    int bytes_per_row, int x, int y, front_overlay_change *change) {
    unsigned char *pixels = NULL;
    *change = (front_overlay_change){0};
    if (!bgra || w <= 0 || h <= 0) w = h = 0;
    else {
        if (w > 2048 || h > 2048 || bytes_per_row < 4 * w) return 0;
        size_t row = (size_t)w * 4;
        pixels = malloc(row * h);
        if (!pixels) return 0;
        for (int i = 0; i < h; i++)
            memcpy(pixels + i * row, (const unsigned char *)bgra + (size_t)i * bytes_per_row, row);
    }
    pthread_mutex_lock(&s->mu);
    front_overlay_box old = s->box;
    unsigned char *old_pixels = s->pixels;
    s->pixels = pixels;
    s->box = (front_overlay_box){w, h, x, y};
    change->kind = !w ? (old.w ? FRONT_OVERLAY_REMOVED : FRONT_OVERLAY_UNCHANGED) :
        !old.w ? FRONT_OVERLAY_ARRIVED : (old.w != w || old.h != h) ? FRONT_OVERLAY_RESIZED : FRONT_OVERLAY_UNCHANGED;
    change->box = w ? s->box : old;
    atomic_fetch_add_explicit(&s->generation, 1, memory_order_release);
    pthread_mutex_unlock(&s->mu);
    free(old_pixels);
    return 1;
}

/* Returns 1 for a new snapshot, 0 for an unchanged slot (including never used),
 * -1 on allocation failure. The caller uploads only after this returns.
 * Growth retries are bounded by the maximum box size; no allocation holds mu. */
static inline int front_overlay_snapshot(front_overlay_stage *s, front_overlay_slot *slot) {
    if (atomic_load_explicit(&s->generation, memory_order_acquire) == slot->generation) return 0;
    for (;;) {
        pthread_mutex_lock(&s->mu);
        size_t bytes = (size_t)s->box.w * s->box.h * 4;
        if (bytes > slot->capacity) {
            pthread_mutex_unlock(&s->mu);
            unsigned char *pixels = malloc(bytes);
            if (!pixels) return -1;
            free(slot->pixels);
            slot->pixels = pixels;
            slot->capacity = bytes;
            continue;
        }
        if (bytes) memcpy(slot->pixels, s->pixels, bytes);
        slot->box = s->box;
        slot->generation = atomic_load_explicit(&s->generation, memory_order_relaxed);
        pthread_mutex_unlock(&s->mu);
        return 1;
    }
}

typedef struct { int64_t x, y; int w, h, sx, sy; } front_overlay_clip;
/* Widen before adding or negating positions, including INT_MIN and INT_MAX. */
static inline int front_overlay_clip_box(front_overlay_box b, int64_t dw, int64_t dh, front_overlay_clip *c) {
    if (b.w <= 0 || b.h <= 0 || dw <= 0 || dh <= 0) return 0;
    int64_t left = b.x > 0 ? b.x : 0, top = b.y > 0 ? b.y : 0;
    int64_t right = (int64_t)b.x + b.w, bottom = (int64_t)b.y + b.h;
    if (right > dw) right = dw;
    if (bottom > dh) bottom = dh;
    if (right <= left || bottom <= top) return 0;
    *c = (front_overlay_clip){left, top, (int)(right - left), (int)(bottom - top),
                             (int)(left - b.x), (int)(top - b.y)};
    return 1;
}
static inline void front_overlay_uv(front_overlay_clip c, int tw, int th, float uv[8]) {
    const float u[8] = {(float)c.sx / tw, (float)c.sy / th, (float)c.w / tw, (float)c.h / th,
        (c.sx + 0.5f) / tw, (c.sy + 0.5f) / th, (c.sx + c.w - 0.5f) / tw, (c.sy + c.h - 0.5f) / th};
    memcpy(uv, u, sizeof u);
}
