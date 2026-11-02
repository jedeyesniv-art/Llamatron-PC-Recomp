# Remaster playbook - how to port the next game

Written 2026-11-02 after the Photon Storm pilot (branch `remaster`, commits 2b9fed8 and dbbb859). It records how the
pilot was built, what went wrong on the way, and a recipe for the next games. Keep it up to date with each port.

## Decisions (owner)
- Every remastered game gets **run-ahead on by default** (`remaster_ahead` = 2) as well as the remaster itself
  (`remaster` = 1). F8 switches the remaster off, F7 cycles run-ahead 0..3.
- **No public push** until all the planned ports are done. Commit on the `remaster` branch as we go.
- Owner's verdict on the pilot: "REALLY cool". Run-ahead: "really good".

## What the remaster is (one paragraph)
The 68000 game runs unchanged. During play, hooks on the game's **draw primitives** (its sprite routine, dot
plotters, special-case draws such as the ship) record each object instead of drawing it into the ST screen. We
draw those objects ourselves over the ST picture (HUD, backgrounds, text: everything not caught), composed at 4x,
**interpolated** between the game's 50 Hz ticks at the display's refresh rate, with rotation where we know an exact
angle. Run-ahead then shows the newest draw pass, or one from a copy of the machine run 1-2 frames ahead, which
removes the delay the interpolation adds and then some.

Not object tables: the pilot plan expected to read each game's object tables. Hooking the draw primitives turned out
far better. It covers every object type at once (enemies, shots, particles, stars, markers), uses the game's own
positions and images, and needs no per-type inventory.

## Architecture
- `src/remaster.h/.cpp`: generic, reused as is.
  - Capture: `BeginFrame(screen)`, `Add(screen, Item)`, `EndFrame(screen)`, `Hold()`, `Stop()`. An `Item` is a
    sprite (image id + x / y + clip lines + `snap`) or a dot (colour, w x h), optionally rotated (`angle`, `angle_base`).
    Its key is caller + object pointer; `Add` appends an occurrence count so keys are unique within a frame.
  - Images: `DrawInSandbox(machine, SandboxCall)` runs any game routine in a throwaway copy of the machine onto a
    cleared screen and returns what it drew (colour indices + offset). `DecodePlanes` reads raw 4-plane data.
    Textures are rebuilt only when a colour the image uses changes.
  - Display: `RunAhead(machine)` then `FrameShown(Machine::FrameBase())` after every `RunFrame`; `Draw(...)`
    composes at 4x and hands it to `crt::DrawPicture` with src 320 x 200 (the CRT shader's supersampled mode).
  - Run-ahead: copy machine (`sandbox` + `ahead` = true: no picture, sound or files, hooks still capture into a
    separate capture), run N frames with the controls held, show its newest complete pass. The game side registers
    save / restore of its own C++ state with `SetAheadState`.
- Per game (Photon Storm: `stgames.cpp`, section "Photon Storm: the remaster", ~250 lines): the hooks, the
  sandbox call set-up, the HUD exclusions, the run-ahead state. **For the next port, first move this into its own
  file (e.g. `src/remaster_ps.cpp`) and give each game the same shape.**
- `main.cpp`: presents every vsync while `remaster::Active()` (clock pacing), alpha = how far into the current game
  frame we are; F7 / F8; `--remaster-*` test options.

## Recipe for the next game (the order that worked)
Budget for the pilot: one long session, including building the generic part. The next game should mostly be
the per-game steps 1-6 plus verification.

1. **Find the frame loop.** Disassemble a play-time RAM dump (`--dump=N:<file>`, copy `Tools/dis.py` /
   `xref.py` elsewhere and run them on the dump). Look for the main loop that waits on the VBL countdown (the 50 fps
   divisor we already patch for every game) and the call to the screen's draw function. Note the "screen being
   drawn" pointer and the triple-buffer rotation (PS: `jsr (a3)` at $4620C, screen at $4F1B2, rotation
   $4F1B2/6/A). The draw call becomes `BeginFrame`; the instruction after it becomes `EndFrame` (needed for
   run-ahead).
2. **Find the primitives.** `xref.py` on the screen pointer and on likely routines; the sprite routine is the one
   called from everywhere. Plotters are tiny routines that read a row-offset table. Special big draws (the player
   ship) read their own frame tables.
3. **Log the sprite routine** with a temporary hook: return address (and one level up if a centring wrapper sits in
   between: PS $44C42 -> $44BA8, caller at sp+6), a0 (sprite), d0 / d1 (position), a1 (screen), a3..a6. One
   register will be the object (PS: a5). Log only a few frames every 200 (`frame % 200 < 3`) or the log explodes.
4. **Write the capture hooks**: replace the first instruction with ILLEGAL (`Machine::AddHook`; check the original
   bytes first; install only once the program has unpacked itself and play has started). In each hook:
   - Not capturing / decoding sandbox (`m.sandbox && !m.ahead`) / drawing into another screen than the one being
     captured: do exactly the replaced instruction and continue.
   - Capturing: record the item and return without drawing (`rts` emulation). The restore list stays valid (the
     PS routine keeps its end marker at (a2)).
   - Unknown image: queue a sandbox decode **and let the game draw it this once**. Decode between frames (in the
     game's Update, never inside `cpu::Execute`: Musashi isn't re-entrant).
5. **Sandbox call set-up**: copy every register, then poke whatever the routine reads from RAM to be in its
   "main loop drawing" state. PS needed: the all-screens flag off, clips wide open (lines $57FC6/8, 16-px groups
   $57FC0/2), the colour parameter $57FD0, and the **draw mode $46420 = 0** (0 while drawing, 2 while
   restoring; the wrong value takes another path and draws nothing). Draw at a fixed, safe spot (x a multiple of
   16) and clamp it: header fields differ by sprite type.
6. **Exclusions**: draws with the "into every screen + background" flag (PS $4630A) are HUD, so leave them; also
   one-off HUD painters that write each buffer in turn with the flag off (PS $14F3E..$14F7A). Find these with the
   pixel check below: they show up as a missing HUD icon.
7. **Pause**: if the game freezes its last picture, call `Hold()` (the last items stay). Other non-play states:
   `Stop()` (the ST picture as it is).
8. **Run-ahead state**: every C++ global a hook writes during play (PS: analog heading, drift, shot table,
   capture flags) goes into the `SetAheadState` save / restore.
9. **Settings**: `remaster` 1 and `remaster_ahead` 2 by default (owner).

## Verification (all headless, deterministic, fast)
- **Pixel identity**: `--remaster-capture --mshot=N:<bmp>` composes the frame in software (whole pixels, the
  game's x snapping, no rotation) and compares it with `--shot=N:<bmp>` from a run without capture. Same input
  script, same settings (turn off anything we change on purpose, e.g. `ps_analog=0`). Goal: zero pixels different
  over a few minutes of scripted combat (PS: 28 of 28 frames identical). Each difference found a real bug.
- **Game unchanged**: `--dump=N` with and without capture (and with `--remaster-ahead=3`), then compare. Only
  drawing internals may differ (restore lists, the sprite routine's temporaries, stack, screens). Use the
  `rd.py`-style comparison that skips the screens.
- **Matching quality**: the log line every 500 frames: "N items a frame: X% slid, Y% jumped, Z% new". PS: ~96%
  slid; 8% jumped in explosion-heavy stretches (particles re-keyed by occurrence order).
- **Latency**: `--remaster-probe` logs the rotated item's angle per frame shown; turn the stick at a known frame
  and compare `--remaster-ahead=0..3`.
- **Cost / pacing**: windowed run, the "timing:" log line (presents/s, longest gap). PS: 120 presents/s; run-ahead
  ~0.3 ms per frame run ahead; a sandbox decode ~0.5 ms, once per sprite.
- `Tools/smoke_all.ps1`: headless runs don't capture, so it checks the rest still works.

## Pitfalls hit in the pilot (avoid them next time)
- **Sprite header fields differ by type**: PS type 4 has no width at +6 ($8000): the sandbox drew off-screen and
  got an empty image. Clamp the sandbox position.
- **Hidden mode variables**: the sprite routine branched on $46420 (main-loop phase). Read the routine's first
  ~40 instructions for every absolute address it tests and poke them all in the sandbox.
- **x snapping**: types 0 / 2 / 5 do `andi #$FFFE,d0`. We draw at the exact x (smoother), but `Item::snap` lets the
  software compose reproduce the original for the pixel check.
- **Plotters skip, not clip**: PS $442F2 skips the whole 2 x 2 dot at y >= $C6, and every plotter skips above the
  top line. Mirror skip rules exactly; don't approximate them with a clip rectangle.
- **One-off HUD painters** (see recipe step 6).
- **Palette flashes**: rebuilding all textures on any palette change stalls; textures depend only on the colours
  their image uses (colour 0 = transparent never counts).
- **CRT shader**: it used integer `Load` at the source size, so a 4x texture showed its top-left quarter. Fixed
  with a supersampled mode (colour.w = 1: continuous horizontal taps, each line the average of its halves).
- **Off-play phase shift**: skipping the drawing makes the main loop finish sooner. Screens drawn every 2nd VBL
  (title, high scores) then land one VBL out of step with the original. Game logic is unaffected; this is
  expected in pixel checks outside play.
- **Windowed runs aren't deterministic** (real-time pacing): compare only headless runs.
- **Test settings drift**: one comparison "failed" completely because the test folder had `ps_analog=1` and the
  reference `0`. Keep a settings file per test role.
- **Tooling**: build with `& cmd.exe /c C:\LlamatronRecomp\build.bat` from PowerShell. Bash heredocs containing
  quotes or backslashes broke twice: write edit scripts to files instead. Source files are CRLF; Python
  `open(p, 'rb')` patterns need `\r\n` handled. PowerShell `Set-Content -Encoding utf8` adds a BOM: avoid it.
  **CLAUDE.md contains raw CR bytes inside lines** (paths like `Private\r...`): edit it only byte-safely (`'rb'`
  / `'wb'`), never in text mode, which turns them into line breaks.

## Open issues from the pilot (fix in passing)
- An occasional 22-26 ms present gap (about once per 10 s) at 120 Hz: not caused by run-ahead or decodes.
  Suspects: texture uploads, the OS.
- Explosion particles keyed by occurrence order: ~8% jump instead of sliding in busy moments. Better: match within
  a (caller, object) group by nearest position.
- The paused picture: ours keeps the last frame's shots, the original drops them.
- CRT + remaster: a little softer than the plain CRT; tune `ss` in `crt.frag.hlsl`'s supersampled `Line`.
- Run-ahead only exists inside the remaster. Plain-picture run-ahead for the other games would need the fork to
  render its frame (sandbox skips `RenderLine`).

## Notes for the next games (from CLAUDE.md and the pilot)
- **Andes Attack**: the same Arc engine as Photon Storm. Expect the same primitives (a $44BA8-style sprite routine,
  the three plotters, the restore list, $4630A-style flag) at other addresses: search the dump for the byte
  patterns of the PS routines first. The likely quickest port. Its sky is a timer B raster (colour 0 per line):
  stays in the ST picture.
- **Llamatron** (the flagship): logic in the VBL; the main loop draws (background restore $5C40-$5C80, sprites
  $76E0-$7720; text offsets, add $10100). Shots have exact positions already (our analog-aim table). Flash
  list into colour 0 is patched. Self-modifying timer A code at $8AA0 doesn't matter here (it isn't drawing).
- **Gridrunner**: drawing in the main loop ($4BA12, 3 rotating screens, divisor $4D2E4). Snakes are linked
  chains (draw routine $1655E draws a whole snake): key per segment. The entry-lane hiding already hooks $1655E.
- **Revenge of the Mutant Camels** (three builds, same code at other addresses: one port, three disks): main
  loop draws ($4505C hook, $113FE in play). The camel's 6.25 Hz walk cycle is the artwork; interpolating its
  position between frames is still possible (an owner call).
- **Defender 2**: logic in the VBL ($1BCEA), main loop $1B2D8. Rasters change colour 0 between lines: leave the
  background in the ST picture.
- **Hardcore**: VBL hook $338BA; check whether its puck and blasters go through one sprite routine.
- **Trip-A-Tron**: not applicable (no sprites).
