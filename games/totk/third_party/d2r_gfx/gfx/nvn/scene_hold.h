#pragma once
/* A picture the game drew without its scene is not shown for a few frames.
 *
 * Breath of the Wild 1.9.0, opening the weapon wheel (d-pad right; measured 2026-11-14 at about 110 frames a second, the
 * frame step following real time): for three frames the game clears its canvas, draws the HUD and nothing behind it. The
 * 3D scene has stopped, and the menu's own backdrop (the paused scene, a 1280x720 picture drawn as a full-screen quad)
 * starts with the fourth. Shown as drawn, those frames are a black picture with the HUD on it: about 25 ms, a flash on
 * an OLED in a dark room, at every opening. Here they are not shown, so the last picture stays up until the backdrop
 * arrives. The HUD stands still for those frames, which nobody sees.
 *
 * A frame is good when it ran the game's compose pass (scene) or drew a picture of its own across the canvas (backdrop).
 * Only frames right after a good one are held, and at most `limit` in a row: a real cut to black (a loading screen, a
 * movie) comes that many frames late and no later. */
typedef struct { int good, held; } SceneHold;
static inline int scene_hold_step(SceneHold *s, int scene, int backdrop, int limit) {
    if (limit <= 0) { s->good = s->held = 0; return 0; }
    if (scene || backdrop) { s->good = 1; s->held = 0; return 0; }
    if (!s->good || s->held >= limit) { s->good = s->held = 0; return 0; }
    s->held++;
    return 1;
}
