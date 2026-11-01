# Llamatron (Atari ST, Jeff Minter / Llamasoft 1991) - native PC port

Hobby project, started 2026-10-22 after the Galaga Legions DX port (C:\GalagaRecomp, same owner and toolchain).
Llamatron is shareware ("please spread this game everywhere", shareware screen at boot); the disk image is still
kept out of git (Private\). The owner downloaded the image from Jeff Minter's own site (it includes the cracked
Gridrunner: Minter distributes that copy himself). Release (owner 2026-10-23, from 1.2.0): the zip includes the
disk image (owner: not concerned given the games' age and provenance); the repo itself still has no game files.
GitHub: jedeyesniv-art/Llamatron-PC-Recomp (public), releases vX.Y.Z with the windows + symbols zips.
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
- src\launcher.cpp      the start screen (2026-10-22; carousel 2026-11-01): low-poly 3.5" disks (SDL_RenderGeometry, own
                        projection and flat shading) on a carousel - picked disk in the middle, neighbours shrinking
                        back to the sides, wrapping (owner's choice), "< n / N >" under the name. Labels: the owner's
                        hand-written photos (res\labels\*-photo.jpg, kept out of git: they show the room;
                        Tools\make_labels.py crops the drawn box, divides out the lighting, black ink on white ->
                        labels.png + src\labels.inc, run-length packed; no photo = an Ink Free stand-in, STAND_IN),
                        pools of light + a faint bloom on pure black. Y = how to play + the game's options (up/down
                        pick, left/right change: trainers, a user disk's program). LB/RB/mouse wheel turn too.
                        B twice quits; games return to it. Tests: --lpress=N:<pad button>, --lshot=N:<bmp>, --lframes=N.
- src\games.cpp         the registry (2026-11-01): the known games (id, caption, program, installer, save folder,
                        input mode, disk colours, help) found by an identifying file on any .msa/.st (or, Trip, in a
                        folder) under game\, the exe's folder, Private\ up the tree (3 levels deep); any OTHER disk
                        image with a TOS program on it (header $601A) becomes a user disk (owner: "add more disks as
                        I find them"): caption = file name, label = <image>.png next to it or a drawn one, colours
                        from the name, generic controls, option "program" (settings disk_<name>_program), saves in
                        userdata\disks\<name>. Saves: Llamatron/Gridrunner userdata\ itself, others userdata\<id>.
                        --program=X runs the known game with that program as from the launcher (--disk picks the image).
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
Tests: ALWAYS pass --userdata=logs\test-userdata (or similar) for scripted runs - otherwise they load and SAVE
the owner's high scores and settings in build\userdata (2026-10-22: Gridrunner tests wrote test entries into the
owner's HISCORES.COW; repaired by hand). --headless --frames=N --shot=N:<file.bmp> --wav=<file> --press=N:<what>[:frames], what = j0|j1 +
fire|up|down|left|right, an SDL key name, or pad<button> (pady, padback, padb: SDL gamepad button names). The exe is a GUI app: from PowerShell use Start-Process -Wait.
Scripted path: press j1fire at frame 150 (leaves the shareware screen), title is up by ~2900, j0fire there at 3000
starts a twin-stick game. Screenshots -> PNG with pillow to view them.
Crash: the log gets "CRASH: ... llamatron.exe+0x..." frames; llvm-symbolizer --obj=build\llamatron.exe
0x140000000+offset.

## More Llamasoft ST games (2026-10-31; owner downloaded them from Llamasoft's site into Private\)
- NEXT (owner 2026-11-01): the other ST disks, all booting from the launcher with pad controls. The plan, order
  and the owner's open decisions: PLAN-ST-GAMES.md - start there.
- Private\*.zip unpacked to Private\new\<zip name>\ (andesattack-st, defender2-st, hardcore-st, rmc-st:
  crack compilation disks; trip_a_t: loose files). --disk takes a folder of loose files too.
- Other programs use generic controls (input::Mode::kGeneric): keys to the ST, PC mouse passed through, pad left
  stick/d-pad + A = joystick 1, right stick = mouse (RT left, LT right button), Start = Space, Back = Esc, B
  twice = leave. Gridrunner's code (pokes its RAM) only runs for GRIDRUNR.PRG.
- Status: RMC (REVENGE.TOS) plays; Andes Attack (ANDES.BOY) reaches its "left button: start" screen; Hardcore
  (HARDCORE.PRG) title only; Defender 2 (DEFENDR2.PRG -> Pexec DEFENDR2.EXE) plays: menus are mouse-only (Space,
  click a game box, click ONE PLAYER; the triangle = sector code); in play keyboard Ctrl up / LShift down / # fire
  + Alt Tab RShift Caps, or mouse (left fire, right, Space Ctrl Alt) - meanings not mapped yet. Its IKBD handler
  takes $F8-$FB as a packet header even mid-packet: we send mouse deltas of at most +-4.
- Trip-A-Tron (third launcher disk, launcher::kTripATron): TRIP1.TOS is an installer writing TRIP.PRG etc. to a
  blank disk; main.cpp RunTrip finds the folder with TRIP1.TOS (game\, exe folder, Private\ up to 3 levels down),
  saves in userdata\tripatron, installs there on first use (unseen, CPU x8, stops when TRIP.PRG exists). Uses
  GEM (AES/VDI at startup), Line-A ($A000/3/6/C/D, drawn into the logical screen) and TOS's VBL queue. Label:
  stand-in in Ink Free (Tools/make_labels.py STAND_IN) until res\labels\tripatron-photo.jpg exists.
- Trip-A-Tron pad (owner 2026-10-31, src/tripatron.cpp, input::Mode::kTripATron): state $108B0 (0 performing,
  1 title, 2 Control Panel, 3 closing it); light source x/y $10692/$10694 (mouse handler $10752: clamps on the
  panel, wraps while performing). Performing: left stick relative (trip_stick_speed 5, _curve 2, _deadzone 0.15),
  right stick = spirograph (writes x/y: centre + stick * trip_spiro_size 0.9 of the half screen), RT hold / LT
  latch = left button, d-pad L/R = letters A-Z (all 26 live with the default KML), U/D = symmetry (cursor
  cluster Up Down Left Right Insert Home Help Undo, all 8 distinct: $102D4), LB/RB = keypad digits (palettes),
  A = random of the three, X Space, Y Esc, Start F10, Back = number keys (waveforms), B twice = launcher.
  Other states: sticks = mouse, A/RT left, X/LT right, Y Esc. Hint line at the top (HintAtTop). Pad polled with
  edges (test pad: gr<deg>@<mag> right stick, ga gb gx gy gstart gback gup gdown gleft gright, glt grt).
- Trip-A-Tron flashes (owner 2026-11-01: palettes 4, 5, 6 "particularly aggressive", tone down to pulses):
  choosing a palette plays a background (colour 0 only) flash list: 4 red/green alternating 0.7 s, 5 white
  flashes fading, 6 a magenta/blue strobe at ~8 Hz that never stops, 1 a short one. Machine::SetCalmBackground
  (on while performing; settings trip_calm_background=1): colour 0 shown through a 16-frame peak hold, capped
  at 40% per channel, smoothed (up 0.12, down 0.05 per frame) - flashes are ~1 s pulses, the strobe a steady
  dim glow; measured max change 5% per frame. Other colours (the cycling) untouched. --palette-log=N:M logs
  the colour registers per frame (+ "shown 0" while calm).
- Trip-A-Tron music (owner 2026-11-01: "react to music like Neon, but still be interactive"): src/music.cpp =
  our Neon port's analysis (C:\Xblades\rexglue\src\audio.cpp: miniaudio WASAPI loopback, 2048 FFT, 20 log bands
  each against its own falling peak, 0.12 s release) + bass/mid/treble averages + onsets (low-band rise vs its
  recent mean + 1.6 sd, >= 0.25 s apart). L3 / F12 cycles trip_music 0 off / 1 pulse / 2 full (saved). While
  performing: the source orbits a centre the left stick / mouse moves (radius bass, speed mids, treble shimmer,
  wraps), each beat holds the button 12-22 frames, the right stick still overrides; full adds palette every 16
  beats, effect at beat 8 of 32, symmetry on strong beats (>= 6 s apart), Space after 2 s of silence; none for
  15 s after the player chooses something. miniaudio in third_party/miniaudio (copied from Xblades, notices #5).
  Tests: --music=<wav> (decoded, fed at the game's pace, not played) --trip-music=N; log "music: ..." lines.
  Measured on a synthetic 120 bpm track: 10 beats per 5 s. Not yet tried by the owner on real music.
- Shared (2026-11-01): src/pad.cpp = pad 1 as a per-frame snapshot (real or the test pad from --press=N:g...)
  with edges; src/stgames.cpp = RMC and Andes controls (modes kRmc / kAndes). Registry play rule (games.h
  play_at / play_value / play_word / play_vbl): the faster CPU (--cpu, x8) while that RAM value says "play";
  --cpu-all=N forces a speed for a whole run (tests). Machine::TypeAhead = keys in the console buffer before the
  program asks (crack prompts). --palette-log=N:M. Tools: copy Tools\dis.py as d68.py elsewhere and run it on
  a --dump (runtime addresses).
- RMC 1 MB STE (owner 2026-11-01, Privateevenge_of_the_mutant_camels_1mb_ste_[no_publisher].zip ->
  Private
ewmc1mb\*.stx): preferred over the 512K one (the first of an id found wins, games.cpp). A Pasti
  .stx (disk.cpp StxToRaw; Tools\stx_to_st.py) with REVSTE1M.TOS + README.TXT (Yak's notes) in a folder
  (ReadFiles lists files in folders by name now). Needs STE DMA sound ($FF8900-$FF8921: control, frame
  start / end, counter, mode: rate + mono; mixed in Sync, MixDma) and the Microwire ($FF8922 / 4: the mask
  reads back; it waits for that), STE colours (4th bit, only for games marked ste in the registry). Same
  code, other addresses: hook $3DD3E, title $102BC, menus $1084A, play $111DE, pause $1056C, bonus $112A2,
  name $194A8; keys as the 512K one (check $1AEF4, flags $3DCB7). The camel animates every 1-2 frames at
  x8 (512K: every 8) - the owner's jerky camel. 15-19 drawn frames/s at x1, x8 -> 50. Docs: README.TXT
  from the disk in the launcher's viewer. stgames::SetProgram picks the addresses. Unhandled I/O reads and
  writes are logged once per address now ("I/O read / write of unhandled").
- RMC (REVENGE.TOS, D-Bug 28): VBL $1B4B6 calls the hook at $42E18: $1118E play (also the attract demo),
  $10526 pause, $10294 title, $107FA menus; colour-0 flash list *$4269C (written at $1B502). Keys via its own
  table ($42D10 -> flags $42D91, test $1B7AA): p pause, o resume ($10508 waits), x abort to the menu (play
  only; $41AC0 = 4), cheat keys l / n behind $11114. Joystick 1. Other hooks: $11252 end-of-wave bonus,
  $193F0 high score name (joystick letter grid): both VBL-timed (the name cursor ends on the same letter at x1
  and x8), boosted too (play_value2 / 3). --cpu-all-from=F: force --cpu-all only from frame F (tests).
  Smoothness (owner 2026-11-01: "feels like a lot of slowdown"): measured - logic every VBL (scroll $384A8 -1
  a frame), objects (table $384AC, $80 each: +6 x) move 0.4-0.75 px a frame in sub-pixel steps, 50 distinct
  frames a second, the owner's own log: 50 drawn / 100 presents a second, gaps <= 12.3 ms; the host runs it
  25x faster than real time. Scroll speed 1 << $407BC is fixed per level ($118E0 +1 is in the cheat code).
  So the pace is the game's own. --watch-step=N logs --watch every N frames. The owner's "jerky camel, smooth
  text": the camel's sprite (frame and position) changes only every 8th frame (6.25 Hz walk cycle, steps of
  2-4 px), while the scroll, stars and text move every frame - the game's animation rate; accepted (owner). Logic in the VBL, the main loop draws
  ($4505C hook, $113FE in play): at 8 MHz 13-19 drawn frames/s in play, x8 -> 50; scroll $384A8 identical
  x1 vs x8 (scores differ: random numbers and per-frame collisions). Pad: stick = joystick 1, A/X/RT/Start
  fire, Back pause, B paused = O then X (menu), B twice = disks; calm background in play.
- Andes Attack (ANDES.BOY, Medway 5): state $12EEE (1 play, 3 paused, 5 title/demo). Mouse (Initmous,
  $15384): y -> $2B682 (0x20..0xBE, down = bigger), left button held = thrust (speed $14BC4), right button =
  reverse (facing $1C088: 0 right). Keys: Left Shift fire (verified: laser), Left Control smart bomb
  (verified: a bomb icon crossed out), Right Shift pause, Space resume; Kbshift + Crawio. Shields / warp
  keys NOT found (Alt, letters, digits, F keys tried; noisy). Text $2B494 "CONTROLS: Mouse = Altitude, Left
  Mouse = Thrust, Right Mouse = Reverse, Left SHIFT = FIRE". Crack screen (right button = trainer, $1041A)
  ends by itself; clicks at frame 100 changed nothing - trainer not offered yet. 13-19 drawn frames/s in play,
  x8 -> 50 (thrust / altitude identical x1 vs x8). Its sky is a timer B raster: no calm background (it
  changes colour 0 per line). Pad: see stgames.h. States after play: 4 game over, 5 high score table; a name
  is typed there (Crawio $1A38C: letters, Space = "_", 3 of them, count $1BC96, cursor $1BC92 >= 0 while
  entering), then back to the title. Pad: up / down picks a letter (shown on the hint line), A types it.
- Defender 2: boots from the launcher (the crack's "T for trainer" prompt answered by TypeAhead from the
  options INVINCIBLE / FRIENDLY SPRITES: "" or "t" + y/n + y/n). VBL $4AF1A dispatches on $4B086 (table
  $212A2): 0 play, 2 title, 8 player select. Pointer x/y $62456/$62458 (writable; $294FC a drawn copy).
  Player select: click the figure (75,60) for ONE PLAYER (the words don't work). Play draws 25 frames/s at x1
  (its design). 50 fps (2026-11-01): the main loop $1B2D8 waits for the VBL countdown $62454 (reloaded from
  the divisor $4D1EC: 2, set at $109BC) and the game logic is in the VBL ($1BCEA), so stgames.cpp writes
  $4D1EC = 1 in play (2 elsewhere; setting d2_50fps, default 1) with the faster CPU in play (play rule
  $4B086 = 0 and VBL $4AF1A): 50 drawn frames/s, world position identical frame for frame vs the original. Variant $1F7F8: 0 Defender, 1
  Stargate, 2 Defender II (title boxes (75,143) (243,143) (160,149); Trainer (22,190), Options (160,193),
  History (298,190)); the title needs Space before a box click works. Control scheme $1BCC4 through the
  table $1B8BC: 0 mouse ($1B8C8 + $1B400: pointer y = altitude, x = speed / direction, left fire, right
  $1BCCC), 1 keyboard ($1B520), 2 attract demo; the default is the mouse. Keyboard scheme (verified): Control
  up / Left Shift down (ship y $21266, 3 px a frame), Return thrust (velocity $1B4B4, world x $2BA2E), Alt
  reverse (facing $1B4B8: -1 left), # fire (lasers), Right Shift smart bomb (count $2BA44; a white / black
  strobe on colour 0 every frame - NOT calmed: the rasters change colour 0 between lines, the per-frame
  filter breaks the picture; needs a patch of the game's flash), Caps Lock inviso (Stargate), Tab ship type
  (Defender II); no pause key, no hyperspace found. Controls are skipped while $2F2DE (death countdown, 300
  frames) runs. Pad (stgames.cpp, verified with the test pad): title d-pad + A (Space, then a click 50
  frames later), player select d-pad + A (figures (75,60) / (190,60)); play: the keyboard scheme is forced,
  stick up / down pulsed for analog altitude, left / right thrust (reverse first), A fire, Y bomb, X reverse,
  LT thrust, LB inviso, RB Tab; Back pauses the emulation (stgames::HostPaused); B twice = disks.
  State 8 is several screens, told apart by the sub-handler $1F200: $1157E player select, $1134C high score
  initials (letter = pointer x / 8; left button fixes, right deletes; then table (6), title). Pad there:
  d-pad steps the pointer 8 px (a letter), A fixes, X deletes. With no high score: game over -> 6 -> 2 alone.
- Hardcore (HARDCORE.PRG, D-Bug 100; Yak 1992, an unfinished "5-level evaluation demo"): plays. VBL $168F2
  calls the hook at $338BA: $104EA title (a click starts), $145D0 level intro, $10C82 play, $11242 paused;
  colour 0 flash list *$33136. Own IKBD handler $16D56 (keys: table $337B2 -> flags $33833, test $16ECE;
  mouse packets -> $16CC6): pointer x $338C2 (21..257), y $338C4 (22..178, mouse up = bigger = up on
  screen) places the blasters on the four edges; left button (bit 1 of $338C6) fires; no right button. Keys
  in play ($10CB4), from Yak's notes: p pause ($1938C = 3; $11216 waits for o), h 50/60 Hz, s strobes
  ($10D66: 0 = on; we set 1 at the first play frame, setting hc_strobes=1 keeps them; 0 flash frames then),
  i puck intelligence ($17520), n next level ($1938C = 1). The guns fire by themselves; the left button =
  side lasers on all four tracks. The puck ricochets; power-ups come from sheep (notes list them). 15-20 drawn frames/s at 8 MHz in play, x8 -> 50; same score and end
  screen at x1 and x8 (RAM differs only in drawing). Pad (stgames.cpp): left stick moves the pointer (6 px a
  frame, curve 2), right stick = the pointer at that spot, A / RT side lasers, Y puck intelligence, Back
  pause, B twice = disks. (Machine::SetCalmBackground has hold / cap / release per game, stgames::CalmStyle;
  Hardcore no longer uses it: its own strobe switch is better.)
- Docs (2026-11-01): Game::docs_program (a reader on the disk: DOX.TOS for the 512K RMC, READ_ME.PP for
  Defender 2 - The Alien's displayer, the crack's notes and the mouse controls -, DOC100E.PRG + typed "b"
  for Hardcore) runs in input mode kDocs (stgames: up / down = cursor keys repeating, LB / RB = Shift +
  cursor, A Return, Y Help, B = back to the disks); Game::docs_text (Trip's README.DOC in its save folder,
  the STE RMC's README.TXT on its disk) is shown by the launcher (ShowText: up / down, LB / RB, B). The
  how-to-play page shows "X READ THE DOCS" (X = the docs there, CRT elsewhere). Andes: no docs on the disk.
- Console (2026-11-01, plan A3): Machine::ConsoleOut = TOS's VT52 console (Cconws, Cconout, Crawio
  write, Bconout 2 / 5, Cconin echo) drawn into the logical screen, low resolution 40 x 25, glyphs from
  SDL's debug font header (third_party/SDL/src/render/SDL_render_debug_font.h, public domain VGA font; bit 0 =
  left; c - 33 for 33..126; ST codes >= 128 blank). ESC A B C D E H I J K L M Y b c d j k l o p q v w.
  Boot palette = TOS's low res one (777 ... 000; text colour 15). Shown now: Defender 2's crack prompt,
  Trip-A-Tron's installer, D-Bug 100's doc menu (DOC100E.PRG). Andes' crack screen: its own drawing, white.
- Medium resolution (2026-11-01): $FF8260 / Setscreen rez 1 -> the frame is 640 x 200 from 2 planes
  (Machine::FrameWidth; taken at line 0), shown through a second 640-wide texture (Context::texture_wide)
  in the same picture rectangle; Get/SetPixel and the console (80 columns, colour & 3) follow; Setscreen to
  medium sets colour 3 black (TOS's text). The D-Bug document displayers work: DOX.TOS (D-Bug 28: Yak's
  Revenge notes) and DOC100E.PRG (D-Bug 100: B = Yak's Hardcore notes for testers: controls, power-ups, keys).
- TOS added for them (machine.cpp): Pexec 0/3/4/5/6 + Pterm back to the parent, Malloc/Mfree/Mshrink,
  Fsfirst/Fsnext, Fsetdta/Fgetdta, Fdelete, Dfree, Cconin/Bconin wait for a key (trap re-run), GEM trap #2,
  Line-A, a ROM VBL routine (frclock, colorptr, screenpt, vblqueue at $1C00).
- Tools gotcha: Tools\dis.py shadows Python's dis module for scripts in Tools\ (numpy/capstone import inspect):
  run them with python -P (make_labels.py), or copy dis.py elsewhere under another name.

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
  (written straight into $54DD0/$54DD2), d-pad slowly; RT/A = left button (fire/click), LT/X = right button
  (nosecone: held = attached); right stick flicked up/down = one left Shift/Alt tap (one step towards up/down;
  owner: "like a button", no repeat while held; next flick after it returns below 30%); RB/LB = one step up/down
  per press; Y = Control (2026-10-23; was RB). X/Y/LB/RB only in the game, not on the title (menu shortcuts there)
  or the shortcut screens; Back/Start
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
- Menus on the pad (owner 2026-10-22), src\gridrunner.cpp: virtual clicks (pointer to the target, optional
  modifier held, left button 4 frames). Title (state 5; clicks only accepted ~800 frames after it appears): A
  play, X = left Shift + click -> GAME OPTIONS (state 10: pause mode demo/freeze, reset power-up message, quit
  rows y 84/104/134/186), Y = Alt + click -> GAME SECRETS (state 7), LB = Control + click -> ADDITIONAL
  INFORMATION (the "may the fleas of 1000 camels infest..." curse on pirates - in a cracked copy). Level
  select (6): d-pad left/right = [-]/[+] at (115,120)/(200,120), A = LET'S BLAST!! (157,172) (x 101-214, y
  162-182); level variable $48BCE. Name entry (9): keyboard cells a-z ! . ? - / DEL OK ($1096E; ^ = delete,
  _ = done), the pointer jumps cell to cell on the d-pad (starts on "a"), A picks, B = DEL, Start = OK; typed
  letters / Backspace / Return click the cells; clicks wait for the letter animation ($10966/$105BC). Options:
  d-pad rows, A select, B = the quit row. Hints per screen. On these screens the d-pad and A are shortcuts
  (RT still clicks at the pointer).
- Entry lane (owner 2026-10-22: "can see a few pixels of [snakes] on the top rows ... but can't shoot them";
  chose "invisible until vulnerable", on by default, --entry-lane = the original look). Snakes (objects: table
  $46770, $B4 bytes x 15; +2 x, +4 y, +6 type: 2 head / 3 body, +$20 shootable, +$2E hits the ship) come in
  along y = 5 under the top rail; shots only hit at shot y >= 20 ($1345E: a deliberate safe zone); the sprites'
  lowest row (y + 6) shows at lines 10-11 below the rail (rail lines 3-9, grid from 10). Hook on the snake draw
  routine $1655E ("cmpi.w #$1D,6(a3); beq rts $12C3C"): it draws a WHOLE snake - called with the tail segment
  (+8 = $FFFF), it follows the links +$E to the head - so the hook walks the chain: hidden only while every
  segment has y < 14 and none is due. (The first version looked only at the tail: a snake coming down stayed
  invisible until its tail left the lane, head already shootable - the owner's "surprise attacks".)
  "Visible 1.5 s before vulnerable" (owner): look-ahead - every 15 frames in play with something in the lane, a
  copy of the machine (Machine is copyable; cpu::SaveContext/RestoreContext/SetMachine; sandbox = no picture,
  sound, files; CPU x1: the logic is in the VBL) runs 90 frames with the controls held; each object is drawn
  from 75 frames before it leaves the lane (g_reveal_at). Measured: head predicted at frame 3472 from every
  look-ahead (3390+82, 3405+67...), shown from 3405 (orig: visible, old: never), whole snake visible coming down.
  Same input with / without: RAM differs only in drawing (restore lists, lane pixels, stack). Rejected (owner agreed): hittable in the lane (breaks the safe zone / balance), a taller
  playfield (frame, rails, limits, HUD all move). Tool: grid_objects.py <dump> lists the objects.
- High scores: HISCORES.COW, 40 x (8-digit score + 3 letters) + 3 messages; byte $217 = checksum: $27 + the
  sum of bytes 0..$216 ($193AC); a bad checksum = the file is ignored silently (the built-in table $49520 is
  used, which lacks the disk's "jon 55323"). Never hand-edit without fixing the checksum.
- Test inputs: --press=N:mleft|mright[:frames], mx<dx>, my<dy> (per frame); pad: gl<deg>@<deflection>,
  gry<value>, glt, grt, gx, gy, glb, grb; pad buttons as for Llamatron (padback, padb...). Flow: mleft at 600 (Llamasoft
  core screen), 1500 (title), 2000 (LET'S BLAST), 2400 (intro), then play (left held = fire).
  --watch takes text offsets here too: runtime address - $10100 (nosecone direction $49130 -> 39030).

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

## CRT look (src/crt.cpp, src/shaders/crt.frag.hlsl; 2026-10-22, owner's request)
Our own one-pass shader tuned to the owner's ShaderGlass profile (crt-yah), then owner-adjusted (crt_defaults=2: no
curvature - the picture keeps its size, no vignette - it made a grey glare bubble, true blacks + S-curve contrast,
halation 90% less): soft horizontal beam, scanlines
thickening with brightness, halation, aperture grille + bleed, curvature, vignette, warm colour. settings.txt crt=1
and crt_* (reread live). Toggle: X / C / F9 on the launcher, F9 in a game. Needs SDL's GPU renderer (D3D12; we ask
for it with DXIL shaders, fallback = default renderer without CRT). Tools/build_shaders.py -> crt.dxil.h
(committed; the Windows SDK dxc has no SPIR-V). LLAMATRON_RENDERER=direct3d11 picks another renderer for tests.
Lessons: SDL_shadercross names every vertex output TEXCOORDn, so a custom pixel shader's inputs must be
TEXCOORD0 (colour) / TEXCOORD1 (uv), else D3D12 "Could not create graphics pipeline state" (logged only via
SDL_GetError, picture black). In-game --rshot=N:<bmp> reads the screen back (CRT included).
