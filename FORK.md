# What this branch changes, and why

Ten changes to the Metal backend (`games/totk/third_party/d2r_gfx/gfx/nvn/`) on top of MacNativeGames `f9f7aa8`
(upstream `main` of 2026-11-13), made while running Tears of the Kingdom 1.4.3 and Breath of the Wild 1.9.0 on an M6
Mac mini connected to an LG G3 (3840x2160, HDR, 40-120 Hz). One commit per change, each standing on the ones before
it. Nothing here has been sent upstream. Upstream `main` has moved on since `f9f7aa8` and `nvn_mtl.m` no longer merges
cleanly, so the commits need a rebase before they could be.

This file is kept up to date as the branch changes; the log at the end says what changed when.

| # | Commit | What it does | Why |
|---|---|---|---|
| 1 | gfx: the present cadence line also says how many intervals sit on the refresh grid | A second log line per 600 pictures: the share of presented intervals on the refresh grid, and a 1 ms histogram | Tells a fixed-grid presentation from one that follows a variable refresh display |
| 2 | gfx(hdr): fewer whole-image copies on the HDR path, and counters for them | Counts the HDR path's seeds, mirrors and captures, mirrors a companion only before a reader of its original, skips the seed while the original is unchanged. `TOTK_HDR_CAPTURE_LOAD`, `TOTK_HDR_RING`, `TOTK_HDR_MIRROR`, `TOTK_HDR_SEED`; 0 restores the previous path | Most of those copies had no reader |
| 3 | gfx(hdr): the scene tone map with less arithmetic per pixel | `TOTK_HDR_COMPOSE`: 0 the original source, 1 constants multiplied out, 2 (default) also skips the hue / purity step at or below the SDR pivot, 3 is 2 without that skip | The HDR compose pass cost 1.28 ms at 2560x1440 against 0.13 ms for the game's own shader |
| 4 | gfx(finish): replacement AA plus sharpening without the copy-back | `TOTK_FINISH_DIRECT`: AA into the scratch target, sharpening back, one whole-image copy fewer | 0.17 ms at 2560x1440 with HDR |
| 5 | gfx(present): a frontend can ask for the present thread | `mtl_present_link_host(0)`: a frontend asks for mode 0 while its window is full screen on a display with a refresh range; applied on the game thread; three holes in the handoff closed; a GPU-free test of the handoff | A display link at one fixed rate keeps a variable refresh display on its fixed grid: at about 115 fps, 9.6% of pictures were held for a second refresh, against 0.02% from the present thread |
| 6 | gfx(watchdog): a commit held while the app is not active is not a GPU hang | The completion watchdog counts a command buffer after the not-active gate, not before | **A crash fix.** 10 s after a frontend marks the game not active (`mtl_app_active(0)`), the watchdog ended the game. The counting before the gate is still in upstream `main` at `2f81ef5e` |
| 7 | gfx(hdr): HDR output for Breath of the Wild 1.9.0 on the Mac (experimental) | An HDR twin of BOTW's compose shader, FP16 companions for its RGB10A2 targets, capture at the final copy; Mac builds of 1.9.0 only | BOTW 1.9.0 has one discrete tone-map pass, so TOTK's method applies |
| 8 | gfx(present): a picture without its scene is not shown for a few frames (Breath of the Wild) | A frame that drew neither the scene nor a menu's still of it, right after one that did, is not presented: the last picture stays, for at most 8 frames and 100 ms. `TOTK_SCENE_HOLD` (frames; 8 on BOTW, 0 elsewhere), `TOTK_SCENE_HOLD_LOG` | Opening the weapon wheel or the pause menu in BOTW 1.9.0, the game draws up to three frames with only the HUD on a cleared canvas: a black flash of about 25 ms at every opening |
| 9 | gfx(hdr): the FP16 surface stays through frames without an HDR picture | While HDR is on the layer stays FP16 / EDR, and a picture without an HDR companion is decoded into it at SDR white. `TOTK_HDR_SURFACE_KEEP=0` is the behaviour before | Such a frame switched the layer to BGRA8 and back, draining the GPU each time: in BOTW 1.9.0 at every opening and closing of the weapon wheel |
| 10 | gfx(write-back): sixteen learned targets instead of eight (Breath of the Wild) | `LATE_MAX` 8 to 16; `TOTK_BOTW_LATE_LOG=1` lists the renderings the game's flush still waits for | In the open world the eight slots went to per-frame targets, and two cube maps the game renders every tenth frame never got one: every tenth frame took 13 ms instead of 8.6 (20 instead of 10.1 with HDR). Frame time p95 12.5 to 9.6 ms, with HDR 19.7 to 11.3 ms. `LATE_MAX` is still 8 in upstream `main` at `2f81ef5e` |

Commit 6 stands on its own in what it changes, and is the one most worth having. Commit 10 is two lines and stands on
its own too.

## What has and has not been checked

Checked: `make -C games/totk/third_party/d2r_gfx/gfx/nvn/tests run` passes at commits 4 to 7 and at 10. Cores built
from commits 1 to 6 (TOTK 1.4.3) and 1 to 10 (BOTW 1.9.0) run on the M6: both games survive 30 s marked not active;
BOTW HDR engages on every gameplay frame. With commits 8 to 10, at 2560x1440 with MetalFX to 3840x2160, with and
without HDR: no dark picture and no switch of the surface at the weapon wheel or the pause menu, window shots of the
wheel with and without HDR agree, and the tenth-frame stall is gone.

Not checked:

- iOS, and every game but the two named.
- TOTK on a core built with commits 7 to 10. Commits 7 and 9 change what TOTK's HDR does or uses (9 is on for it by
  default); 8 and 10 are off or absent there.
- Commit 8 with a 3840x2160 canvas leaves one dark picture of 4 to 9 ms at each opening of the weapon wheel. Menus
  other than the weapon wheel and the pause menu, cutscenes and loading screens were not looked at one by one: where a
  cut shows neither the scene nor a large still, the last picture stays up to 100 ms longer.
- Commit 9 with frame interpolation, and with an SDR picture in an `_sRGB` format (the path is there, nothing here
  produced one).
- Commit 10 anywhere but one place in the open world, and for memory: each slot in use holds up to four 1 MiB buffers.
- BOTW HDR for more than a few minutes, or outside the opening area when it was measured. With 2560x1440 and MetalFX
  to 3840x2160 it ran at about 110 fps where the same scene held 120 without HDR.
- BOTW 1.9.0 HDR through MacNativeGames' own app: its settings panel has no HDR row for BOTW, so there it needs
  `TOTK_HDR=1` or `hdr.txt`.
- Everything was measured on one Mac and one television.

## How it was made

The changes were written with Claude (Anthropic) and, for parts, GPT through Codex; the commits carry
`Co-Authored-By`. They were cross-reviewed by other models (Gemini, DeepSeek, Kimi, GPT).

## Log

- 2026-11-14: branch published. Commits 1 to 7 on `f9f7aa8`.
- 2026-11-14: commits 8 to 10 (the menu flash, the HDR surface, the tenth-frame stall), merged from the series branch.
