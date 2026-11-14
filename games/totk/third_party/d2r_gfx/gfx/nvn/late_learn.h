#pragma once
/* Which render targets the flush keeps waiting for ("Grass masks" in nvn_mtl.m): the counting, without Metal.
 *
 * A target whose rendering the flush has had to wait for often enough is learned: from then on its rendering lands in
 * memory when its submission completes and the flush no longer waits. Two rules say "often enough":
 *
 *  - LATE_LEARN waits, whenever they happened (the first rule there was);
 *  - a wait in LATE_LEARN_RUN frames, each no more than LATE_LEARN_GAP frames after the one before.
 *
 * The second is for the seconds after a scene loads. Breath of the Wild 1.9.0 in the open world, measured 2026-11-14
 * with the first rule alone: eleven targets need learning, and they are learned one after another, because the flushes
 * of a frame come one by one and the wait for the first leaves the others' renderings finished by the time their
 * flush looks. Nine are rendered every frame (120 frames each); the two 64x64 cube maps are rendered every tenth frame
 * and counted only once the nine were out of the way. The last was learned about 2,400 frames after the world
 * appeared; until then frames took 12 to 16 ms instead of 10.5, and every tenth one 19.6 ms. A new scene gives most of
 * them new serials, and it starts again.
 *
 * A wait counts once a frame towards the run (a cube map's six faces can each be waited for in one frame), and a run
 * is broken by LATE_LEARN_GAP frames without a wait, so a target rendered and read once now and then (a picture the
 * game keeps) never has a run: its reader gets the rendering it waited for, as before.
 *
 * This is evidence that the target is rendered and read over and over, not that its reader can do with the rendering
 * before the last: a target rendered and read in each of nine frames and never again has its ninth read answered
 * with the eighth rendering (with the first rule alone that took 121). None is known in the game.
 *
 * The table. A target that is due but found no slot stays, and each further wait of its run asks again. When the
 * table is full a new target takes the place of one not waited for in the last LATE_LEARN_GAP frames, the one with
 * the fewest waits (so a slow target well on its way by the first rule outlasts the leftovers of a scene); failing
 * that, of the one waited for longest ago. One waited for in this very frame is never replaced, nor one that is due and
 * was waited for in the last LATE_LEARN_GAP frames; if that is all of them the newcomer is not counted this time. (The rule used to be "fewest waits", which is the newcomer before
 * it: two new targets could keep replacing each other.) */
#include <stdint.h>

#define LATE_LEARN 120
#define LATE_LEARN_RUN 8
#define LATE_LEARN_GAP 120
#define LATE_CANDS 64

typedef struct { uint64_t serial, frame; unsigned waits, run; } late_cand;   /* serial 0: free. frame: of its last wait */

/* The flush waited for `serial` in frame `frame`. Returns the target's index in c[LATE_CANDS] when it is due for
 * learning, now or since an earlier wait of this run (the caller frees the entry once the target has its slot:
 * c[i] = (late_cand){0}), -1 otherwise. run_need: LATE_LEARN_RUN, or 0 for the first rule alone. */
static inline int late_cand_wait(late_cand *c, uint64_t serial, uint64_t frame, unsigned run_need) {
    int i, free_i = -1, stale_i = -1, old_i = -1;
    for (i = 0; i < LATE_CANDS; i++) {
        if (c[i].serial == serial) break;
        if (!c[i].serial) { if (free_i < 0) free_i = i; continue; }
        if (c[i].frame >= frame) continue;
        if (frame - c[i].frame <= LATE_LEARN_GAP && (c[i].waits >= LATE_LEARN || (run_need && c[i].run >= run_need))) continue;
        if (frame - c[i].frame > LATE_LEARN_GAP) { if (stale_i < 0 || c[i].waits < c[stale_i].waits) stale_i = i; }
        else if (old_i < 0 || c[i].frame < c[old_i].frame) old_i = i;
    }
    if (i == LATE_CANDS) {
        i = free_i >= 0 ? free_i : stale_i >= 0 ? stale_i : old_i;
        if (i < 0) return -1;
        c[i] = (late_cand){serial, frame, 0, 0};
    }
    if (!c[i].run || frame < c[i].frame || frame - c[i].frame > LATE_LEARN_GAP) c[i].run = 1;
    else if (frame != c[i].frame) c[i].run++;
    c[i].frame = frame;
    return ++c[i].waits >= LATE_LEARN || (run_need && c[i].run >= run_need) ? i : -1;
}
