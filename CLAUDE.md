# Llamatron (Atari ST, Jeff Minter / Llamasoft 1991) - native PC port

Hobby project, started 2026-10-22 after the Galaga Legions DX port (C:\GalagaRecomp, same owner and toolchain).
Llamatron is shareware ("please spread this game everywhere", shareware screen at boot); the disk image is still
kept out of git (Private\). The owner downloaded the image from Jeff Minter's own site (it includes the cracked
Gridrunner: Minter distributes that copy himself). Release plan: no game files in the zip, the README points to
Minter's site; bundling only with his OK.
Goal: a native Windows app (SDL3) that plays well on an Xbox pad, twin-stick style. Owner's focus: controls.

## Status
- 2026-10-22, stage 1 DONE: the original 68000 code runs on the Musashi interpreter inside a native SDL3 app with
  the ST hardware and TOS emulated at a high level (src\machine.cpp). Boots, shareware screen, version screen,
  instruction scroller, title, twin-stick game verified with scripted input (screenshots); audio produced (WAV
  dump). Not yet tested by the owner with a real pad.
- 2026-10-22: 1.0.0 portable version done (owner: "we're done, make a portable version ready for general
  play"). Tools\package_portable.ps1 [-GameData <msa>] [-Release] -> dist\Llamatron\ (exe + README + licences +
  game\; with -GameData the owner's disk is copied in) and dist
elease\Llamatron-<ver>-windows.zip (no game
  data, leak-checked) + -symbols.zip. Static C runtime: the exe needs only Windows DLLs. Original icon
  (Tools\make_icon.py -> res\llamatron.ico, a pixel llama, no game art), version resource from CMake VERSION.
  Tested: the zip unpacked fresh + the .msa in game\ plays. Not published anywhere; Steam Deck not tested.
  Next (owner): Gridrunner (GRIDRUNR.PRG on the disk), then package both games together.
- Recompilation: discussed 2026-10-22, not worth it for this game (interpreter ~2.5% of a core; risks: SMC,
  CPU-timed screens) - owner agreed to skip for now. Stage 2 (if ever): static recompilation of the 68000 code to C++ (Tools\ to write), with the interpreter kept as the
  reference (lockstep compare) and as fallback for code it can't place (self-modifying code, see below).
- Gridrunner: runs (2026-10-22), see its section; controller design pending.

## Layout (C:\LlamatronRecomp)
- Private\              (git-ignored) Llamatron ST.msa (owner's copy), disk\ = its files, tron.bin/.reloc =
                        the unpacked game (Tools\depack.py), for the analysis tools.
- src\                  the app: main.cpp (window, pacing, options), machine.cpp (ST + TOS), cpu.h (CPU interface)
                        + cpu_musashi.cpp, ym2149.cpp, input.cpp (pad/keyboard mapping), disk.cpp (MSA/FAT/depack),
                        crash.cpp (unhandled exception -> stack in the log), musashi\m68kconf.h (our Musashi config,
                        copied over the submodule's at build time).
- third_party\          submodules: musashi (kstenerud/Musashi, MIT-style), SDL (3.4.16, static).
- res\                  README.txt (the player's readme), llamatron.ico + .rc.in (icon, version info).
- licenses\             THIRD-PARTY-NOTICES.txt (SDL zlib, Musashi MIT-style, SoftFloat 2b, ayumi MIT).
- Tools\                st_disk.py (msa -> files), depack.py (TRON1MEG.PRG -> tron.bin + tron.reloc),
                        dis.py (capstone disassembly of tron.bin: dis.py <hex addr> [count]), xref.py (absolute
                        operand references: xref.py <hex>[,<hex>]). pip: capstone, pillow.
- build.bat             configure (once) + build in vcvars64 with clang/Ninja -> build\llamatron.exe (+ PDB).
                        Run it from PowerShell (cmd /c build.bat); from Git Bash "cmd //c" silently didn't build.

## Running and testing
build\llamatron.exe finds the disk image itself (game\ next to the exe, the exe's folder, nearest Private\ up).
Saves (hiscores: SCORES.YAK) go to userdata\ next to the exe; files there replace the disk's.
Options: --aspect=pal|square (default pal, owner's choice: PAL TV pixel aspect 0.92, picture 320x217 in square
units; square = 16:10) --disk=<.msa|.st> --log=<file> (default userdata\llamatron.log) --trace-tos --window (fullscreen is the
default; F11 / Alt+Enter toggle) --refresh=desktop (don't change the display mode)
Tests: --headless --frames=N --shot=N:<file.bmp> --wav=<file> --press=N:<what>[:frames], what = j0|j1 +
fire|up|down|left|right, an SDL key name, or pad<button> (pady, padback, padb: SDL gamepad button names). The exe is a GUI app: from PowerShell use Start-Process -Wait.
Scripted path: press j1fire at frame 150 (leaves the shareware screen), title is up by ~2900, j0fire there at 3000
starts a twin-stick game. Screenshots -> PNG with pillow to view them.
Crash: the log gets "CRASH: ... llamatron.exe+0x..." frames; llvm-symbolizer --obj=build\llamatron.exe
0x140000000+offset.

## Gridrunner (GRIDRUNR.PRG, --program=GRIDRUNR.PRG) - runs, controls TBD (owner: "really challenging")
- Standard TOS PRG, Pompey Packer 2.5: unpacks itself in our ST (disk::LoadPrg). Its stub reads the packed
  length below A4: TOS's loader leaves A4 = end of text+data (Machine::Load sets it). After unpacking the code is
  at its runtime addresses (no relocation needed to read dumps): --dump=N:<file> then dis.py/xref.py <file>.
- Title says "CRACKED BY THE ALIEN... (DAMAGED KARMAS ALL ROUND!)": the copy on this disk is a cracked release.
  HISCORES.COW is its high score file (Fopen/Fread at the title).
- Uses: Initmous(relative, topmode 1 = y origin at the BOTTOM -> IKBD $0F, threshold 1/1, mousevec $4B7B6),
  Xbtimer(timer B, event count, data 1, $4B750) = an interrupt after every display line (raster colours),
  TOS's IKBD handler (Kbdvbase()->mousevec gets the packets: our IkbdSysHle), console via TOS maybe.
- Mouse handler: buttons (packet header) -> $54DD8 (bit 1 left, bit 0 right), dx/dy -> $54DD4/$54DD6, pointer
  x $54DD0 (0..319) += dx, y $54DD2 (0..199) -= dy; ignored while $4CF2E (attract demo, which moves the pointer
  itself at $1271C). Joystick fire is ignored.
- The ship sits at the pointer (anywhere on the grid); left = fire (hold), right = drop the drone at the pointer
  ($12916; it then moves towards its spot at 2 px/frame). Menus are point-and-click: title "press fire" = left;
  level select ($11F06): [-] x $65-$80 / [+] x $BB-$D6 at y $6E-$82, [LET'S BLAST!!] y $A2+, pointer starts at
  (160,170) = on LET'S BLAST; another 3-row menu at $10A70 (y $50-$59, $64-$6D, $82-$8B); high score name entry
  ($10686): a 10x4 on-screen keyboard, cells 20 px from (65,80), characters at $1096E.
- States ($4B632, VBL dispatch table $12636): 5 title, 6 level select, 3 wave intro, 1 play, 12 pause screen
  (11 entering it), 2 in between. Keys (TOS: Kbshift + Crawio): right Shift = pause (in play); pause screen:
  Space = resume ("restart"), Q = quit to title, typing "pink-floyd-are-gods" there = cheat ($1384C); left
  Shift / Alt = nosecone shot direction $49130 (0 up, 1 up+out, 2 sideways, 3 down+out, 4 down; held = repeat);
  Control = "voluntary martyrdom" (lose a life, blow up the vulnerable enemies, ghost ship fights on); letters
  a-z = a sound each ($4D870). Built-in manual "game secrets" at $10EC3.
- Pad (owner 2026-10-22, src\gridrunner.cpp): left stick moves the pointer (= ship) through a response curve
  (written straight into $54DD0/$54DD2), d-pad slowly; RT/A = left button (fire/click), LT = right button
  (nosecone: held = attached); right stick flicked up/down = one left Shift/Alt tap (one step towards up/down;
  owner: "like a button", no repeat while held; next flick after it returns below 30%); RB = Control; Back/Start
  = pause/resume; B = pause: quit to title (Q), title: quit (twice). Keyboard: every key goes to the ST.
  Tunables in userdata\settings.txt, reread while running (src\settings.cpp): grid_stick_speed (6 px/frame at
  full deflection), grid_stick_curve (2 = quadratic), grid_stick_deadzone (0.15), grid_dpad_speed (1),
  grid_fire_mode_swap (0). Measured: full stick 6 px/frame, half 1.0, inside the dead zone 0.
- Performance (owner: "fairly severe slowdown"): like Llamatron, all logic is in the VBL ($4B55A: rasters,
  pending screen $4D2E8, then the state machine; play state $1278A moves the objects via $14F7A) and never
  misses a VBL; the main loop ($4BA12) only draws into 3 rotating screens and waits $4D2E4 VBLs per frame (2,
  set once at $4B2C8: 25 fps by design; at 8 MHz it drops to 13-20). Now: x8 CPU in play (state 1) and $4D2E4
  = 1 in play (setting grid_50fps, default 1; 2 elsewhere) -> 50 drawn frames/s in every second of play.
  Verified the game itself is unchanged: same scripted input with 25 vs 50 fps, RAM at the same frame differs
  only in drawing bookkeeping (screen rotation, per-screen restore lists $54ECB/$55E6D/$56E0B, stack) - objects,
  score, state identical. Game memory is only written once the title has been seen (before that it holds the
  packed program). Log: "VBLs missed" next to the drawn-frames line. Tools: profile_summary.py, ramdiff.py.
- Test inputs: --press=N:mleft|mright[:frames], mx<dx>, my<dy> (per frame); pad: gl<deg>@<deflection>,
  gry<value>, glt, grt, grb; pad buttons as for Llamatron (padback, padb...). Flow: mleft at 600 (Llamasoft
  core screen), 1500 (title), 2000 (LET'S BLAST), 2400 (intro), then play (left held = fire).

## The program (TRON1MEG.PRG)
- 263 KB self-extractor: a 38-byte mover, an LZ77 stream (unpacks to basepage+$100, its own text start), a
  standard GEMDOS relocation table after the unpacked text (2529 entries), BSS cleared ($965 words). Unpacked
  text $4CFA6 bytes. Relocatable: we load it at $10100 (basepage $10000). Addresses below are text offsets
  (tron.bin / dis.py); add $10100 for the runtime address.
- Startup: Super(0), reads phystop but assumes 1 MB (stack top hard-coded $FFFFF-1000), XBIOS Random for a
  shuffle table, Bconout(4,$14) = IKBD joystick event mode, Kbdvbase(): mousevec=$58B0, joyvec=$28E0, ikbdsys=
  $7FF4 (its own ACIA handler, replaces TOS's), VBL $70 = $5398, Timer A vector $134 (sample playback: digitized
  sound through YM volume registers, auto-EOI), Fopen("scores.yak") (missing on the disk -> default table).
- Takes over the machine: no further TOS use except files. Hardware used: video base $FF8201/03, palette,
  $FF8260 (low res), $FF820A (50/60 Hz, toggled by the game's H key in pause), YM2149, MFP timer A (IERA/IMRA $20),
  ACIA $FFFC00/02, FDC status read ($FF8606=$80, $FF8604 bit 6 = write protect: we say not protected -> it saves).
  No blitter, no timer B rasters, no STE hardware.
- Self-modifying code: the timer A handler around $8AA0 patches an immediate ($8AD0, "move.w #x,d0") - the
  recompiler must treat that as data. More may exist.

## Controls (what the game does - the owner's focus)
- IKBD joystick packets $FE/$FF + state land in $28F2 (joystick 0, the mouse port) and $28F3 (joystick 1).
  Bits: 0 up, 1 down, 2 left, 3 right, 7 fire. Keys: scancodes -> table $37940 -> key-down flags $37AC1.
- One stick (stick 1 = joystick 1): move; firing is automatic in the direction of travel; fire locks the angle.
- "Robotron style": press fire on stick 2 (joystick 0) instead of stick 1 to start -> stick 1 moves, stick 2
  aims the shots ($8616 = 1: twin mode; $23AA reads both). Allowed in modes 1 PLAYER and TEAM.
- Title options ($55C0, by joystick 1): left/right cycle mode bits 0-1 of $4CF9A: 0 = 1 PLAYER, 1 = PLAYER +
  DROID, 3 = TEAM (two humans: joystick 0 is player 2); up/down toggle bit 2 (the 1/2 shown). Saved to $4EA.
- Pause: P pauses; in pause H = 50/60 Hz, S = samples on/off, T = Ozric tentacle toy, G = back to the game screen,
  O = unpause.
- Our mapping (src\input.h): pad 1 left stick/d-pad = joystick 1; right stick = joystick 0. A = joystick 1 fire,
  except on the title screen, where A = joystick 0 fire = starts the twin-stick game (decided when pressed; not
  in PLAYER + DROID, where the game ignores it). X = joystick 1 fire (title: one-stick game). Start/RB/RT =
  joystick 0 fire. Back = pause/unpause (P/O). A second pad = joystick 0 (TEAM). Keyboard: WASD + Space =
  joystick 1, cursors + Enter = joystick 0, other keys = ST keys.
- Quitting (owner: "a way to exit to the menu from a run, and then to exit the game"): the game's own X key
  ($78, VBL hook $1F46 -> $2BBA = 3 -> $21E title) aborts to the title, but only in play, not on the pause
  screen (P: VBL hook $1D32). Back = pause/resume (taps P/O by game state); B (Esc) on the pause screen =
  resume + X; B (Esc) twice within 3 s at the title = quit the program. Y on the pause screen = the Ozric
  tentacle on/off (T/G; state: screen draw routine $52FBE = $88BC tentacle / $2702 game; resuming from the
  tentacle restores the game screen by itself). Mouse cursor hidden (shown while the mouse moves, windowed). A hint line is drawn over the picture
  (SDL_RenderDebugText): pause help, quit confirmation. Key codes in the game are ASCII (p, o, x, h, s, t, g).
- 2026-10-22 owner: twin-stick worked with RT at the title, "otherwise it defaults to single stick" -> A at the
  title now starts twin-stick. Title detection (src\game.h): VBL hook $52FC2 == text + $551C.
- The log (default userdata\llamatron.log) has pads connected and "title screen" / "left the title
  (twin-stick|one stick)".

## Performance: the faster CPU during play (owner 2026-10-22: "levels 3, 7 and 8 have severe slowdown")
- How the game runs (profile, --profile): ALL game logic is in the 50 Hz VBL (in-game hook $1EDA via $52FC2:
  movement, bullets $11A0, enemies, joystick $23AA) - never misses a VBL even on wave 8. The main loop only draws
  (74% of the CPU: background restore $5C40-$5C80, sprites $76E0-$7720) and swaps screens at a VBL ($5BFE). On
  the 8 MHz ST it draws 16-25 frames/s, worst seconds lower: the "slowdown" is the picture updating rarely.
- So the CPU runs faster (--cpu=N, default 8) while the gameplay hook is installed (game::Playing): measured
  every second of play on waves 1/3/7/8 at 50 drawn frames/s from x4 up (x1: 16-25). Game speed unchanged
  (llama 1 px per VBL at x1/x2/x8). Cost: 2 minutes of play in ~3 s of one core.
- Only during play: the version screen and instruction scroller are timed by CPU speed (at x8 the title came at
  frame 156 instead of 2819); intros, wave transitions, pause run at x1 too.
- Timing: devices count ST cycles; the CPU runs speed_ cycles per ST cycle (Machine::Now, cpu_rem_), so the
  speed can change every frame. Log: every 10 s "drawn N frames/s while playing, worst second M".
- Test tools added: --cpu=N, --poke=N:<off>:<word> (wave: $4BFD2 = wave-1, poke every frame 3002-3040 after a
  j0fire start at 3000), --watch=<off>[,..], --profile=N:M:<file> (per 16-byte block, main vs interrupt; uses
  Musashi's instruction hook, compiled in).

## Game patches (src\game.cpp, applied after loading, original bytes checked)
- No screen flashes (owner 2026-10-22: remove the white flash on enemy kills; "jokes about it giving people
  epilepsy"). The VBL ($5398) plays a colour list (*$25EA, one entry per frame, 0 ends) into palette 0: $25E6 white
  1 frame (enemy kill at $1450; also $62A8), $167E white fade, $36E0/$49B4 white, $49E6 white fade strobing with
  blue, $4AF2 red/dark-blue strobe every frame, $25EE multicolour strobe, $1A86 rainbow. NOPping only the kill
  flash made the red strobe run longer (a kill used to cut it short), so the patch NOPs the palette write at
  $53DA instead: lists run with the same timing, the background stays black. A/B over 500 gameplay frames:
  identical except the 5 white frames. --flashes restores them. Left alone: colour 1 rainbow cycle in the VBL
  ($5402, smooth), the load indicators at $87B0.. (boot).

- Analog aim (owner 2026-10-22: "can we make this full analog?"; then: a toggle for purists, RB, off by
  default): RB at the title toggles it (hint line for 4 s when the title appears / 3 s after a toggle; RB isn't
  stick-2 fire at the title any more), saved in userdata\settings.txt (analog_aim=0|1); --aim=8|analog for one
  run (not saved). The hooks are always installed; off = they leave shots alone. Code hooks
  (Machine::AddHook: the instruction becomes ILLEGAL, C++ runs and does its work): $10A8 (shot created, a0 = slot)
  and $1198 (shot movement starts, VBL). Shots ($54FD4, 16-byte slots: x, y, dx, dy, sprite 29/30/31/32, $C reflective
  lifetime) move by integer dx/dy (-3/0/3); we keep an exact position/velocity per slot (any angle of the right
  stick, speed 3/max(|cos|,|sin|) = the original's at the 8 directions) and set dx/dy before each move. Shots
  moved by anything else are dropped from our table. Reflective shots (power-up, $1634; not "hot bullets") bounce by looking dx/dy/sprite up in
  the 8-direction table $2650 (an endless loop if nothing matches): then shots snap to the nearest of the 8.
  Tested: 20 deg shot, a 360 deg sweep with reflective shots forced on (no hang). Not changed: the multi-shot
  power-up's extra shots ($10C0, spread from the 8-way direction $2318).
  Test input: --press=N:aim<degrees>:frames (0 = right, 90 = down).

## Text in the program (for the curious; text offsets, the game's \c colour / 
 newline codes)
- $39CBA: an unused font test before the version screen text: Inspiral Carpets lyrics ("marys crying for her
  baby", "commercial rain"), "inspiral carpets" x5, the alphabet and a rude word.
- $3A356 credits: "perpetrated by yak, coded on devpac developer, samples grabbed with replay-8", the "oh f..!"
  sample by m c urtis, sween "e" todd and andy "rubber chicken" capon; "programmer entertained by" a list
  (Inspiral Carpets twice, Floyd, Ozric Tentacles, Grateful Dead, Lemmings, Thunderstrike PC VGA, Devil Crash on
  the PC Engine, Buckley's Best Bitter, Pensarnau pizza...). $3A5B8 story (2112, the Zzyaxians), $3A794 enemy
  and power-up guide (sheepipede, screaming mandy, false teeth, reflective shots, floyd bonus...).
- HISCORES.COW on the disk isn't what the game opens (scores.yak); default names include "cup _of tea" and
  messages "yak the hairy sez beat this!!!", "mint sauce is murder!", "keep welsh sheep radioactive!".

## Emulation notes (src\machine.cpp)
- Memory: 1 MB RAM, our TOS stubs at $FC0000 (traps = ILLEGAL opcodes handled in C, ACIA interrupt stub in
  hand-assembled 68000 that calls Kbdvbase()->ikbdsys while GPIP bit 4 is low, one ILLEGAL per vector for
  unhandled exceptions -> logged, program stops).
- Timing: 8.021247 MHz, 160256 cycles per 50 Hz frame (133604 at 60 Hz); CPU runs in slices up to the next
  timer A underflow / ACIA byte / VBL. Timer A delay mode only. IKBD bytes 10240 cycles apart.
- Frame: rendered at VBL from the screen address latched at the previous VBL and the current palette (the game
  doesn't change the palette mid-frame).
- YM2149 run up to the CPU time before every register write (digitized samples need it), box-filtered to 48 kHz,
  DC blocked. Levels from ayumi (MIT).
- Musashi gotcha: m68k_set_reg(SR) doesn't swap USP/SSP on a mode change - cpu::Set(SR) does it.
- Pacing (owner 2026-10-22: "run it at 60 but with the speed from 50?" - the game moves a fixed amount per frame
  and its own 60 Hz mode is just 20% faster, so: show 50 Hz evenly). Fullscreen picks the desktop resolution at
  the fastest refresh that is a whole multiple of 50 Hz (owner's 4K 120 Hz screen: 100 Hz; 60 Hz screens often
  have 50), and if the display's refresh is k x the game's rate, one game frame per k vsyncs (game runs at
  50.00 instead of 50.05). Otherwise (windowed at 120 Hz...) by the wall clock: uneven 2/3 refreshes per frame.
  Clock pacing presents only new frames and sleeps (SDL_DelayPrecise) until the next one is due, so a
  variable refresh display (G-Sync/FreeSync) refreshes at 50 Hz: smooth on 60 Hz-only VRR screens too
  (measured, windowed and fullscreen at 120 Hz: 50.05 presents/s, gaps 20.5-21.4 ms; VRR itself untested).
  Fixed 60 Hz without VRR or a 50 Hz mode: 5 frames in 6 refreshes, a hitch 10x a second (unavoidable).
  Audio: ~50 ms queued, SDL stream frequency ratio +-0.5% to follow. Log: "pacing: ..." and every 10 s
  "timing: game frames/s, presents/s, longest present gap" (measured: 50.00 / 100.00 / 10.4 ms).
