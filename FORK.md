# What this branch changes, and why

Twelve changes to the Metal backend (`games/totk/third_party/d2r_gfx/gfx/nvn/`) on top of MacNativeGames `f9f7aa8`
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
| 11 | gfx(present): a frontend's overlay box drawn into the presented picture | `mtl_front_overlay()` takes a premultiplied BGRA bitmap and a position in drawable pixels; the present pass draws it over the picture with alpha blending, in a pipeline of its own. Staging in `front_overlay.h`, with a GPU-free test | On a variable-refresh display macOS varies the refresh only while the game's layer is the one thing on the screen that changes. A frontend's statistics box redrawing ten times a second in a layer above the game kept the display on its fixed grid: 21.8% of pictures stayed up for a second refresh at about 94 fps, against 0.17% with the box drawn into the picture and 0.27% with no box |
| 12 | gfx(botw): a waited-for render target is learned after eight frames of waits | A second learning rule beside the 120 waits: a wait in 8 frames, each no more than 120 frames after the one before (`late_learn.h`, with a GPU-free test; the write-back GPU probe drives it through `late_count`). `LATE_MAX` 16 to 32; a target that is due and finds no slot asks again at its next wait; the table of targets being counted holds 64 and replaces leftovers first. `TOTK_BOTW_LATE_RUN=<frames>` changes the eight, 0 switches the rule off | The eleven targets of the open world were learned one after another, 120 waits each (the wait for one leaves the others' renderings finished when their flush looks), the two cube maps of every tenth frame last: with HDR at about 95 fps, frames took 12 to 16 ms instead of 10.5 and every tenth one 19.6 ms until about 2,400 frames after the world appeared, and again after each new scene. Now the last is learned about 180 frames after the world appears and the frame time is at p95 11.0 ms from the next 300-frame window on. Upstream `main` at `3edfb0b6` has the 120-wait rule alone |

Commit 6 stands on its own in what it changes, and is the one most worth having. Commit 10 is two lines and stands on
its own too. Commit 11 does nothing until a frontend calls it.

## What has and has not been checked

Checked: `make -C games/totk/third_party/d2r_gfx/gfx/nvn/tests run` passes at commits 4 to 7, at 10 and at 11. Cores built
from commits 1 to 6 (TOTK 1.4.3) and 1 to 10 (BOTW 1.9.0) run on the M6: both games survive 30 s marked not active;
BOTW HDR engages on every gameplay frame. With commits 8 to 10, at 2560x1440 with MetalFX to 3840x2160, with and
without HDR: no dark picture and no switch of the surface at the weapon wheel or the pause menu, window shots of the
wheel with and without HDR agree, and the tenth-frame stall is gone (once the targets are learned: see commit 12). With commit 11, cores of both games built from
commits 1 to 11, 3840x2160 with HDR: a 691x502 box updated ten times a second shows in window shots, translucent over
the picture, also after 6 s marked not active; pictures held for a second refresh as in the table (TOTK at about
115 fps: 0.03% with the box, 0 without); frame time p50 / p95 10.68 / 11.26 ms with the box against 10.64 / 11.19
without (BOTW). With commit 12, cores of both games built from commits 1 to 12, 2560x1440 with MetalFX to 3840x2160 and
HDR, BOTW 1.9.0 loading a save into the open world, three runs with the rule (the last on this commit, two on
earlier versions of it) and one with `TOTK_BOTW_LATE_RUN=0` (on an earlier version): with the rule every target is
learned 175 to 225 frames after the world appears (fifteen slots in use, no target without a slot), the 300-frame
window that ends about 700 frames after it has frame time p95 11.3 to 11.8 ms, and every window after that 10.9 to
11.2 ms with no picture held for a second refresh; without it p95 stays at 19.6 to 19.8 ms until about 2,400 frames
after the world appears. TOTK 1.4.3 on the same build: 115 fps, 1 picture of 3,000
held, as before. The tests pass at 12; six mutations of the counting each fail `t_late_learn`.

Not checked:

- iOS, and every game but the two named.
- TOTK on a core built with commits 7 to 12 beyond scripted runs of 30 s to two minutes in one cave, with HDR, which
  went without a failure. Commits 7 and 9 change what TOTK's HDR does or uses (9 is on for it by default); 8 and 10 are off or
  absent there.
- Commit 8 with a 3840x2160 canvas leaves one dark picture of 4 to 9 ms at each opening of the weapon wheel. Menus
  other than the weapon wheel and the pause menu, cutscenes and loading screens were not looked at one by one: where a
  cut shows neither the scene nor a large still, the last picture stays up to 100 ms longer.
- Commit 9 with frame interpolation, and with an SDR picture in an `_sRGB` format (the path is there, nothing here
  produced one).
- Commit 10 anywhere but one place in the open world, and for memory: each slot in use holds up to four 1 MiB buffers.
- BOTW HDR against a reference, and over a long session by measurement: it is what the game is played in on this
  setup, the scripted runs last under a minute in one place of the open world, and nothing compared its colours or
  brightness with the console's picture. With 2560x1440 and MetalFX to 3840x2160 it runs at about 95 to 110 fps
  where the same scene holds 120 without HDR.
- BOTW 1.9.0 HDR through MacNativeGames' own app: its settings panel has no HDR row for BOTW, so there it needs
  `TOTK_HDR=1` or `hdr.txt`.
- Commit 12 beyond loading one save into one place: entering and leaving a shrine, a second load in one session
  (the old scene's slots are free again 2,000 submissions after its last rendering), long play, and the write-back
  GPU probe as changed (it compiles with the backend in the tests; it was not run). The rule shows that a target is
  rendered and read again and again, not that its reader can do with the rendering before the last: a target rendered
  and read in nine frames and never again gets the eighth rendering at its ninth read (the 120-wait rule needed 121
  for that). None is known in the game, and none was looked for beyond the runs above. Four more targets are learned
  than before, those the flush waits for only while the others are being learned.
- Commit 11 on a BGRA8 drawable (every run had the FP16 surface), with a box that reaches past the drawable, with
  frame interpolation, and for what its first use costs (a shader library and a pipeline are built in the present
  pass, once per drawable format).
- Everything was measured on one Mac and one television.

## How it was made

The changes were written with Claude (Anthropic) and, for parts, GPT through Codex; the commits carry
`Co-Authored-By`. They were cross-reviewed by other models (Gemini, DeepSeek, Kimi, GPT).

## Log

- 2026-11-14: branch published. Commits 1 to 7 on `f9f7aa8`.
- 2026-11-14: commits 8 to 10 (the menu flash, the HDR surface, the tenth-frame stall), merged from the series branch.
- 2026-11-14: commit 11 (a frontend's overlay box drawn into the presented picture), merged from the series branch.
- 2026-11-14: commit 12 (a waited-for render target is learned after eight frames of waits), merged from the series
  branch.
