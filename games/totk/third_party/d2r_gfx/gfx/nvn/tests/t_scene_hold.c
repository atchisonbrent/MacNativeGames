/* scene_hold.h: which frames are held back. No GPU. */
#include <stdio.h>
#include <string.h>
#include "../scene_hold.h"
static int fails;
/* frames: 's' scene, 'b' backdrop, '.' neither. want: 'H' held, '-' shown. */
static void run(const char *name, const char *frames, const char *want, int limit) {
    SceneHold h = {0, 0}; char got[64]; size_t n = strlen(frames);
    for (size_t i = 0; i < n; i++) got[i] = scene_hold_step(&h, frames[i] == 's', frames[i] == 'b', limit) ? 'H' : '-';
    got[n] = 0;
    if (strcmp(got, want)) { fails++; printf("FAIL %s: frames %s limit %d: got %s, want %s\n", name, frames, limit, got, want); }
}
int main(void) {
    run("the weapon wheel", "sss...bbbbsss", "---HHH-------", 8);
    run("at the limit", "ss....b", "--HHHH-", 4);
    run("a cut to black comes late, then shows", "ss..........", "--HHHH------", 4);
    run("black from the start is shown", "....ss", "------", 8);
    run("after the limit nothing more is held until a good frame", "ss.....s..", "--HH---"  "-HH", 2);
    run("closing a menu", "bbb..sss", "---HH---", 8);
    run("a scene and a backdrop in one frame is good", "s.b.s", "-H-H-", 8);
    run("off", "ss...bb", "-------", 0);
    run("a negative limit is off", "ss...", "-----", -3);
    { SceneHold h = {0, 0}; scene_hold_step(&h, 1, 0, 8); scene_hold_step(&h, 0, 0, 8);   /* turned off mid-run: the state is dropped */
      scene_hold_step(&h, 0, 0, 0); if (h.good || h.held || scene_hold_step(&h, 0, 0, 8)) { fails++; printf("FAIL off drops the state\n"); } }
    if (!fails) printf("scene hold: ok\n");
    return fails != 0;
}
