# Llamasoft on the ST (was "Llamatron PC" until 1.2.0) - Jeff Minter's Atari ST games, native on PC
Title from 1.3.0 (owner 2026-11-01); the exe is still llamatron.exe, the repo Llamatron-PC-Recomp. Labels (2026-11-07): the
owner redrew every label digitally (res/labels/sheet-1..3.png, committed: no room in them); make_labels.py SHEETS
cuts each cell, border included, scaled evenly onto white (the bottom row's cells are taller: narrower boxes).

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
- NEXT MAJOR STEP (owner 2026-11-01, after bug fixes from the owner's testing): PLAN-NEXT-LEVEL.md - run-ahead
  (lower input lag, all games), then a native remaster renderer with Photon Storm as the pilot.

## Remaster (2026-11-02, branch remaster; PLAN-NEXT-LEVEL.md step 2, pilot Photon Storm)
- Porting the next game: REMASTER.md (recipe, verification, pitfalls, per-game notes). Owner: run-ahead on
  by default in every port; no public push until all the ports are done.
- src/remaster.cpp: generic. The game runs unchanged, but in play its draw calls for moving things are caught by
  hooks and NOT drawn into the ST screen: remaster::Add(screen, item) per object (key = caller + object pointer +
  occurrence). After each RunFrame, FrameShown(Machine::FrameBase()) picks the list drawn into the screen shown;
  Draw composes at 4x (1280 x 800 target): the ST frame, then the items lerped between the previous and this frame
  (alpha from the clock / vsync phase; > 40 px = a jump, not slid), the ship rotated. Then crt::DrawPicture with
  src 320 x 200 (the shader's supersampled mode: colour.w = 1, continuous taps, lines averaged). Presents every
  vsync while active (main.cpp pacing). Interpolation = 1 frame (20 ms) of extra delay (run-ahead would offset).
- Sprite images: the game's own routine run in a sandbox copy of the machine between frames
  (remaster::DrawInSandbox: cleared screen, ILLEGAL return address past it); a sprite seen for the first time is kept too and its image
  decoded right after that frame (main: remaster::DrawQueued, 2026-11-07; before: the game drew it once), then
  cached (~0.5 ms each). Colour indices; a texture is rebuilt only if a colour it uses changes.
- Photon Storm capture (stgames.cpp, "Photon Storm: the remaster"): $4620C draw call (BeginFrame), sprite routine
  $44BA8 (sandbox needs $46420 = 0, clips $57FC0/2/6/8, colour $57FD0; types 0/2/5 snap x even: Item::snap,
  we draw at the exact x), plotters $440BA/$44150/$442F2, ship $15C72 (frames decoded from RAM). Not captured:
  $4630A set (all screens) and the HUD painter $14F3E..$14F7A. Paused: the last frame held.
- Setting remaster (1), F8 in a game. Headless runs don't capture unless --remaster-capture. Tests:
  --mshot=N:<bmp> = the composed frame in software (whole pixels, snap, no rotation: compare with --shot of a run
  without capture, ps_analog=0), --remaster-alpha=F, log "remaster: N items a frame, % slid / jumped / new"
  every 500 frames (~96% slid). 2026-11-06: captured on the attract screens too (states 12 ARC logo, 7 title,
  11 enemy guide, 8 high scores: PsDrawsEveryVbl), divisor 1 + x8 there (games.cpp play_more; their animation
  is in the VBL), hooks from the ARC logo on; dot entry $442EA hooked; title stars behind the text ($441EA ->
  Item::behind: Draw puts colour 0, those items, then the picture with colour 0 see-through). The original's
  behind plotter rewrites whole background words, erasing other stars in the word (flicker): ours shows them all.
  Bonus stage (2026-11-07, owner): state 3 (the quasi-3D tunnel of particles; entered at $18022 when the warp
  $1B102 is open and the ship reaches the world's middle $1D134/$1D136 = $800; VBL handler $1BA22, frame routine
  $1C060) and 5 (after it, ~290 frames; frame routine $1BE92: the same particles $1BD14, sprites via $44C42) drawn
  every VBL on the faster CPU (PsDrawsEveryVbl, play_more); both fully captured (the raw ST picture lacks the
  particles and ship), pixel-identical, same lengths (720 / 290 frames). The bonus won (its script's 'e', $1C634):
  4 (score counts up, ~275 frames) then 6 (WARP: 3D streaks, $1C866 / $1C9F4, ~405) - captured the same way,
  verified (--poke=2100:@44846:4 mid-bonus); owner tested in real play 2026-11-07: smooth. Divide by zero (Deck run 2026-11-07: the game quit after the bonus): the projection $43F8E (x + x*z / (D - z), D = $43F8C) divides by zero when a WARP streak's z equals D (D rises to +520 in the WARP's second half) - the original's own bug (no handler: bombs on an ST), random, ~1 warp in 10. Hook at its sub.w d5,d6 ($43FA2, stgames.cpp kPsProjectSub): 0 -> 1. Repro: the bonus pokes + --poke=2100:@44846:4 --poke=2450:@2B0E0:FA --poke=2450:@1C83C:0 --poke=2450:@43F8C:64 --poke=2450:@2B52A:172. Every state now: 0 boot, 2 change-overs and 9 (high-score initials,
  an input screen) are the only ones not drawn every VBL / captured. Test: --poke takes
  @<hex> absolute addresses now: --poke=1900:@1B102:1 --poke=1900:@1D134:800 --poke=1900:@1D136:800 in play.
- Gotcha: CLAUDE.md has raw CR characters inside lines (paths like Private\r...): edit it byte-safely (Python
  open(..., 'rb')), never through text mode.
- Andes Attack remaster (2026-11-02): src/remaster_andes.cpp (sprite routine $21D6C, draw pass $18DD2..$18E78,
  world -> screen $1B50A for matching the landscape tiles). 2026-11-06: captured on every screen (passes 0/2/4/5/6, end
  at $15490) and x8 in states 5 / 4 too: title / high scores / game over 50 a second, pixel-identical. remaster.cpp matches items to the nearest one of
  the frame before within a key group (Item::world: in world coordinates). Owner on PS: run-ahead 2 is the
  sweet spot, 3 "indefinably odd". Pixel checks need --remaster-ahead=0.
- Llamatron remaster (2026-11-02): src/remaster_llama.cpp (sprite routine $5E8A, frame $5C10 / $5C12; text
  offsets). Its state depends on CPU speed (the original alone diverges at x4 / x16), so pixel checks run in
  windows before the first death or wave change (REMASTER.md "Verification").
  2026-11-06: also the title (hook $551C, draw $5668: the parade, the sheep; on the faster CPU too, main.cpp
  game::AtTitle - VBL-timed, at x1 a busy pass took 3 VBLs) and the continue screen (hook $C94, draw $D5A: the
  last play pass's items added under it each frame - the game leaves that picture in the screens). The wave change zoom ($AEC, 2026-11-07):
  its dots ($5B08 hook) go to remaster::Reveal (old colour, Machine::FramePhase): between frames the picture shows
  only what was drawn by then, so it builds up at the display's rate (game unchanged; cpu::UseCycles keeps the
  replaced movem's 36 cycles: 134 frames as before). The version screen / scroller ($7E6E, 2026-11-07: a line a drawn frame, 2 VBLs; draw fn
  $7F20 = lines 2..197 x 16..303, $7F52 = all): remaster::SetScroll slides that rectangle the rest of a line
  between steps (step = a new screen shown; pace averaged) - smooth, a step shown up to 2 frames later.
  remaster.cpp, run-ahead 0: each screen's last complete pass (Capture::done), not the one being built.
  Audit 2026-11-07 (every value stored to the VBL hook $52FC2 / draw fn $52FBE): also the pause screen's Ozric
  tentacle (hook $1D32 + draw $88BC: captured, faster CPU there - its motion is in the pause VBL - so it draws every
  VBL; was the ST picture at x1); game over's 10-frame hand-over ($CEC typing VBL under draw $D5A, no passes) gets
  the last play items under the ST picture (was an empty arena: the capture had kept them out of the screens);
  sprites are captured only inside a draw pass (g_in_pass: GAME OVER is drawn by the continue VBL as it hands
  over). Left: the high score entry ($8272 / $82C2, "enter something wonderful"), the scrollers (slid already).
- Andes Attack audit (2026-11-07): passes ($202BA, table $20296) 0 play, 2, 4, 5, 6 captured; 3 = level set-up
  (hands back to pass 0 the same frame: its ATTACK LEVEL intro is pass 0's, captured, pixel-identical); 7 the
  change-over, 8 the pause's set-up; 1 never set. The pause (state 3) was held - the play picture's ship,
  terrain and llama stayed over GAME PAUSED (owner): now captured like the rest (pass 8 draws the background and
  text, then pass 4 its arc of dots: captured, sliding), faster CPU there (play_more {3}); pixel-identical.
- Gridrunner remaster (2026-11-02): src/remaster_grid.cpp (core $4BCE4, pass $144DA..$145E2). Items now take
  each line's palette (Machine::LinePalettes: rasters), all games.
  Its image id includes the colour word $58054 (level 4's text came out dark: the owner's report).
  2026-11-06: also the wave intro (state 3, pass $18B5A..$18B92: the flying letters; its star burst is a palette
  cycle) and game over (8, frame routine $4CE30..$4CE4A); grid::DrawsEveryVbl = play / intro / game over at
  divisor 1, grid::FastCpu = play / intro (game over's click wait is a CPU loop). Not the title and menus (their
  timeout counts drawn frames). The ship-lost screen (4: a still picture, raster bands rolling up a line a frame
  through a gradient; 2026-11-07, owner: "at 50") is crossfaded between frames (remaster::SetCrossfade, generic:
  the previous frame over this one at 1 - alpha; checked: alpha 0.5 = the 50/50 blend), so it presents at the
  display's rate. Intro ~0.4 s shorter (its end waits
  for the letters, which line up with drawn frames); pixel-identical with capture. Next session:
  REMASTER.md "Where we are" (all ports done; its "Polish to-do" list is the plan; no push until the owner says).
- Camels remaster (2026-11-03): src/remaster_rmc.cpp, STE + TT builds (TT addresses via Tools/mapaddr.py; the 512K
  build not covered). Captures sprites, stars, falling blocks, ground stripes, buildings, the level message;
  not in pause. Generic additions: remaster::kBlack (opaque black pixels), Item::fine_x/fine_y/period,
  SetShownColour0 (calm background). 120 presents/s in play.
  2026-11-06: also the two menus (hooks $1084A / $10C4A, draws $10EE8 / $10E1E: only the starfield moves -
  hooked, each star a dot sliding by its countdown) and the end-of-wave bonus ($112A2 / $115AA); faster CPU on
  the menus (games.cpp play_more; the change-overs $1AC3C are CPU-timed). TT: $1085E / $10C5E, $10EFC / $10E32,
  $112CA / $115D2. Pixel-identical but for a TT bonus frame where a stretched (type 1) streak is clipped at
  line 16 (the sandbox image sliced vs the routine's own clipping).
  2026-11-07 (owner, Deck: still 50 a second between levels): the warp (exit code $3C3AE / STE $3C01C = 3: hook
  $1102C / draw $110D8, "warp to next oasis!") and the restart oasis after it and after every 5th level's bonus
  (hooks $122D4 $12350 $1238A (the password: fire or 500 frames) $123BE $12402, draw $12414: the camel walks in /
  off) captured, faster CPU; STE $11018 / $110C4, $122AC $12328 $12362 $12396 $123DA / $123EC. Pixel-identical
  both builds. Test: --poke=2600:@3C3AE:3 in play (TT).
- Sprite textures capped (2026-11-07, the owner's Deck: Camels TT crashed in d3d12core.dll = vkd3d-proton, in CopyDescriptorsSimple from our glow pass's flush, ~80 s into play): remaster.cpp never freed its baked / layer textures (TT ~1850 made in 80 s; and every earlier game's stayed). Now FreeTextures at each game's start and a soft cap (kMaxLive 1024: the least recently drawn, idle >= 1000 draw frames, go; TT settles ~1500 live). Not reproduced here: vkd3d-proton 3.1 on this PC (DLLs next to a copy of the exe, logsk) ran TT 4 min without the cap, 4327 textures, no crash - the Deck's AMD driver differs. If it crashes again, the cap wasn't it. Measured SDL's GPU descriptor heap use: ~257 of 2048 a command buffer (not an overflow).
- Defender 2 remaster (2026-11-03): src/remaster_d2.cpp - many small draw routines hooked (sprites, plotters,
  OR-ed stars / particles / lasers via remaster::IndexAt, ground bands, terrain, planet); HUD and radar left.
  --remaster-writers=N logs what still writes into the screen (any game).
  2026-11-06: also the title ($1FA38: the three preview boxes), player select ($117F0) and the high scores
  ($11C70) - frame routines of states 2 / 8, remaster_d2::DrawsEveryVbl: divisor 1, faster CPU (stgames::FastCpu
  next to the registry rule), captured. Not the version screen ($1EB24) nor the logo ($1E92A: its lightning
  grows per drawn frame). The high scores' side pillars are copied inline ($11C90): not slid, but now 1 px a
  frame (were 2 every other).
  Audit 2026-11-07 (every frame routine stored to $4CC46, every state of $4B086): also the end-of-wave screen
  (state 4, frame $12848: ATTACK LEVEL n COMPLETED, bonus humanoids, a wireframe pyramid) and the fade when the
  ship is lost (state 5 with play's frame) - Attract(): every VBL, faster CPU, captured. Both are partly timed by
  their drawing: they end 4 / 3 frames (of 340 / 38) sooner than the original, capture on or off alike (captured
  at the original pace their length changed with the capture instead). Known: the pyramid's lines cross the
  humanoids in the original, pass behind them here (~10 pixels). Owner tested in real play: smooth. Left: state 3 (frame $12C9C, not reached
  scripted), the logo (owner).
- Hardcore remaster (2026-11-03): the Camels port with Hardcore addresses (remaster_rmc.cpp kHc) + compiled
  sprites (type 6) and one-plane plotters. Trail rings $1A1F8/$1A4F8 advance per drawn frame (cosmetic).
- Run-ahead (2026-11-02; setting remaster_ahead 0..3, default 2; F7 cycles, a message line for 2.5 s):
  0 = the list in the screen shown; 1 = the newest complete draw pass (hook $4620E -> remaster::EndFrame; the ST
  shows it a VBL later); 2 / 3 = a copy of the machine (sandbox + Machine::ahead: hooks still capture, into
  remaster's g_fork) runs 1 / 2 frames with the controls held after every real frame, its newest complete list
  shown. stgames saves / restores the hooks' C++ state around it (heading, drift, shots, capture flags:
  remaster::SetAheadState). Measured (--remaster-probe: the ship's angle per frame shown; stick turned at frame
  2200): 0 -> seen at 2203, 1 -> 2201, 2 -> 2201 a frame further on, 3 -> two further. 2 = 60 ms quicker than
  0, ~40 ms quicker than the original game (cancels the interpolation's frame). Cost ~0.3 ms per frame run
  ahead. Game RAM identical with run-ahead 3 + ps_analog 1 (7000 frames). --remaster-ahead=N for a run.

## Controls audit (2026-11-06; owner: "we can't be missing any controls")
- Every game's input reads listed from RAM dumps (TOS Kbshift / Crawio calls, own key tables) against the pad.
  Mapped now: Photon Storm play loop Kbshift ($17BF4, copy $27C50): bit 2 Control = smart bomb (count $1D126,
  one at a time $183B8; colour 0 pulses blue ~4 frames: no calming needed) -> RT; bit 1 Left Shift held = boost
  (move doubled while the gauge $1D12C 0..19 lasts: -1 per 30 frames, +1 per 120) -> LT; bit 3 Alternate held
  while firing = single shots instead of the three-way spread ($1845E) -> LB / RB. RT / LT no longer fire /
  thrust there (A / right stick fire, X / far push thrust). Verified scripted: bombs 3 -> 2 -> 1, gauge 19 -> 10.
  Camels two-player (team mode $3933C = 2): player 2 = the goat on joystick 0 ($13804) -> pad 2
  (input::SecondPadJoystick, as Llamatron's TEAM). Untested with a real second pad.
- Complete already: Andes (Kbshift bits 0-3 all mapped), Defender 2 (its 8 keys via table $4B300: Tab / Insert,
  Return, Ctrl, LShift, #, RShift, Alt, Caps - Caps in Defender II = an energy weapon $138A2, on LB), Gridrunner.
- Left unmapped on purpose (cheats / switches): Llamatron pause h (50/60) s (samples); Hardcore s h n (next
  level, a tester key); Camels l n (cheats); typed codes: Photon title "your-mind-is-my-ashtray" ($27316),
  Andes title "delicate-sound-of-thunder" ($137D8), Gridrunner pause "pink-floyd-are-gods"; letters = sounds in
  Andes / Gridrunner. Keyboard still sends them all.

## Arcade controls for Andes Attack and Defender 2 (2026-11-04; owner tried it: "thrust on RT feels very good")
- After the arcade Defender (stick up/down only, thrust a button): settings andes_controls / d2_controls (1 =
  ARCADE, default; 0 = STICK THRUST, the older scheme below). Stick left / right only turns round (SidePush in
  stgames.cpp: 60% out and within ~35 deg of level, held from 40%; one turn per push; d-pad at once), X / Y
  reverse (X was full thrust until 2026-11-06), A / LT fire, B smart bomb, LB / RB shield (Andes) / inviso + Tab (D2).
- RT analog thrust: the squeeze sets a target speed. Below it the thrust is held (the game's own acceleration),
  at it the one-step overshoot is taken back (engine lit), above it released (the game's glide / friction).
  Andes: speed index $14BC4 (0..19; climb delays table $14BC6 1..32 frames, $2D77C = thrust held). D2: velocity
  $1B4B4 (16.16, + right), accel $1B4BE (0x3000), friction $1B4C6 (0x1800), top $1B4CA (9 px a frame);
  left alone while dying ($2F2DE countdown: controls skipped). Measured: D2 half 4.7, 30% 2.6, full 9; Andes
  half 10, full 19 (2026-11-07: identical to the original's mouse-button thrust frame for frame - 0 -> 19 in
  ~4.8 s; owner: keep it as the original),
  andes_altitude (owner 2026-11-07, panel ALTITUDE: DIRECT / ORIGINAL): 1 = the stick moves the ST mouse again
  (4 px a frame, quadratic; d-pad 3) and the game's own mouse handling moves the ship - the mapping before the
  direct write; default 1 since 2026-11-07 (owner: "original feels perfect"); 0 = direct. eased 17 -> 5 in ~2 s.
- Tests: --press=N:grt@<0..1> (part squeeze), --watch=@<hex> (absolute address). Scripted runs die often:
  defender2_invincible=1 in the test userdata's settings.txt; Andes dies around frame 1150 with the smoke input.

## Docs on X (2026-11-07)
- Docs on X (owner 2026-11-07): X on the carousel = the disk's docs (hint "X DOCS" only where there are some);
  the CRT toggle left X / C (F9 and the panel keep it). Gridrunner: Game::docs_in_game - the game runs unseen
  (grid::StartDocs / DocsSkipping: black, silent, 100 frames a present) to its title, clicks Alt + left until GAME
  SECRETS (state 7, ~954 frames, ~1 s), no hint; A pages, B or the last page (title) = back to the disks.
  Trip-A-Tron: docs_text "MANUAL.TXT|README.DOC" (names joined, each found shown with a heading); MANUAL.TXT =
  Tools/make_trip_manual.py (pdftotext, not pypdf: it splits words) from Private	rip-a-tron-manual.pdf (James
  "Piku" Grimwood's 2003 transcription), into the Trip folder; package_portable.ps1 runs it and checks it. The
  text viewer: right stick pages too, L3 / Home top, End bottom.
- Andes Attack's docs (owner 2026-11-07): Game::docs_via = "gridrunner", docs_screen 1 - Gridrunner's ADDITIONAL
  INFORMATION (Ctrl + click on the title) run the same way, its text start ($11CDE: movea.l #$49762,a0; operand
  $11CE0) moved to the essay "yak reviews the reviewers" ($49B32, whose 10 bytes \c3 \s 2   become \g 2 2 \c3
  \s 2: top left). Text codes: \g x y place, \c n colour, \s n size,  line, \e end. Its screen runs in state 2;
  back to the title = done. Every docs run shows CRT++ as CRT+ (crt::SetDocs; owner: D2 / Hardcore readers
  unreadable). Package: Extras\Trip-A-Tron manual.pdf. Gotcha fixed: a docs_via disk sent down the text path
  looped the launcher (no text -> kDocs again every frame).

## Launcher previews, rumble, pad button names (2026-11-07, owner)
- src/preview.cpp: the picked disk's game runs unseen behind the carousel (after 0.45 s still; launcher Update
  -> preview::Select / Update, Render -> Draw before the pools of light). Per game a Script: input (past crack /
  shareware screens: Llamatron j1 fire 150, Gridrunner click 600, Camels STE fire 300, TT nag patch + fire,
  Andes click 300, Hardcore clicks + a wandering pointer (strobes off $10D66), Trip: Esc then a Lissajous light
  show, palettes 1 2 3 7 8 9 only), when it's ready, the CPU rule (the registry's play rule, Llamatron's title).
  Machines loaded like RunGame's (saves read, none written: empty save dir), kept per game (resume where they
  were); each slice switches Musashi's context (cpu::SetMachine + Save/RestoreContext). Warm-up 4 ms a launcher
  frame, others warmed 1.5 ms when one shows (SetGames). Look: a trail texture (old frame faded 70/255, new at
  0.55, a flash guard dims frames brighter than the running mean), drawn ~80% of the height at 40%, cool tint,
  four offset ADD copies (defocus), breathing / drift / flicker, a rounded vignette past the copies. Log:
  "preview: x ms a launcher frame" (here 0.3-1.2, worst ~6). Test: --preview-test=<id> (frames to logs\preview).
- src/rumble.cpp (setting rumble, GENERAL): lives counters found by RAM search (--dump-every=K:<prefix>, new):
  Llamatron $5C0CF, Andes $12BB6 (bombs $12BB4), Photon $1D124 (bombs $1D126), Camels $19264 / TT $1929C,
  Hardcore $14839; Gridrunner state 1 -> 4; D2 death countdown $2F2DE from 0, bombs $2BA44. Only when pad 1 was
  used in the last 15 s (attract demos die too). Verified in scripted runs (log "rumble: ...").
  2026-11-07 more (owner): kills (score digits up: Andes $12B96.., D2 $4B098..; Photon: its score digits on the
  screen shown change - RAM score not found - not for 150 frames after a death; Llamatron: the kill flash list
  *(text+$25EA) set to text+$25E6, stronger), thrust hums (Andes speed $14BC4 / 19, D2 |$1B4B4| / 9 px: 0.05..0.18
  low motor), Photon boost hum (Kbshift copy $27C51 bit 1 + gauge $1D12C: 0.06), Hardcore's puck ($1B714 / $1B718,
  16.16: the motor on its side, with its speed, <= ~0.2), Camels pickups (the pickup flash: $3D5D0 = $3D636 /
  $3D736 with level $3D5D4 jumping; TT $3D960 / $3D9C6 / $3DAC6 / $3D964: a 2-frame blip). Pulses + hum mixed per
  motor, sent when changed or every 5 frames (150 ms). Log: "kills so far" every 10, "hum up to" every 500 frames.
- Button names: remap::PadStyle (pad 1's SDL type, or setting button_names 0 auto / 1 Xbox / 2 PlayStation /
  3 Nintendo) -> InputName (CROSS CIRCLE SQUARE TRIANGLE L1 R1 L2 R2 OPTIONS SHARE/CREATE; Nintendo by the
  printed labels, L R ZL ZR + -). How-to-play pages and our UI mark buttons {A} {START}...: Rename / UiText
  (launcher DrawText, panel text) / RenameHint (game hints: "A:" tokens, Start / Back too). Unmarked = prose.
  PlayStation names are glyphs (owner: the words ran off screen): src/glyphs.cpp, codes - = cross /
  circle / square / triangle (pad colours, dimmed with the text) / options (three lines) / share (an arrow), drawn
  as 8 x 8 pixels in a character cell by glyphs::Text (replaces SDL_RenderDebugText in the launcher, panel, hints).
- Launcher (owner 2026-11-07): disks 10% smaller (DrawDisk world() x0.9), no pools of light / bloom / spotlight:
  an even front light (0.35 + 0.55 diffuse) on every disk, the preview behind is the backdrop.

## Steam (2026-11-07, owner: art + an ADD TO STEAM option on the Settings disk)
- Art: Tools/make_steam_art.py <shots> -> res/steam/ capsule (600 x 900), wide (920 x 430), hero (1920 x 620), logo
  (transparent), in make_cover.py's style; tiles = screenshots through each game's own CRT++ look (not in git:
  logs/steamshots), taken with --window --window-size=1920x1200 --program=X --rshot=N:<name>_N.bmp and the smoke
  inputs (PICKS in the script: shot, centre, zoom). Shipped as Extras\Steam art (package_portable.ps1).
  2026-11-07: the tiles are now mostly the owner's own title-screen captures (logs/steamshots/own: Llamatron, Grid,
  Revenge, Trip, photon, Andes, Hardcore; from their Videos\Captures), ours only for Defender II's title.
- src/steam.cpp AddToSteam (panel GENERAL, ADD TO STEAM, Kind::kSteam; outcome on the row, <= 18 letters):
  launched from Steam (env SteamGameId = appid << 32 | 0x02000000; Proton: Steam root = Z: +
  STEAM_COMPAT_CLIENT_INSTALL_PATH) -> art only, for every user whose shortcuts.vdf has that appid. Else (Windows,
  registry SteamPath, loginusers.vdf MostRecent user): our exe's shortcut found -> art; else Steam must be
  closed (steam.exe; it rewrites shortcuts.vdf on exit), the entry is appended (binary VDF; appid =
  crc32("\"exe\"" + "Llamasoft on the ST") | 0x80000000, verified; .bak kept) and the art copied to
  config\grid\<appid>{p,,_hero,_logo}.png. Tests: LLAMATRON_STEAM_ROOT=<stand-in folder> (also skips the running
  check) + --steam-add (once, then quit). Verified on a copy of the owner's files: the 14 existing entries byte-
  identical, second run = art only, SteamGameId path. Never run --steam-add without the stand-in root.

## Settings panel and buttons (2026-11-03; supersedes the "Back = pause", "B twice = disks" notes below)
- DONE (owner tested 2026-11-03: the panel and every pause "very happy"; the Settings disk stays last on the
  carousel). Known: turning Remaster off while suspended shows the frame without sprites until resumed.
- src/settings_menu.cpp: one table of every setting (key, kind toggle / slider / choice, default, range, help,
  "next start" for ones read at game start); 8x8 font at the output's resolution on the left ~half, picture to
  the right as the preview. Changes: settings::Set + Save, then main's ApplySettings (every module's
  LoadSettings; stgames::LoadSettings is new). Defaults in the table must match where each setting is read.
- Settings disk: games::Game::settings, appended last by Discover (label: make_labels.py STAND_IN, no byline);
  the launcher returns Action::kSettings, main RunSettings shows the panel over c.preview (the last game's
  frame, remaster sprites composed in, saved as userdata\preview.bmp) or TestCard through the CRT look.
- In a game (not a docs reader; Trip-A-Tron: Back only, F1 is its own key): Back / F1 suspend the emulation (no frames, audio cleared,
  remaster alpha 1) with the panel (general, the game's section, CRT); Back resumes (pacing resyncs), B / Esc
  leave to the disks. Key releases still reach the ST while it is open.
- Start = the game's own pause everywhere (Defender 2 in play: host pause, it has none). "B twice" removed
  except Trip-A-Tron; Llamatron / Camels no longer fire on Start; user disks: B = Esc.
- Tests: --mpress=N:<pad button or key name> and --menushot=N:<bmp> at the panel's Nth frame (counted while
  open). The panel's d-pad is polled (repeat), so scripted moves use key names (Down, Right). The launcher's
  frame counter now runs across visits. SaveRenderShot reads the whole window.
- Gotcha: pad events arrive in the background (SDL hint): the owner's pad opened the panel during a test.

## Button remapping + panel tidy (2026-11-07; PLAN-CONTROLS.md, owner's answers below)
- src/remap.cpp: per game a list of functions (each = the game's own buttons, the first = what attached buttons
  act as); physical -> logical for pad 1 only, in a game only (main RunEntry remap::SetGame; not Trip-A-Tron, not
  the docs readers: no panel there). Applied on all three paths: pad::Poll (stgames), remap::Button / Axis
  (input.cpp Llamatron + generic, gridrunner.cpp), events (main: pad 1's real button events dropped while a
  remap is active, Start / Back / Guide kept; remap::Events makes the logical downs / ups from the state each
  frame; SyncEvents when the panel closes). Triggers are inputs too (owner): bits kLt 30 / kRt 31; a button on a
  trigger's function = full squeeze, a trigger on a button's = on past half. A listed default attached nowhere
  does nothing; never-listed buttons (L3...) pass through. Saved <prefix>_controls_<fn> = bit mask (prefix: id,
  rmctt -> rmc, andes_arcade / _stick, defender2_arcade / _stick, user disks disk_<name>). settings.cpp now
  writes %.10g (%g rounded the masks to 6 digits).
- Panel: GENERAL (REMASTER, CRT LOOK second), the game's section, CONTROLS (in a game only; owner), CRT LOOK,
  CRT++. Every heading but GENERAL folds ([+] / [-]: A, right opens, left closes), remembered (panel_open_<id>,
  the game's own = panel_open_game, default open; others closed; the Settings disk its own panel_disk_open_*,
  all closed). Controls rows: A = listen 5 s (a button / trigger toggles it on that row, moving it from another;
  Start / Back / Esc cancel), X = the row's defaults, RESET CONTROLS. Andes / D2 "CONTROLS" choice renamed
  CONTROL SCHEME. Button names (2026-11-07): how-to-play
  pages mark them {A} {RT}... (games.cpp help; remap::Rename, columns kept), hint lines by "A:" / "LB/RB" (RenameHint
  in main), both showing the physical buttons that act as each (X/LB; -- none). Not marked: Start, Back, d-pad, the
  panel's "B there", Trip-A-Tron.
- Tests: --remap-test sends scripted pad input (g..., pad<button>) through the remap. Verified: panel driven by
  --mpress (Y attached to Photon Storm's SMART BOMB), then gy in play -> bombs 3 -> 2; Andes scheme switch
  rebuilds the list; smoke all 12 pass. Owner tested the build: works. Gridrunner's d-pad up / down are now listed
  under NOSECONE SHOT UP / DOWN (the owner moved them there from UP / DOWN rows, which made them RB / LB on the
  menus too); list a button under what it does in play, so moving it there keeps it as itself. Owner tested with a real pad (2026-11-07): remaps work well.

## Mutant Camels twin stick (2026-11-05; owner asked: move on the left, aim on the right)
- The camel spits all the time in the joystick's direction (Yak's README; fire held = no jump / sit). Play reads
  the joystick at $12778 (STE): d0 -> 8-way table $38D34 -> shot velocity $38D24 / $38D28 (16.16 longs, 3 px,
  neutral = right; the goat's turret $17B9C reads it too), the same bits walk / jump / sit. stgames.cpp hooks
  the move.w d0,d1 after it ($1279C; TT $127D4, mapaddr.py) and, with the right stick pushed (0.4 / 0.3),
  writes its direction (speed 3 / max(|cos|, |sin|)). Confuzer $3BFDE >= 0: aim mirrored, or left alone with
  two players ($3933C = 2: joysticks swapped). Setting rmc_twin_stick 0 off / 1 8 ways / 2 360 (default);
  panel section MUTANT CAMELS (rmctt shows it too). Not the 512K build. Verified: up while walking right,
  jump while aiming up, 160 deg = (-3, 1.09); TT 225 deg = (-3, -3); smoke all pass.

## Owner's settings baked in (2026-11-07: "happy with the CRT tuning and controls")
- crt.cpp kGameDefaults: every <game>_crt / <game>_crt_psy_* from the Deck test build's settings.txt (generated:
  andes, defender2, gridrunner, hardcore (CRT++ sliders only), llamatron, photonstorm, rmc, rmctt). GameGet():
  the player's own <game>_<key>, else the table, else the shared key; the panel's Value() asks
  crt::GameDefaultValue too; its reset row reads "THE GAME'S OWN CRT++ AGAIN". Shared crt default 1 -> 2 (CRT++;
  the shared sliders already matched). The table is the owner's - regenerate it from their file if they retune.
- remap Function::start = the buttons attached out of the box when not the game's own (identity stays
  defaults[0]): Andes / Defender 2 arcade - fire on RT, thrust on A and LT (LT still the analog squeeze, A full
  thrust). StartMask() for the initial map, X / RESET, Changed(), the panel's colours. Menus that pick with A
  (D2 title / player select, Andes / D2 name entry) take RT too (pad::RtPressed). grid_stick_speed 6.5 -> 6.
- Not baked (not CRT / controls): volume 0.6, trip_music 2. Tests: scripted pad input bypasses the remap unless
  --remap-test.

## Volume (2026-11-05; owner: games "really loud on a TV", some louder than others)
- Settings volume (master, 0..1, gain = v^2: 0.5 = -12 dB) and volume_normalise (1): main.cpp scales the samples
  (clamped) before the queue and the --wav dump. games.cpp NormalGainDb = -23 LUFS (EBU R128) - each game's
  measured loudness (scripted smoke runs, title + ~1 min play; Tools/loudness.py, run with python -P): Andes -7.1,
  Gridrunner -5.3, Camels TT -3.8, Photon Storm -3.7, Camels -3.2, Defender 2 -3.1, Llamatron -0.6, Hardcore +1.3
  dB; user disks / Trip-A-Tron (silent) 0. Verified: all eight at -23.0 LUFS after, peaks -4..-10 dB.

## CRT++ (2026-11-05; owner: "gorgeous", after Minotron's look: "lurid and overwhelming")
- Setting crt = 0 off / 1 CRT+ (the CRT look below) / 2 CRT++; F9 and the launcher's X cycle all three.
  crt.cpp RunPsy before the CRT shader: the picture copied to 1280 x 800, a feedback trail (psy_trail.frag:
  the last trail zoomed / turned about the centre, faded, tinted, hue-turned, bright parts fed in; two float
  textures in turn), then psy_mix.frag (colour bleed, trails screened on where the picture is dark - owner:
  yellow must stay yellow), then the CRT with glow x crt_psy_glow and saturation x crt_psy_saturation.
  Per-second rates (any refresh). Trail colour (2026-11-07, owner: any colour but blue went
  white or bled): the tint measures the trail with per-colour weights (psy_trail weigh, crt.cpp: same fade and white
  flash as the default blue for every colour; plain luminance at 0.66, so that look is unchanged). Settings crt_psy_* in the panel's CRT++ section, own reset
  (crt_psy_defaults). Tests: a windowed run with --rshot (headless has no renderer).
- Per game (owner 2026-11-05, after the Deck test: 90 fps, GPU ~35%): the look (<id>_crt) and each CRT++ slider
  (<id>_crt_psy_*) are the game's own once changed in its panel; until then, per key, the shared ones (the
  Settings disk / launcher edit those). crt::SetGame(id) around RunGame; panel: "CRT++ FOR THIS GAME", Delete
  on a row or "USE THE SHARED CRT++" drops the game's own. F9 in a game sets the game's look. settings::Has.
- Survey of the screens outside play for the remaster: REMASTER.md "Outside play" (--survey, Tools/survey.py).

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

## Cover (2026-11-01): res/cover.png, Tools/make_cover.py <folder of screenshots>
A 320 x 200 ST screen x4 in ST colours (3 bits a channel): 8 game tiles (80 x 56 crops of --shot screenshots,
CROPS in the script; the screenshots aren't in git), make_icon.py's llama on a badge, the title in SDL's 8 x 8
font doubled with a raster gradient. In README.md.

## Release 1.3.0 (2026-11-01, branch more-st-games, not pushed yet: owner "we'll push it later")
- Tools\package_portable.ps1 [-Release]: game\ = Llamatron ST.msa, Trip-A-Tron\ (TRIP_A_T.RON's files), the
  Revenge 1 MB STE .stx (copied with -LiteralPath: the download's [brackets] are a wildcard to -Path) and
  Andes Attack / Defender 2 / Hardcore .st built by Tools\make_release_disks.py (owner: Llamasoft-only - the
  compilation disks' Batman, Hot Wheels, MJ etc. don't ship; files kept = what the games open, traced:
  Andes ANDES.BOY HISCROES.YAK; Defender 2 DEFENDR2.PRG/.EXE SAFESNG2.EXE (Pexec 3: title tune)
  DEFENDER.SET TITLE.4V READ_ME.PP; Hardcore HARDCORE.PRG DOC100E.PRG EXPLAN.TXT). The script checks every
  game arrived; the leak check allows loose files only in Trip-A-Tron\.
- Tools\smoke_all.ps1 [-Exe <exe>]: run it on the unpacked zip (outside the repo: only its game\ is found).
  1.3.0 zip: all 10 pass. res\README.txt and README.md cover all the games.

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
  A = random of the three, X Space, Y Esc, Start F10, B = the number row (was Back; see "Trip-A-Tron B").
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
- Trip-A-Tron text (2026-11-07, owner: tool screens full of blank boxes): every label on its tool screens (Sillyscope,
  Global Parameters, MIDIlink, Colour Cooker, Pattern Edit, Starfield, VSEQ, alerts) is VDI v_gtext ($396f6 builds
  the block, trap #2 at $33808), which our GEM stub ignored. machine.cpp now does v_gtext, vst_height (it asks for
  4 = the small 6 x 6 font: our own 5 x 5 glyphs, kSmallFont, not Atari's), vst_color, vswr_mode, vst_alignment,
  vst_effects (logged, not drawn). The KML key screen and the panel's icons are its own graphics.
- Trip-A-Tron B (2026-11-07, owner; manual Private\trip-a-tron-manual.pdf, Piku's 2003 transcription):
  Back = the settings panel (leaving is there; B twice removed). B while performing = the number row 1..0 - =
  (manual: demo 1 random palettes, demo 2 lasers). B elsewhere = back to the Control Panel (tripatron.cpp
  Back): state $108B0 per screen -> its exit button clicked (kExits: 5 resize tick, 7 waveform quit-llama, 9
  osc FX QUIT, 11 event assign QUIT, 12 disk BAA sheep, 13 VSEQ QUIT, 14 IES quit figure, 16 FINITO, 17 QUIT
  EDIT, 18 NAFF OFF, 19 VAMOS, 20 EXIT) or Esc (10 KML editor, 15 starfield); state 2 + $10A78 = an alert
  (click 136,47: every alert's button row, a two-button one's "no"), $1036E = a side panel (state 4 while it
  slides in; $16AFE 1 = Sym: OK 308,131, 0 = Cube: GROOVY 299,194), else Esc (panel away). Clicks repeat every
  30 frames until the state changes (screens ignore clicks while they slide in / load: Disk ~2 s, its own
  sub-state $1BB34, buttons table $242BC). Verified all 36 icon / button screens. Pointer = $10692/4 in every
  screen, buttons $10696. Other manual notes: icons differ by left / right button; F1-F6 plot switches,
  Delete = next symmetry is presym, ; / ' secondary cursor on / off, ? starfield off, right button steers some
  starfields.
- Trip-A-Tron as an instrument (2026-11-07, owner: "best-in-class", holistic redesign; pass 1 done): RT analog =
  paint density (pulses: 0.12 + 0.88 p^2 starts a frame; trip_pressure), LT = right button, R3 = latch, LB / RB
  tap = palette, held (11 frames or the right stick pushed) = effect wheel (keyboard rows Q / A / Z, d-pad row) /
  symmetry wheel (+ Y = pre-symmetry: Delete, then the key 5 frames later), Y tap = panel, held = plot layer (LB A
  X B Start RB = F1-F6, d-pad U/D = pre-symmetry), overlays trip::DrawOverlay (main.cpp after the hint), panel
  d-pad grid (9 x 3 incl. presets, kIcons names + A / X meanings), tool screens: d-pad nudge, LB slow pointer;
  trip_flow (inertia), trip_gyro / _speed (SDL sensor, yaw / pitch), trip_rumble (music beats). Tests: gr3 / gl3,
  "trip key $xx" logged when a test pad is used. Pass 2: KML system variables found - names at text $2F2C3
  ($FF-separated), addresses at $2F3BA (CX $1146C, CY $1146E, BUTTONS $10696, SVX/SVY/SVZ $23AEE/F0/F2,
  ANG_VELOCITY $23AF4 (reset every frame: no use), ABS_ANGLE $23B04, PATTERN $20C8C, GA..GZ $3F58E..); $23AEC =
  starfield running. Done: the right stick flies a running starfield (SVZ offset eased back on release, spin by
  adding to ABS_ANGLE, RT boost). Not done (deeper digs): live keys only (the key tables $15DF4 / $42B86 are
  identity maps; event programs not located), the stages' on / off (F1 rewrites a routine-pointer chain at
  $3B848, no flags), Global Parameter banks (KML commands: 114 names from $2ED74, GLOBAL = 54, dispatch not a
  pointer table), laser steering (Sillyscope parameters aren't KML variables).
- Trip-A-Tron effect labels + stage on/off (2026-11-07): the loaded KML is in RAM - programs from $3C4DC (event
  n = the n-th, $FFFF-ended), key -> event words at $3E632 (by scancode, >= 128 none; the "identity" table was it),
  command names / argument counts at $3EE74 (TRIP.PRG text $2ED74). The .KML file: programs, offsets at $1F46, keys
  at $2156. tripatron.cpp Describe() names each key from its program (DECAY / LINE / EXPANDOR SHAPE n or ANIMATED,
  STARFIELD: WARP / DRIFT / SPIN / WAVES / RANDOM + LIGHT STEERS (reads CX / CY), LASER n, PALETTE, STROBE, 2ND
  CURSOR, SWITCH <stage>, LOAD THE OTHER DEMO); wheel greys keys with none, d-pad / surprise / music skip them;
  scripted runs log "kml key X: ..." when the KML changes. Plot stages on / off: the FX mask byte $15B30 (bit 0
  pre-sym .. 5 post-sym; KML FXENABLE / FXSTATE) - green in the Y strip. Gyro: SDL's HIDAPI Steam Controller
  driver is on by default on Windows (gyro reported); under Steam Input only Steam's virtual pad (no gyro) is
  seen - a hint says so once.
- Settings panel: TRIP-A-TRON CONTROLS (section "tripkeys", owner 2026-11-07: a reference, no remapping - too
  complex): Kind::kInfo rows (label = control, choices[0] = what it does, help = the details), after the game's
  own section, open by default in Trip-A-Tron; also on the Settings disk (folded).
- Shared (2026-11-01): src/pad.cpp = pad 1 as a per-frame snapshot (real or the test pad from --press=N:g...)
  with edges; src/stgames.cpp = RMC and Andes controls (modes kRmc / kAndes). Registry play rule (games.h
  play_at / play_value / play_word / play_vbl): the faster CPU (--cpu, x8) while that RAM value says "play";
  --cpu-all=N forces a speed for a whole run (tests). Machine::TypeAhead = keys in the console buffer before the
  program asks (crack prompts). --palette-log=N:M. Tools: copy Tools\dis.py as d68.py elsewhere and run it on
  a --dump (runtime addresses).
- RMC TT Megamix (owner 2026-11-01, Private\Revenge_of_the_Mutant_Camels_demo-playable_1991_Llamasoft_TT_Megamix.st;
  id rmctt, its own disk "Camels TT Megamix"): REVTT.TOS + README.TXT in a folder "REVENGE TT". The .st has 10
  sectors a track (82 tracks) while its boot sector says 9: disk.cpp Logical() picks TOS's logical sectors out;
  its root starts with an empty sector (ReadFiles skips all-zero entries now). Jeff's notes: 32 bullets instead
  of 8, twice the objects, the STE extras. Same code as the STE build: hook $3E0CE, play $11206, pause $10580,
  bonus $112CA, name $194E0, title $102C4, menus $1085E, keys $1AF32 (flags $3E047), joystick $3E0C8/9 (own
  IKBD handler $1AEE2), frame counter $3D94E. Shareware screen: fire only counted after 2000 VBLs (40 s; loop
  $102A0, same code in the STE build, which doesn't show it) - owner: keep the screen, fire skips it, so
  stgames.cpp NOPs the ble at $102A8. Listed after the STE Camels (owner); in the release zip as
  "Revenge of the Mutant Camels (TT Megamix).st" (owner: long past on sale, a historic artefact); smoke rmctt. 8 MHz: ~9 drawn frames/s in play; x8: 50 (scored
  ~3x the STE build in the same scripted 50 s). Pad / pause / DMA sound verified.
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
  (verified: a bomb icon crossed out), Right Shift pause, Space resume; Kbshift + Crawio. Shield (found
  2026-11-01): Alternate held - the play loop stores Kbshift in $12C06 ($1425E) and tests bit 1 fire, bit 3
  shield ($143B2: on while held, $1C444 = 1, energy $1C440 from 16 drains 1 per 20 frames; "SHIELD" gauge over
  the ship), bit 2 smart bomb (count $12BB4). Pad: LB / RB held. Warp: not found.
  Feel (owner 2026-11-01: "a lot of inertia", up / down tricky): measured - the altitude $2B682 follows the
  mouse within 1-3 frames and the ship the next frame, no easing: the lag was our stick -> mouse mapping (4 px
  a frame max, quadratic). Now in play the altitude is written directly (owner: 6 px and 1 step a frame were
  "a bit too sensitive" -> halved: 3 px, andes_thrust_ease 0.5 step a frame): andes_stick_speed (3) x
  ((|y| - 0.15) / 0.85) ^ andes_stick_curve (1.5) px a frame. Left / right inertia is the game's: the speed
  index $14BC4 (0..19) climbs a step every few frames while thrusting and sinks over ~10 s. andes_direct_thrust
  (1): the index follows how far the stick is pushed (19 x push), andes_thrust_ease steps a frame (0.5: 0 -> full ~40 frames), the
  thrust button held while moving (exhaust); direction from 0.2 deflection. Owner tried it: "left and right
  feel wrong" -> default 0 (the original thrust); the vertical change kept. Text $2B494 "CONTROLS: Mouse = Altitude, Left
  Mouse = Thrust, Right Mouse = Reverse, Left SHIFT = FIRE". Crack screen (right button = trainer, $1041A)
  ends by itself; clicks at frame 100 changed nothing - trainer not offered yet. 13-19 drawn frames/s in play,
  x8 -> 50 (thrust / altitude identical x1 vs x8). Its sky is a timer B raster: no calm background (it
  changes colour 0 per line). Pad: see stgames.h. States after play: 4 game over, 5 high score table; a name
  is typed there (Crawio $1A38C: letters, Space = "_", 3 of them, count $1BC96, cursor $1BC92 >= 0 while
  entering), then back to the title. Pad: up / down picks a letter (shown on the hint line), A types it.
- Defender 2 at 60 Hz (owner 2026-11-01: default; the smart bomb flash stays - "very quick"): it reads
  $FF820A at boot ($1EC04): bit 1 clear = 60 Hz -> "60 hz game speeds adjusted" ($2043F = 1; Options can
  switch it, $1F33E). Registry boot_60hz + setting defender2_60hz (1) -> Machine::SetBoot60Hz (sync_ 0).
  Fullscreen re-picks the display mode when the game's rate changes (Context::mode_hz: 120 Hz on a 120 Hz
  screen at 60, 100 Hz at 50). The title comes at frame ~1500 (1400 at 50 Hz). Play draws 60 a second.
  Revenge (bchg $FF820A at $10852 / $10802) and Hardcore (h) only flip the register: 20% faster - not used.
  Title on the pad: the first d-pad press sends Space (the pointer only shows after it); $1E6BE = 1 while
  the title waits for Space (set a few frames in): Space is sent again on presses while it is 1 and a due click
  retries (2026-11-07: a Space sent as the title appeared was missed - no pointer, A did nothing); the hint line
  names the choice.
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
- Photon Storm (owner 2026-11-01, Private\photonstorm-st.zip -> Private\new\photonstorm\photonstorm\
  PhotonStorm.st: a Llamasoft compilation - PHOTON.YAK, DEF2.YAK, ANDES.YAK, GRIDRUN.YAK are TOS programs;
  the boot sector is AtariForce's message, not a loader): PHOTON.YAK (Automation 2.2f; Arc / Llamasoft 1990,
  the Andes Attack engine). State $44846 (1 play, 7 title, 10 paused). Own mouse handler $448EA: pointer
  $4F1C0 / $4F1C2 (0..79, limits $448E4 / $448E6), buttons $4F1C8; in play the pointer's 5x5 cell ($17BCA,
  table $27958: 16 directions, centre -1) = the ship's direction $27956; left fire, right thrust (verified).
  Right Shift pause (Kbshift bit 0 -> 10), Space resume, Q quit. Title: left = 1 player, right = 2 players;
  left / right also change $16E5A (a level start). Draws every $466BE VBLs (2) - the main loop $4622E waits for
  $4F1BE; logic in the VBL: x8 + divisor 1 in play = 50 frames/s, state and final picture identical to x1
  (photonstorm_50fps). No 60 Hz mode. Pad: stgames.cpp (stick = direction written to the pointer, far =
  thrust). 360-degree analog (owner 2026-11-01; settings ps_analog=1 default, 0 = the original 16): the ship's
  sprite is 16 pre-rotated frames ($2D53E, heading $27942), but aim $27956, the turn ($17D02: 1 step a VBL
  towards the aim while thrusting), the drift ($17DEC: speed $27944 -> table $27760, d6 / d7), new shots
  ($18494, slots $27974 x 16 bytes, 12 px a frame) / their movement ($181DA) and the aim marker ($15C30) are
  hooked to the stick's exact angle (speeds kept: length / max(|cos|, |sin|)); the triple shot keeps its
  +-22.5 spread. Hooks go in only once state = play: the unpacker copies from its own output (an early
  ILLEGAL got copied into other code -> crash). Measured: stick 20 deg -> drift 19.9, shots 19.5-19.9 (orig 26.6).
  Twin stick (owner 2026-11-01, with ps_analog): the right stick pushed (on 0.4 / off 0.3) = the aim (pointer,
  marker, shots) + fire; the left stick only steers (the turn hook turns to its angle, not $27956) and thrusts;
  left alone = as before. Verified: left 0 + right 135 -> heading 8, drift 0, shots 135 +-22.5.
  Y on the title (state 7) toggles ps_analog (saved); the hint line there names A / X / Y and the mode. Release: "Photon Storm.st" with PHOTON.YAK + PSHIGHS.GNU (make_release_disks.py).
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
  owner: "like a button", no repeat while held; next flick after it returns below 30%); RB/LB (and in the game d-pad up/down, owner 2026-11-06; the d-pad's left/right still move slowly) = one step up/down
  per press; Y = Control (2026-10-23; was RB). X/Y/LB/RB only in the game, not on the title (menu shortcuts there)
  or the shortcut screens; Back/Start
  = pause/resume; B = pause: quit to title (Q), title: quit (twice). Keyboard: every key goes to the ST.
  Tunables in userdata\settings.txt, reread while running (src\settings.cpp): grid_stick_speed (6 px/frame at
  full deflection), grid_stick_curve (2 = quadratic), grid_stick_deadzone (0.15), grid_dpad_speed (1), grid_stick_smooth (2) / grid_stick_stop (1) (2026-11-03,
  owner: the direct stick felt "brittle": the speed eases to the stick's, time constants in frames, 0 = instant;
  measured full push 2, 4, 5, 6 ... 6.5 px a frame, release 3, 1, 0), grid_stick_speed now 6.5,
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
  select (6): d-pad left/right = [-]/[+] at (115,120)/(200,120), up/down = the pointer between the
  [-] [+] row and LET'S BLAST, A clicks what it is on, no hint line (owner 2026-11-03), LET'S BLAST!! (157,172) (x 101-214, y
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
- Restart best (already in the game, verified 2026-11-01): the level select's [+] stops at the byte $49735 =
  HISCORES.COW byte $215 (4 on the disk). Game over ($18638) on level N >= 5 raises it to N - 1 and saves the
  file (after name entry if any; not after the pause screen cheat: $13846). So dying on level 6 unlocks 5.
  Owner 2026-11-01: unlock the level you died on - hook at $1865C (gridrunner.cpp: d0 = level + 1, max $40;
  setting grid_unlock_level_died, default 1). Verified: died on 9 -> 9 saved, the select goes to 9.
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
  joystick 0 fire. Back = pause/unpause (P/O). A second pad = joystick 0 only in TEAM (title mode 3; before 2026-11-07 any second pad took it, so two pads connected broke twin-stick). Keyboard: WASD + Space =
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
  and its own 60 Hz mode is just 20% faster, so: show 50 Hz evenly). 2026-11-01 (owner: exclusive fullscreen
  "a hefty blink when minimising"): fullscreen is BORDERLESS at the desktop mode by default (clock pacing unless
  the desktop rate is a multiple; VRR makes it even); settings fullscreen_exclusive=1 = the old behaviour below
  (opt.match_refresh). The launcher hides the cursor too (fullscreen; windowed: shown while moving). Before: fullscreen picked the desktop resolution at
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
Halation (2026-11-03, owner: the old one "looks fake" - a 5 x 5 grid of point taps gave square grey copies around
stars): crt.cpp BuildGlow makes the picture as linear light (glow.frag.hlsl) at 320 x 200, then blurred levels
160 x 100 ... 10 x 7 (float textures; 4 offset quarter copies each); the shader adds their weighted sum after the
contrast curve (before it, the S-curve drew a rim: "like a sticker"). crt_halation = amount, crt_halation_size
0..1 = tight halo .. long tail. Owner's values are defaults version 4. The log says "crt: glow levels in float".
