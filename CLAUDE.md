# Llamatron (Atari ST, Jeff Minter / Llamasoft 1991) - native PC port

Hobby project, started 2026-10-22 after the Galaga Legions DX port (C:\GalagaRecomp, same owner and toolchain).
Llamatron is shareware ("please spread this game everywhere", shareware screen at boot); the disk image is still
kept out of git (Private\).
Goal: a native Windows app (SDL3) that plays well on an Xbox pad, twin-stick style. Owner's focus: controls.

## Status
- 2026-10-22, stage 1 DONE: the original 68000 code runs on the Musashi interpreter inside a native SDL3 app with
  the ST hardware and TOS emulated at a high level (src\machine.cpp). Boots, shareware screen, version screen,
  instruction scroller, title, twin-stick game verified with scripted input (screenshots); audio produced (WAV
  dump). Not yet tested by the owner with a real pad.
- Stage 2 TODO: static recompilation of the 68000 code to C++ (Tools\ to write), with the interpreter kept as the
  reference (lockstep compare) and as fallback for code it can't place (self-modifying code, see below).
- Not yet looked at: GRIDRUNR.PRG (the bonus game on the disk, "Pompey Packer 2.5" packed).

## Layout (C:\LlamatronRecomp)
- Private\              (git-ignored) Llamatron ST.msa (owner's copy), disk\ = its files, tron.bin/.reloc =
                        the unpacked game (Tools\depack.py), for the analysis tools.
- src\                  the app: main.cpp (window, pacing, options), machine.cpp (ST + TOS), cpu.h (CPU interface)
                        + cpu_musashi.cpp, ym2149.cpp, input.cpp (pad/keyboard mapping), disk.cpp (MSA/FAT/depack),
                        crash.cpp (unhandled exception -> stack in the log), musashi\m68kconf.h (our Musashi config,
                        copied over the submodule's at build time).
- third_party\          submodules: musashi (kstenerud/Musashi, MIT-style), SDL (3.4.16, static).
- Tools\                st_disk.py (msa -> files), depack.py (TRON1MEG.PRG -> tron.bin + tron.reloc),
                        dis.py (capstone disassembly of tron.bin: dis.py <hex addr> [count]), xref.py (absolute
                        operand references: xref.py <hex>[,<hex>]). pip: capstone, pillow.
- build.bat             configure (once) + build in vcvars64 with clang/Ninja -> build\llamatron.exe (+ PDB).
                        Run it from PowerShell (cmd /c build.bat); from Git Bash "cmd //c" silently didn't build.

## Running and testing
build\llamatron.exe finds the disk image itself (game\ next to the exe, the exe's folder, nearest Private\ up).
Saves (hiscores: SCORES.YAK) go to userdata\ next to the exe; files there replace the disk's.
Options: --disk=<.msa|.st> --log=<file> (default userdata\llamatron.log) --trace-tos --window (fullscreen is the
default; F11 / Alt+Enter toggle) --refresh=desktop (don't change the display mode)
Tests: --headless --frames=N --shot=N:<file.bmp> --wav=<file> --press=N:<what>[:frames], what = j0|j1 +
fire|up|down|left|right, or an SDL key name. The exe is a GUI app: from PowerShell use Start-Process -Wait.
Scripted path: press j1fire at frame 150 (leaves the shareware screen), title is up by ~2900, j0fire there at 3000
starts a twin-stick game. Screenshots -> PNG with pillow to view them.
Crash: the log gets "CRASH: ... llamatron.exe+0x..." frames; llvm-symbolizer --obj=build\llamatron.exe
0x140000000+offset.

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
- 2026-10-22 owner: twin-stick worked with RT at the title, "otherwise it defaults to single stick" -> A at the
  title now starts twin-stick. Title detection (src\game.h): VBL hook $52FC2 == text + $551C.
- The log (default userdata\llamatron.log) has pads connected and "title screen" / "left the title
  (twin-stick|one stick)".

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
  Audio: ~50 ms queued, SDL stream frequency ratio +-0.5% to follow. Log: "pacing: ..." and every 10 s
  "timing: game frames/s, presents/s, longest present gap" (measured: 50.00 / 100.00 / 10.4 ms).
