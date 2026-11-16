# What this branch changes, and why

Thirteen changes to MacNativeGames `f9f7aa8` (upstream `main` of 2026-11-13), one commit each, nearly all in the Metal backend
(`games/totk/third_party/d2r_gfx/gfx/nvn/`). They were made while running Tears of the Kingdom 1.4.3 and Breath of
the Wild 1.9.0 on an M6 Mac mini connected to an LG G3 television (3840x2160, HDR, variable refresh from 40 to
120 Hz), with the runtime hosted by a frontend that owns the window.

Every measurement here was made on that one Mac and that one television. Nothing was built for iOS or tried with
another game. Nothing has been sent upstream: `main` has moved on since `f9f7aa8` and `nvn_mtl.m` no longer merges
cleanly, so the commits need a rebase first.

## A crash fix

**A commit held while the app is not active is not a GPU hang** (`048e685a`). Ten seconds after a frontend marks the
game not active (`mtl_app_active(0)`, for a pause menu), the completion watchdog ended the game: it had counted a
command buffer before the not-active gate that was holding it. A watched commit is now counted after the gate. The
rule moves to `gpu_watchdog_rule.h` with a test, and no longer underflows when a completion lands between the
watchdog's reads.

The counting before the gate was still in upstream `main` at `2f81ef5e`. This is the commit most worth having. Its
code depends on nothing else here; its test hunk adds to the `tests/Makefile` that `7f255bea` creates.

## Variable refresh

macOS varies the refresh only for a full-screen window, and only while the game's layer is the one thing on the
screen that changes.

- **A frontend can ask for the present thread** (`7f255bea`). A display link at one fixed rate keeps a
  variable-refresh display on its fixed grid: at about 115 fps, 9.6% of pictures were held for a second refresh,
  against 0.02% from the present thread. `mtl_present_link_host(0)` asks for it, for use while the frontend's window
  is full screen on a display with a refresh range; any other value ends the request and the configured mode
  returns. The switch is applied on the game thread. It no longer leaves a link or a queued frame without a
  presenter, or takes a late first tick of a new link for a stall, and the handoff has a GPU-free test.
- **A frontend's overlay box drawn into the presented picture** (`943dcd1a`). A frontend's statistics box redrawing
  ten times a second in a layer above the game also kept the display on its grid: 21.8% of pictures held at about
  94 fps, against 0.17% with the box drawn into the picture and 0.27% with no box. `mtl_front_overlay()` takes a
  premultiplied BGRA bitmap and a position in drawable pixels, and the present pass draws it over the picture; it
  does nothing until a frontend calls it. Staging is in `front_overlay.h`, with a GPU-free test.
  - Tested only on the FP16 (HDR) surface. Not tested: a BGRA8 drawable, a box that reaches past the drawable,
    frame interpolation.
  - Its first use builds a shader library and a pipeline in the present pass, and again whenever the drawable's
    format changes. That cost was not measured.
- **The present cadence log gets a second line** (`7967c29c`): how many of the last 600 intervals sit on the refresh
  grid, with a 1 ms histogram. It tells fixed-grid presentation from variable refresh. (The "held for a second
  refresh" percentages above come from the line that was already there.)

## Breath of the Wild 1.9.0

- **HDR output, experimental** (`4b684f7a`; needs `e64690af`). The game has one discrete tone-map pass, so TOTK's
  method applies: an HDR twin of the compose shader, FP16 companions for its RGB10A2 targets, capture at the final
  copy. Mac builds of 1.9.0 only.
  - At 2560x1440 with MetalFX to 3840x2160 the open world runs at about 95 fps with it and about 115 without (a
    frame takes 10.5 ms against 8.7).
  - Its colours and brightness were not compared with the console's picture.
  - MacNativeGames' own settings panel has no HDR row for BOTW, so there it needs `TOTK_HDR=1` or `hdr.txt`.
- **A picture without its scene is not shown for a few frames** (`f7f0bae8`). Opening the weapon wheel or the pause
  menu, the game draws up to three frames with only the HUD on a cleared canvas: a black flash of about 25 ms at
  every opening. A run of frames that drew neither the scene nor a menu's still of it, starting right after a frame
  that did, is now not presented; the last picture stays, for at most 8 frames or about 100 ms. `TOTK_SCENE_HOLD`
  sets the frames (8 on BOTW, 0 elsewhere) and `TOTK_SCENE_HOLD_LOG` logs it.
  - Watched at 2560x1440 with MetalFX to 3840x2160: no flash. With a 3840x2160 canvas one dark picture of 4 to 9 ms
    remained at the weapon wheel when it was measured.
  - Other menus, cutscenes and loading screens were not looked at one by one. Where a cut shows neither the scene
    nor a large still, the last picture stays up to about 100 ms longer.
- **The FP16 surface stays through frames without an HDR picture** (`8161cdbd`). A presented frame without an HDR
  picture switched the layer to BGRA8 and back, draining the GPU each time: at every opening and closing of the
  weapon wheel. While HDR is on, the layer now stays FP16 / EDR and a picture
  without an HDR companion is decoded into it at SDR white. `TOTK_HDR_SURFACE_KEEP=0` is the behaviour before. This
  one is not specific to BOTW: it is on by default for TOTK's HDR too. Not tested with frame interpolation, or with
  an SDR picture in an `_sRGB` format (the path is there; nothing here produced one).

- **The text's glyph arrays are read from copies** (`8fb95d07`). Dialogue and menu text was drawn all over the screen
  for a frame or two while it faded in or out, at up to 120 fps. The game draws a string as instanced quads with a
  read-only storage array of 64 bytes a glyph, which the GPU read in place from the game's memory. Under
  `TOTK_SYNC_LAG`, the default for this game, the game's wait for a submission settles for the one before, and the
  game rewrites those arrays for its next frame while the submission that draws them is still on the GPU.
  - `OOT_RACE_CHECK=1` reported 71 such arrays in a run of thirteen openings of the pause menu at about 108 fps,
    changed up to 4.9 ms before the GPU was done. With the copies it reports none: a copied array is no longer read
    in place.
  - The copies are the ones Ocarina of Time has (`OOT_SBUF_SNAPSHOT`), made by default for the text's vertex shader
    alone and up to 64 KB: 19 copies of 0.6 KB a frame in that run, with the frame time unchanged (9.02 ms against
    9.12). `OOT_SBUF_SNAPSHOT=0` switches them off, and a positive value copies every shader's ranges.
  - One shader on purpose. Other read-only storage is rewritten early too (56 ranges reported in the run) and is
    left alone, because the cached copies are not checked against every GPU writer of a range.
  - A frame with the scattered text was never captured (36 sampled frames without the copies were clean), so the fix
    rests on the reported writes and on what the shader draws. A write by another thread during the copy itself is
    not excluded. Measured in one place in the open world and in the pause menu; dialogue was not driven.
    `TOTK_SYNC_LAG=0` also ends the early writes but cost about a quarter of the frame rate there.

### Render targets the game reads back

The game flushes some small render targets and reads them on the CPU. The flush waits for the GPU on the game's
main thread unless the target is "learned"; a learned target's rendering lands in memory when its submission
completes, and the flush does not wait. Two commits make the learning reach the targets that matter, and sooner.

- **Sixteen slots instead of eight** (`8cf9f2eb`). In the open world the eight slots went to per-frame targets, and
  two cube maps rendered every tenth frame never got one: every tenth frame took 13 ms instead of 8.6 (20 instead
  of 10.1 with HDR). Frame time p95 went from 12.5 to 9.6 ms, and with HDR from 19.7 to 11.3 ms.
  `TOTK_BOTW_LATE_LOG=1` lists the renderings the flush still waits for.
- **A target is learned after eight frames of waits** (`50b789c4`; needs `8cf9f2eb`). Learning took 120 waits a
  target and happened one target after another, because the wait for one leaves the others' renderings finished by
  the time their flush looks. After a save loaded, frames took 12 to 16 ms instead of 10.5, and then every tenth one
  19.6 ms, until about 2,400 frames after the world appeared; each new scene started it again.
  - The 120-wait rule stays. A target is now also learned after 8 waits, one per frame, each no more than 120
    frames after the one before (`late_learn.h`, with a GPU-free test). `TOTK_BOTW_LATE_RUN=<n>` sets that count; 0
    switches the new rule off.
  - Measured: everything is learned 175 to 225 frames after the world appears. Frame time p95 is 11.3 to 11.8 ms
    in the 300-frame window ending about 700 frames after it, and 10.9 to 11.2 ms from then on. Two of the three
    runs and the comparison with the rule off were made on earlier versions of this commit.
  - In the measured scene fifteen targets are learned where eleven were, so there are 32 slots. Each slot in use holds up to
    four 1 MiB buffers (128 MiB if every one were allocated); memory was not measured.
  - Known gap: the rule shows that a target is rendered and read again and again, not that its reader can do with
    the rendering before the last. A target rendered and read in exactly nine frames and never again gets the
    eighth rendering at its ninth read (the 120-wait rule needed 121 for that). None is known in the game.
  - Only loading one save into one place was tested: not entering and leaving shrines, a second load in one
    session or long play. The
    write-back GPU probe was changed to drive the new rule and compiles, but was not run.

Upstream `main` at `3edfb0b6` has eight slots and the 120-wait rule alone.

## Cheaper HDR for Tears of the Kingdom

- **Fewer whole-image copies on the HDR path** (`19e0ecfa`). Most of the copies between the FP16 companions and
  their originals had no reader. They are now counted, a companion is copied back only before something reads its
  original, and the copy into a companion is skipped while the original is unchanged. `TOTK_HDR_CAPTURE_LOAD`,
  `TOTK_HDR_RING`, `TOTK_HDR_MIRROR` and `TOTK_HDR_SEED` set to 0 each switch one of these savings off
  (`TOTK_HDR_SEED=2` is a stricter middle setting).
- **The scene tone map with less arithmetic per pixel** (`e64690af`). Before it the HDR compose pass cost 1.28 ms at
  2560x1440, against 0.13 ms for the game's own shader. `TOTK_HDR_COMPOSE`: 0 is the original source, 1 has the
  constants multiplied out, 2 (the default) also skips the hue and purity step at or below the SDR pivot, 3 is 2
  without that skip.
- **Replacement AA plus sharpening without the copy-back** (`1e0c415a`). With sharpening and either FXAA Quality
  or SMAA on, the finish pass saves one whole-image copy, which cost 0.17 ms at 2560x1440 with HDR.
  `TOTK_FINISH_DIRECT=0` restores it.

## How it was tested

`make -C games/totk/third_party/d2r_gfx/gfx/nvn/tests run` passes at the head of the branch; it was run at most
commits as they were made, not at every one. Cores of both games built from the first twelve commits run on the M6, and a Breath of the Wild core built from all
thirteen. The
measurements come from scripted runs there, read from the runtime's log, and Breath of the Wild is played on that
build in HDR.

Tears of the Kingdom has had only short scripted runs (30 s to two minutes in one cave, with HDR) since the Breath
of the Wild commits went in. They went without a failure, but the FP16-surface commit changes TOTK's HDR path and the
BOTW HDR commit edits headers it shares, so longer TOTK play is still owed.

## How it was made

The changes were written with Claude (Anthropic) and, for parts, GPT through Codex; the commits carry
`Co-Authored-By`. They were cross-reviewed by other models (Gemini, DeepSeek, Kimi, GPT); the thirteenth by three of them, without
DeepSeek.
