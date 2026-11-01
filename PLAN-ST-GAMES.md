# Plan: every Llamasoft ST game in the launcher, with modern pad controls

Written 2026-11-01 at the end of the session that added Trip-A-Tron (branch `more-st-games`, commit 55a4e24).
Goal (owner): all the Llamasoft ST games boot from this launcher and play well on an Xbox pad. Llamatron, Super
Gridrunner and Trip-A-Tron are done; this plan covers the rest. CLAUDE.md ("More Llamasoft ST games") has the
facts found so far. Keep this file up to date as games are finished, and delete it when the plan is done.

## Done means, for each game
1. A disk in the launcher (colour, label, how-to-play page), started like the others; B twice returns.
2. Its own save folder `userdata\<game>` (high scores, settings it writes) - as Trip-A-Tron's.
3. Found automatically: its image or folder in `game\` next to the exe, the exe's folder, or `Private\`.
4. Pad controls designed with the owner (the owner's focus), plus keyboard and mouse as the original.
5. Crack intros / trainer prompts skipped or turned into options; no white TOS-console screens shown.
6. Smooth: no slowdown in play (faster CPU in play only, as Llamatron / Gridrunner, if needed); flashes
   reviewed with the owner (Llamatron's were removed).
7. A scripted headless test that reaches play (in `Tools\smoke_all.ps1`, see below), and the release zip
   includes it.

## The games

| Game | Source (Private\new\) | Program | Status now | Expect |
|---|---|---|---|---|
| Defender 2 (Arcadia 1990; Defender, Defender II, Stargate) | defender2-st\defender2\defender2.msa (BBC crack, trainer by The Alien) | DEFENDR2.PRG -> Pexec DEFENDR2.EXE | plays (menus by mouse) | medium: controls design |
| Revenge of the Mutant Camels (shareware 1.0) | rmc-st\DBUG028.MSA (D-Bug 28) | REVENGE.TOS (Atomik 3.3, unpacks itself) | plays (camels, score) | small: joystick game |
| Andes Attack | andesattack-st\andes attack\med_005.st (Medway Boys 5) | ANDES.BOY (packed PRG) | reaches the crack's "LEFT mouse: start / RIGHT: trainer" screen | small-medium |
| Hardcore (unfinished, Yak 1992) | hardcore-st\hardcore\DBUG100E.MSA (D-Bug 100) | HARDCORE.PRG | title picture loops, ignores fire | unknown: investigate |
| Trip-A-Tron | trip_a_t\TRIP_A_T.RON | TRIP1.TOS installs TRIP.PRG | done | release packaging only |

Not Llamasoft, ignore: on Medway 5 BATMAN1/2.BOY, THUND (a comic game from Swansea), BLOCKS (ICE-packed data);
on D-Bug 28 HOT / CRAPGAME.PRG (Hot Wheels + editor); on D-Bug 100 MJ.PRG, AUTO\CCT.PRG; the compilation menus
(AUTO\MENU5.PRG, AUTO\100E.PRG). The D-Bug doc readers (DOX.TOS on D-Bug 28, DOC100E.PRG on D-Bug 100, Atomik
packed) hold the games' instructions: try running them in our machine once console text is drawn (step A3).

Other Llamasoft ST titles that may exist but aren't on these disks (check Llamasoft's site with the owner before
spending time): Colourspace (ST light synth, 1987), Photon Storm. Ask the owner what else they have.

## Order of work
Shared groundwork first (A), then games from closest to furthest: RMC, Defender 2, Andes Attack, Hardcore.

### Progress
- 2026-11-01: A1 registry done (src\games.cpp), A2 carousel done, user disks (drop an image in game\) done,
  options on the how-to-play page (ready for trainers), Trip-A-Tron's palette flashes calmed (CLAUDE.md).
  Next: A3 console text, A5 smoke test, A6 packaging, then RMC.
- 2026-11-01 (later): owner: all boot except Defender 2; wants Llamatron's frame-rate fix and pad support.
  Done: Defender 2 boots (trainer prompt answered from launcher options); RMC: faster CPU in play (17 -> 50
  frames/s) + pad module; Andes: faster CPU in play + pad module (smart bomb found, shields not). Defender 2:
  25 frames/s by design, state / pointer / key codes found. Hardcore: not looked at yet.
- 2026-11-01 (later still): Defender 2 pad done (keyboard scheme forced, menus on the d-pad, Back = our pause).
  Open: its smart bomb's white strobe (patch the game), hyperspace.
- Then: Defender 2 50 fps (divisor), RMC bonus / name screens boosted, Andes / Defender 2 high score names on
  the pad, Hardcore: pad (pointer on the sticks), 50 fps, flashes calmed. Every game boots and plays on the
  pad. Left: A5 smoke test, A6 packaging, Andes trainer / shields, Defender 2 bomb flash.
- A6 packaging done: 1.3.0 zip with all the games (Llamasoft-only trimmed disks), smoke-tested unpacked.
- A5 smoke test done: Tools\smoke_all.ps1 (every game headless from a fresh save folder to play at 50
  frames/s, the docs readers; logs and screenshots in logs\smoke\).
- A3 console text done (VT52), medium resolution too (the D-Bug doc readers work: idea - offer them from the
  launcher, "read the docs", with keys on the pad). Flashes: RMC calmed in play; Andes' smart bomb flash not handled (raster sky).

### A. Shared groundwork (do once, before the games)
A1. Game registry instead of hard-coded games (main.cpp RunGame / RunTrip, launcher kDisks / kInfo / labels):
    one table entry per game: name, how to find its files (image name / file inside, folder with a file),
    program to run, install step (optional), save folder, input module, disk colours, label, help lines, CPU
    boost rule. Trip-A-Tron's RunTrip becomes the general path; Llamatron/Gridrunner keep `userdata\` itself so
    the owner's scores and settings don't move.
A2. Launcher for 7+ disks: three fit on screen; more need a layout. ASK THE OWNER: carousel (picked disk in
    the middle, neighbours shrinking off to the sides) or a shelf of two rows. Labels: the owner's hand-written
    photos (res\labels\<name>-photo.jpg -> Tools\make_labels.py); stand-ins in Ink Free until then
    (STAND_IN in make_labels.py; run it with `python -P`).
A3. TOS console text (VT52): installers, trainers and crack prompts print with Cconws / Cconout and we draw
    nothing (white screen). Either draw it (an 8x8 font into the screen at _v_bas_ad, ESC Y/E/p/q codes) or
    answer prompts automatically. Recommend drawing it: cheap, and the D-Bug doc readers then work too. Font:
    our own 8x8 (no TOS ROM font); SDL's debug font is the same style.
A4. Input modules: copy the Trip-A-Tron pattern (src\tripatron.cpp): state read from RAM, pad polled with edges,
    test pad support (--press g...), key taps, virtual mouse clicks (Gridrunner's pattern), hint line.
    Generic mode (input.cpp UpdateGeneric) stays the fallback.
A5. Tools\smoke_all.ps1: one scripted headless run per game to its play state, screenshot + "reached play"
    check; run after any change to machine.cpp. (This session did it by hand each time.)
A6. Packaging: package_portable.ps1 copies every found game (images / folders) into game\; check the zip.

### B. Revenge of the Mutant Camels (closest)
Known: REVENGE.TOS runs from the D-Bug 28 disk; shareware screen, then play (scripted: j1fire / j1right got the
camel moving and scoring). Fopen(SCORES.YAK) fails -> it may write one (save folder). IKBD $14: joystick events.
Timer A for samples (works).
Steps: find what leaves the shareware screen (key? fire?); find the game's state / title / pause the
Llamatron way (it's the same author and era: likely a VBL hook dispatch); pad mapping (joystick 1 + fire,
maybe joystick 0 for a second player); pause / quit-to-title / quit buttons; check slowdown (--profile) and
flashes; save folder.

### C. Defender 2
Known (CLAUDE.md): loader asks "T for trainer" (Cconin; Return = no), then Invincibility / Friendly sprites.
Menus mouse-only: Space on the title, click a box (rects at $1FA02: Defender (49,128)-(102,158), Stargate
(217,128)-(270,158), Defender II (133,134)-(186,164), Trainer, History, Options), then ONE PLAYER / TWO
PLAYERS starts (the triangle = sector code entry). The game's key-down table $4B381 via translation $4B300;
in play $1B52A (keyboard scheme: Ctrl up, LShift down, # fire, Alt / Tab / RShift / Caps) or $1B8D0 (mouse
scheme: mouse steers, left fire, right, Space / Ctrl / Alt) - which variable picks the scheme and what each
key does (thrust, reverse, smart bomb, hyperspace) is NOT confirmed. Its IKBD handler: we cap mouse steps at
+-4 (done).
Steps: confirm each key's action (watch the variables $1B69C / $1BCC2 / $1BCCC / $1BCD2 while pressing);
answer the trainer prompt automatically (or offer the trainer as a launcher option - ask); menu shortcuts on
the pad (virtual clicks: d-pad picks a box, A clicks); design the in-game pad (Defender style: left stick
up/down altitude, left/right = face + thrust, A fire, B smart bomb, Y hyperspace, X reverse? - with the owner);
the three games may each read the controls differently: test all three.

### D. Andes Attack
Known: the Medway Boys' AUTO\MENU5.PRG menu is skipped by running ANDES.BOY directly; it opens HISCROES.YAK
(the crack's own name; the disk has both spellings); Initmous with a vector and Xbtimer timer B (rasters).
The crack screen: left mouse = start, right = trainer.
Steps: click through (A = left button); find what the game reads (mouse? joystick?); same state / pause /
quit work as RMC; save folder (HISCROES.YAK / HISCORES.YAK).

### E. Hardcore (unfinished)
Known: HARDCORE.PRG shows "HARDCORE - YAK 1992 - A FURRED I PRODUCTION" with colour cycling and timer A
samples; joystick fire didn't leave it. Data: S1-S7 / D0-D10 / BO / GO / NAZE / FFHSC .MBK, files 146.. (ATM5 =
Atomik-packed data). EXPLAN.TXT / ENT_PAK1.DOC are packed: read them with DOC100E.PRG once A3 works.
Steps: profile the title loop to see what it waits for (key, mouse, joystick 0, a file); then as the others.
Being unfinished, agree with the owner what "playable" means here.

### F. Trip-A-Tron leftovers
Release packaging (A6); the owner's label photo; music mode tuning on real music (CLAUDE.md lists the knobs);
untested: the Disk Control screen's file list, KML loading, FOR1024K.

## The owner's decisions (2026-11-01)
1. Launcher: a CAROUSEL (picked disk in the middle, neighbours shrinking off to the sides).
2. Trainers / cheats: OFFER them, as a toggle (per game, on the launcher's how-to-play page).
3. Flashes: REMOVE them, as in Llamatron. Trip-A-Tron too: its palettes 4, 5 and 6 flash aggressively - tone
   them down to pulses if possible.
4. Labels: stand-ins (Ink Free) for now.
5. Wanted: adding more disks as the owner finds them (drop an image in game\ and it appears in the launcher,
   generic controls, no rebuild).

## Techniques that worked (save rediscovering them)
- Run a program directly: `build\llamatron.exe --headless --disk=<image or folder> --program=<NAME> --trace-tos
  --userdata=logs\test-userdata-<game> --log=... --frames=N --shot=N:<bmp>`; from PowerShell quote paths with
  spaces inside the -ArgumentList string (Start-Process splits them otherwise).
- Where is it stuck / what is it doing: `--profile=A:B:<file>` then `python Tools\profile_summary.py <file> 10100`
  (addresses are relative to the first program's text, $10100); `--dump=N:<file>` (one frame per run) and
  disassemble with a copy of Tools\dis.py renamed (dis.py shadows Python's `dis` module; or run with -P).
- What a key or click changes: dump RAM with and without it and diff the words (found Trip-A-Tron's state,
  effect and symmetry variables in minutes). `--watch=<offsets from $10100>` logs words every 10 frames.
- Pass `N:path` arguments as Windows paths (cygpath -m): Git Bash doesn't translate them.
- Programs' own IKBD handlers can be fussy (Defender 2: headers checked before packet bytes) - keep sent packets
  small and realistic.
- Programs that chain to TOS (VBL queue, GEM, Line-A, Pexec) now work; still missing: VT52 console output,
  Initmous absolute / keycode modes, the MIDI ACIA, FDC / track loading (none of these disks need it so far),
  blitter / STE hardware, Line-A beyond $A000/3/6/C/D, most of GEM (AES forms, file selector).
