/* late_learn.h: which waited-for render targets are learned, and when. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../late_learn.h"

static late_cand c[LATE_CANDS];
static void reset(void) { memset(c, 0, sizeof c); }
static int wait_for(uint64_t serial, uint64_t frame) { return late_cand_wait(c, serial, frame, LATE_LEARN_RUN); }
static int find(uint64_t serial) { for (int i = 0; i < LATE_CANDS; i++) if (c[i].serial == serial) return i; return -1; }
static int used(void) { int n = 0; for (int i = 0; i < LATE_CANDS; i++) n += c[i].serial != 0; return n; }

int main(void) {
    int i;
    /* a target waited for every frame is due at its LATE_LEARN_RUN-th frame */
    reset();
    for (unsigned f = 0; f < LATE_LEARN_RUN - 1; f++) assert(wait_for(7, 100 + f) < 0);
    i = wait_for(7, 100 + LATE_LEARN_RUN - 1);
    assert(i >= 0 && c[i].serial == 7 && c[i].run == LATE_LEARN_RUN && c[i].waits == LATE_LEARN_RUN);

    /* until the caller frees it (no slot yet) every further wait of the run says so again, in the same entry; a run
       broken before a slot came has to be made again */
    assert(wait_for(7, 100 + LATE_LEARN_RUN) == i && wait_for(7, 100 + LATE_LEARN_RUN) == i && wait_for(7, 150 + LATE_LEARN_RUN) == i);
    assert(wait_for(7, 5000) < 0 && find(7) == i && c[i].run == 1);
    c[i] = (late_cand){0};
    assert(wait_for(7, 5001) < 0 && c[find(7)].run == 1 && c[find(7)].waits == 1);

    /* one rendered every tenth frame: the same number of frames with a wait */
    reset();
    for (unsigned k = 0; k < LATE_LEARN_RUN - 1; k++) assert(wait_for(8, 10 * k) < 0);
    assert(wait_for(8, 10 * (LATE_LEARN_RUN - 1)) >= 0);

    /* several waits in one frame are one frame of the run: six faces in each of two frames do not make a cube map due */
    reset();
    for (int k = 0; k < 6; k++) assert(wait_for(9, 50) < 0);
    for (int k = 0; k < 6; k++) assert(wait_for(9, 60) < 0);
    assert(c[find(9)].run == 2 && c[find(9)].waits == 12);

    /* a gap of exactly LATE_LEARN_GAP frames continues the run; one frame more starts it again, and the new run has
       to be a whole one */
    reset();
    assert(wait_for(10, 1000) < 0 && wait_for(10, 1000 + LATE_LEARN_GAP) < 0);
    assert(c[find(10)].run == 2);
    uint64_t f0 = 1001 + 2 * LATE_LEARN_GAP;
    assert(wait_for(10, f0) < 0);
    assert(c[find(10)].run == 1 && c[find(10)].waits == 3);
    for (unsigned k = 1; k < LATE_LEARN_RUN - 1; k++) assert(wait_for(10, f0 + k) < 0);
    assert(wait_for(10, f0 + LATE_LEARN_RUN - 1) >= 0);

    /* a target read once now and then never has a run, and is due by the first rule at LATE_LEARN waits */
    reset();
    for (unsigned k = 0; k < LATE_LEARN - 1; k++) { assert(wait_for(11, 1000ull * k) < 0); assert(c[find(11)].run == 1); }
    i = wait_for(11, 1000ull * (LATE_LEARN - 1));
    assert(i >= 0 && c[i].serial == 11 && c[i].waits == LATE_LEARN && c[i].run == 1);

    /* run_need 0: the first rule alone */
    reset();
    for (unsigned f = 0; f < LATE_LEARN - 1; f++) assert(late_cand_wait(c, 12, f, 0) < 0);
    assert(late_cand_wait(c, 12, LATE_LEARN - 1, 0) >= 0);

    /* the frame counter going back (it does not, in a run of the game) starts the run again and breaks nothing */
    reset();
    assert(wait_for(13, 500) < 0 && wait_for(13, 501) < 0 && wait_for(13, 20) < 0);
    assert(c[find(13)].run == 1 && c[find(13)].frame == 20);
    for (unsigned k = 1; k < LATE_LEARN_RUN - 1; k++) assert(wait_for(13, 20 + k) < 0);
    assert(wait_for(13, 20 + LATE_LEARN_RUN - 1) >= 0);

    /* a table's worth of targets counted side by side do not disturb each other, and a freed entry is used again */
    reset();
    for (unsigned f = 0; f < LATE_LEARN_RUN - 1; f++) for (uint64_t s = 1; s <= LATE_CANDS; s++) assert(wait_for(s, f) < 0);
    for (uint64_t s = 1; s <= LATE_CANDS; s++) { i = wait_for(s, LATE_LEARN_RUN - 1); assert(i >= 0 && c[i].serial == s); c[i] = (late_cand){0}; }
    assert(used() == 0);
    assert(wait_for(77, 9) < 0 && used() == 1);

    /* one more target than the table holds, all waited for in every frame: none replaces another (the last is not
       counted), and when the others have their slots it is counted from then on */
    reset();
    for (unsigned f = 0; f < LATE_LEARN_RUN; f++) for (uint64_t s = 1; s <= LATE_CANDS + 1; s++) {
        i = wait_for(s, f);
        assert((i >= 0) == (f == LATE_LEARN_RUN - 1 && s <= LATE_CANDS));
        if (i >= 0) { assert(c[i].serial == s); c[i] = (late_cand){0}; }
        assert(s <= LATE_CANDS || (find(s) >= 0) == (f == LATE_LEARN_RUN - 1));
    }
    assert(used() == 1 && c[find(LATE_CANDS + 1)].run == 1);

    /* a full table of leftovers (not waited for in LATE_LEARN_GAP frames): newcomers take the places of those with
       the fewest waits, one each, and are learned; a slow target with many waits outlasts them though it was waited
       for longest ago */
    reset();
    for (uint64_t k = 0; k < LATE_LEARN - 1; k++) assert(wait_for(1, 1000 * k) < 0);
    for (uint64_t s = 2; s <= LATE_CANDS; s++) for (uint64_t k = 0; k < (s == 2 ? 2 : s == 3 ? 3 : 5); k++) assert(wait_for(s, 200000 + s + 1000 * k) < 0);
    uint64_t now = 300000;
    for (unsigned f = 0; f < LATE_LEARN_RUN - 1; f++) { assert(wait_for(101, now + f) < 0); assert(wait_for(102, now + f) < 0); }
    assert(find(101) >= 0 && find(102) >= 0 && find(2) < 0 && find(3) < 0 && find(1) >= 0 && find(4) >= 0);
    assert(wait_for(101, now + LATE_LEARN_RUN - 1) >= 0 && wait_for(102, now + LATE_LEARN_RUN - 1) >= 0);
    i = wait_for(1, now + 5000);                 /* its 120th wait, long after */
    assert(i >= 0 && c[i].serial == 1 && c[i].waits == LATE_LEARN);

    /* a target that is due and found no slot keeps its place when a newcomer comes in a later frame before its own
       wait there, with every other entry already waited for in that frame; its wait then says "due" again */
    reset();
    for (unsigned f = 0; f < LATE_LEARN_RUN; f++) i = wait_for(1, 700 + f);
    assert(i >= 0 && c[i].serial == 1);
    for (uint64_t s = 2; s <= LATE_CANDS; s++) assert(wait_for(s, 708) < 0);
    assert(wait_for(500, 708) < 0 && find(500) < 0 && find(1) == i);
    assert(wait_for(1, 708) == i);
    /* long after, a newcomer is counted again */
    assert(wait_for(501, 708 + LATE_LEARN_GAP + 1) < 0 && find(501) >= 0);

    /* a full table of targets all waited for lately, none in this frame: the newcomer takes the place of the one
       waited for longest ago, whatever its waits */
    reset();
    for (uint64_t s = 1; s <= LATE_CANDS; s++) assert(wait_for(s, 10 + s) < 0);
    for (int k = 0; k < 5; k++) assert(wait_for(2, 12) < 0);     /* 2: more waits than any, and the second oldest */
    assert(wait_for(1, 80) < 0);                                  /* 1 is fresh again; 2 is now the oldest */
    assert(wait_for(200, 81) < 0);
    assert(find(2) < 0 && find(1) >= 0 && find(3) >= 0 && find(200) >= 0 && c[find(200)].waits == 1);

    /* leftovers and targets waited for lately side by side: the newcomer takes a leftover's place, the one with the
       fewest waits, not the place of the one waited for longest ago among the others */
    reset();
    for (uint64_t s = 1; s <= LATE_CANDS; s++) assert(wait_for(s, s <= 2 ? 10 : 500 + s) < 0);
    assert(wait_for(1, 10) < 0 && wait_for(1, 10) < 0);
    assert(wait_for(300, 600) < 0);
    assert(find(2) < 0 && find(1) >= 0 && find(3) >= 0 && find(300) >= 0);

    puts("t_late_learn: ok");
    return 0;
}
