# Remaster playbook - how to port the next game

Written 2026-11-02 after the Photon Storm pilot (branch `remaster`, commits 2b9fed8 and dbbb859). It records how the
pilot was built, what went wrong on the way, and a recipe for the next games. Keep it up to date with each port.

## Decisions (owner)
- Every remastered game gets **run-ahead on by default** (`remaster_ahead` = 2) as well as the remaster itself
  (`remaster` = 1). F8 switches the remaster off, F7 cycles run-ahead 0..3.
- **No public push** until all the planned ports are done. Commit on the `remaster` branch as we go.
- Owner's verdict on the pilot: "REALLY cool". Run-ahead: "really good". After playing it (2026-11-02): "feels
  fantastic"; run-ahead 2 is "the sweet spot, 3 felt indefinably odd" - keep 2 as the default. Andes Attack: "really
  good, no notes". Llamatron: "plays excellently". Gridrunner: "almost perfect" - the one issue (level 4's rising
  text dark) fixed the same day.

## Where we are (2026-11-03; start here next time)
- Done, committed on `remaster`, not pushed: Photon Storm, Andes Attack, Llamatron, Gridrunner, Revenge of the
  Mutant Camels (1 MB STE + TT Megamix; not the 512K build, see its notes), Defender 2, Hardcore. Each
  verified pixel-identical to the original (software compose, run-ahead 0) and with unchanged game RAM (Hardcore:
  but for two cosmetic trail rings, see its notes). All planned ports are done; Trip-A-Tron doesn't apply.
- Next: Llamatron's shots at their exact analog positions (owner), then the release.
- Later (owner): Llamatron's shots at their exact analog positions. Then the release: package, smoke test, push
  (owner decides when).
- Before porting the next game: scan its draw core for every global it reads (`Tools/absreads.py`: each `$xxxxx.l` operand in
  the routine) and sort them into clips / mode / colour / internal; check new sprites
  with run-ahead 2 as well as the run-ahead 0 pixel check.

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
- Shared hook helpers (remaster.h): `QueueImage` / `DrawQueued` (sprites seen for the first time, drawn in the sandbox
  between frames from the game's Update), `Decoding(m)`, `Key(caller, object)`, `Return(m)` (rts).
- Since the Camels: `kBlack` pixels (colour 0 drawn) with `Image::opaque` / `SandboxCall::opaque_black`;
  `Item::fine_x / fine_y` (sub-pixel display position) and `Item::period` (a repeating pattern slides the short way
  when it wraps); `SetShownColour0` (calmed colour 0).
- Matching between frames (remaster.cpp `Match`): within a key group (the key without `Add`'s occurrence count),
  each item takes the nearest item of the previous frame (greedy, closest pairs first; an image change costs 4 px;
  further than 40 px on the screen = a jump, drawn where it is). `Item::world` + `wx` / `wy` / `wrap_x` match in
  the game's world coordinates instead: a scrolling landscape of identical tiles 16 px apart matches at any scroll
  speed. So keys can be coarse (just the caller) when the object isn't in a register.
- Per game: `src/remaster_<game>.cpp` with `Start()` / `Update(m)` called from the game's controls code (Andes:
  `remaster_andes.cpp`). Photon Storm's stays in `stgames.cpp` (section "Photon Storm: the remaster"): its ship and
  shot hooks share the analog controls' state, so moving it out would mean exposing that; not worth it.
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
   register will be the object (PS: a5). If none is, key by the caller only and let the matcher pair them
   (Andes); give world coordinates if a world -> screen routine sits in between. Log only a few frames every 200 (`frame % 200 < 3`) or the log explodes.
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
- Tools: `Tools/remaster_pix.sh` (the pixel + RAM check below, both runs in one go), `--remaster-writers=N` (what
  still writes into the screen), `Tools/mapaddr.py` (a build's addresses in another build) and `Tools/absreads.py` (what a
  draw core reads).
- **Games whose state depends on CPU speed** (Llamatron: the original alone diverges at x4 or x16 vs x8; random
  numbers / wave starts timed by drawn frames): skipping draws acts like a CPU speed change, so a long run drifts
  from the original after the first death or wave change. Verify in windows before that (start at several waves
  with `--poke`), with RAM identical in the window. After it, compare `--mshot` with `--shot` of the same capture
  run: outside play they must be equal (no stale items).
- **Pixel identity** (always with `--remaster-ahead=0`: with run-ahead the composed frame is a later frame than
  the ST screen and every moving thing differs): `--remaster-capture --mshot=N:<bmp>` composes the frame in software (whole pixels, the
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

## Pitfalls hit in Defender 2
- **Most drawing outside the engine's core**: a dozen small routines. `--remaster-writers=N` (any game: logs the
  code writing into the screen during captured passes N and N + 1; `cpu::g_write_watch`) lists what is left;
  repeat until only the HUD remains. Run-time entries the static xrefs miss (`jsr` into a routine's middle): log
  jumps into the range with `cpu::g_profile`.
- **OR-ed pixels**: lasers, stars and particles are OR-ed into one plane, so their colour depends on what is
  under them (a laser over the planet barely shows). `remaster::IndexAt` gives the colour index under a pixel as
  drawn so far this pass (captured items, else the screen); OR the plane in and emit an ordinary dot. Lasers are
  several spans OR-ed on one row: gathered per row and handed over as runs of one colour - flushed before the next
  other item, so they keep their place in the drawing order.
- **Restore lists written next to the drawing**: where a routine writes a restore pointer and then ORs (stars),
  keep the pointer write and skip only the OR; a skipped count increment with no pointer would leave garbage.

## Pitfalls hit in the Camels
- **Opaque drawing**: some routines write whole words (blocks, stripes, the message) or clear a strip before
  drawing (the buildings): their black covers stars behind them. Images carry it as `kBlack` (colour 0, drawn):
  `Image::opaque` for decoded data, `SandboxCall::opaque_black` (two sandbox runs, onto colour 0 and 15: what
  matches was drawn) for routines.
- **Calm background**: with `Machine::CalmBackground` the ST picture's colour 0 isn't the register; drawn black
  must use the shown colour (`remaster::SetShownColour0`, from main.cpp before Draw / ComposeSoftware), else a
  colour-0 flash shows through as a red gradient.
- **Things that move less than a pixel a frame** (every few frames) look stepped even when interpolated:
  `Item::fine_x / fine_y` (display only; the pixel check ignores them) from the game's own fractions.
- **Finding an uncaptured draw**: take `--shot` of a capture run (the ST picture without the items) to see what's
  left, then trace writes to that screen area per instruction (`cpu::g_profile`) during the pass. Here it was a
  sprite's first sight (an animated score popup: each new image is drawn by the game once).
- **First sights in the run-ahead copy** (owner 2026-11-03: the camel flickered at level starts, in the attract
  demo and on the first duck): the copy meets a new image before the real machine; letting the game draw it there
  lost the sprite for a frame (the copy's screen is never shown). Now hooks keep the item in the copy
  (`if (!m.ahead) return original();` after `QueueImage`), and `RunAhead` decodes the queue right after the copy's
  run, from its state (all five ports). Checked: the camel in every frame shown at level starts and in the demo;
  RAM with run-ahead 3 byte-identical to before in the other four games.
- **First sights in pixel checks**: a frame where a new image appears differs where captured items overlap it
  (the game drew it under them). Expected; pick another frame.

## Pitfalls hit in Llamatron
- **Compiled sprites**: Llamatron compiles sprites into 68000 code for 8 shifts (x & $E): they land on even x.
  Record the top-left the routine computes (it centres on d0 / d1) and snap that; draw the sandbox copy at an even
  top-left, else the image's own offset gets snapped twice.
- **Hold must show the screen on display**: a hold that starts between a draw and its display kept the previous
  frame's list. `FrameShown` now takes the list drawn into the shown screen while holding (fixes PS / Andes too).
- **Frozen phases**: the llama's death sets the VBL handler to an `rts` ($5664) and the game draws nothing: hold,
  don't stop, or the sprites vanish from the frozen picture.
- **Leave short-lived effects to the game**: the shaped draws (warp-in, materialising) are a new image every frame;
  capturing them filled the cache with hundreds of one-use images and some sandbox draws never returned.
- **Draw order**: items go over the ST picture, so where a sprite the game still draws itself crosses a captured
  one, the order can differ from the original (a few pixels). (Most of the 6-14 px seen were raster colours, fixed.)
- **Raster colours** (found with Gridrunner; it also explained earlier "draw order" pixels in Llamatron): games
  change palette entries per line (timer B). Machine records every line's palette (`LinePalettes`); items take the
  colours of the lines they cover, row by row when an image uses a colour that changes within the frame. Textures
  are cached per colour set.
- **Capture only inside the play pass**: a game can draw through the same core from other frame routines while
  the capture flag is still set (Gridrunner's held states): bracket it (begin hook on, end hook off).
- **Colour words in the draw core** (Gridrunner $58054, like PS $57FD0; owner found it: level 4's rising text came
  out dark): any global the core reads that changes the picture belongs in the image id and the sandbox pokes.
  List them all with an absolute-operand scan of the core (every `$xxxxx.l` it reads), not by reading a few
  handlers. The pixel check missed it: a sprite's first sight is drawn by the game, and with run-ahead 0 the decode
  happened to run while the colour was still right. Also compose with `--remaster-ahead=2` at frames where a new
  sprite appears, and look at the picture.
- **Run-ahead state from other files**: game.cpp's analog-aim shot table is C++ state the hooks change: saved and
  restored too (`game::SaveAheadState`).

## Open issues from the pilot (fix in passing)
- An occasional 22-26 ms present gap (about once per 10 s) at 120 Hz: not caused by run-ahead or decodes.
  Suspects: texture uploads, the OS.
- (Fixed with Andes: nearest-position matching within a key group. PS jumps went from up to 8% to 0.1-1.2%.)
- The paused picture: ours keeps the last frame's shots, the original drops them.
- CRT + remaster: a little softer than the plain CRT; tune `ss` in `crt.frag.hlsl`'s supersampled `Line`.
- Run-ahead only exists inside the remaster. Plain-picture run-ahead for the other games would need the fork to
  render its frame (sandbox skips `RenderLine`).

## Notes for the next games (from CLAUDE.md and the pilot)
- **Andes Attack**: DONE 2026-11-02 (`src/remaster_andes.cpp`, its header comment has the addresses). Same engine
  family as PS but compiled differently: no byte pattern matched; the shape did (one sprite routine with a type
  table, mode word 0 draw / 2 restore, clip words, a restore list). Differences: the callers write the restore
  list, and the restore copies background (not XOR), so skipping a draw is safe without touching the list; the
  landscape is 16 x 16 sprite tiles (smooth scrolling now), matched in world coordinates from the world -> screen
  routine $1B50A (world 2048 wide). Not captured: the scanner dots (top band) and the HUD. Verified: 13 + 25
  frames pixel-identical (play, pause, death, game over, high scores, title), game RAM identical except the two
  screens with run-ahead 3, 95-99% of items slide (the jumps are new laser dashes), 120 presents/s. Took about a
  third of the pilot's time.
- **Llamatron**: DONE 2026-11-02 (`src/remaster_llama.cpp`, header comment). One sprite routine $5E8A for everything
  moving (objects, shots through the $58A2 wrapper, the llama), frame $5BFE's `jsr (a3)` at $5C10 / $5C12. Verified:
  25 frames over waves 1 / 3 / 5 / 7 / 8 identical except the draw-order pixels (one frame 14 px), RAM identical in
  every window incl. analog aim and run-ahead 3; game over / continue / title with no stale items; 120 presents/s
  in play; pause (items held under the PAUSE cup) and the Ozric tentacle (ST picture). **Owner wants to try later**:
  shots at their exact analog positions (game.cpp keeps them as doubles) for even smoother diagonals.
- **Gridrunner**: DONE 2026-11-02 (`src/remaster_grid.cpp`). The Andes draw core again ($4BCE4), play pass
  $144DA..$145E2; snakes key per segment (a3 through $4BBFE), the entry-lane hook still hides them. HUD icon painter
  $4CDBA excluded. Image id includes the colour word $58054 (level 4's rising text). Level 4 from a script: poke the
  level select's variable, `--poke=N:38ACE:4` for frames 1900-1995 (text offset of $48BCE). States 2 (between, e.g. fading into pause) and 11 hold the items; the pause screen (12) is its
  own picture. Not sensitive to CPU speed: a 12,000-frame run (pause, deaths, restart) 45 / 45 frames identical,
  RAM identical. 120 presents/s in play.
- **Revenge of the Mutant Camels**: DONE 2026-11-03 (`src/remaster_rmc.cpp`, header comment has the STE addresses;
  `Addrs kTt` the TT Megamix's, found with `Tools/mapaddr.py` from the STE dump: code bytes with address operands
  as wildcards, data addresses read from the matched instructions). Double-buffered main loop ($1A842: `jsr (a3)`
  = BeginFrame, the list's end marker after it = EndFrame); one sprite dispatcher with three handlers hooked at
  their entries (type 2 pre-shifted / even x, type 1 stretched, type 3 any x; a type 3 sprite can become type 2 at
  run time, so snap follows the type at each draw). More bespoke drawers than the others, all captured: a 2 x 1
  dot plotter (stars), falling blocks drawn inline (hooked per block; sub-pixel y from the frame counter:
  `Item::fine_y`), six ground stripes (16.16 positions: wide repeating lines with `Item::period` and `fine_x`), the
  buildings (opaque 16-px strips: `SandboxCall::opaque_black`), the level message (opaque band). Pause: not
  captured (the pause code writes text over the picture; nothing moves). Keys: a6 when it points into the object
  tables, else the caller alone (some callers leave scratch values in a6). Verified: STE and TT 20 / 20 frames
  identical over 6000 frames (play, pause, deaths, the message, game over, title), RAM identical except drawing
  temporaries, also with run-ahead 3; ~99% of items slide. The 512K build (REVENGE.TOS, D-Bug 28): not covered -
  the launcher runs the STE build for that id, and a scripted run stays on its shareware screen, so its mapped
  addresses couldn't be checked in play (most map with mapaddr.py from a dump taken there). The camel's 6.25 Hz
  walk cycle is the artwork (an owner call whether to do more).
- **Defender 2**: DONE 2026-11-03 (`src/remaster_d2.cpp`, header comment and one per hook). The Andes engine's
  frame loop and core ($4CC9A `jsr (a3)` / $4CC9C; core $4B8B0: only the player's ship uses it), but most of play
  is drawn by its own routines, each hooked: the Camels' "type 3" sprite routine ($1DF2A: enemies), 1-px and 2-px
  colour plotters, a 3 x 3 cell drawer (enemies materialising), one-plane OR-ed pixels (two star lists,
  particles, a one-plane shape routine), laser spans, the terrain outline (world matching), the two ground bands
  (pre-shifted 32-px patterns, `period` 32, sub-pixel from scroll / divisor) and the planet (opaque, drawn after
  the lasers). Left to the game: the HUD and the radar (its dots go through the plotters' inner entries). Found
  with `--remaster-writers` (below). Verified: 13 / 13 frames identical over a fight (lasers, explosions,
  materialising, death), RAM identical except drawing state (restore lists, core temporaries, stack), also with
  run-ahead 3.
- **Hardcore**: DONE 2026-11-03 - the Camels' library again, so it is an address set in `src/remaster_rmc.cpp`
  (`kHc`, found with `Tools/mapaddr.py` from the Camels' STE dump) plus two things of its own: type 6 = compiled
  sprites (8 routines by x & 14, generated at run time from type 3: each shift decoded on its own) and 2 x 2 / 2 x 1
  one-plane plotters (stars, sparks: `IndexAt` colours). Type 2 clips from line 1 here. Left to the game: the
  playfield's fixed lines and the HUD panel. Verified: 17 / 19 frames identical over 4500 frames; the other two
  (a level start, an explosion's trail) differ because two objects' trail rings ($1A1F8, $1A4F8: recent
  positions) advance per drawn frame, and outside play the capture run draws at another pace - in the capture
  run the game itself draws the trail where the remaster does. Same with run-ahead 3 (closer to the original).
  The fix found on the way: a sprite converted between types (3 -> 2 Camels, 3 -> 6 Hardcore) must be placed
  and decoded by one convention - all by their top-left now.
- **Trip-A-Tron**: not applicable (no sprites).
