LLAMASOFT ON THE ST
===================

Jeff Minter's Atari ST games running natively on Windows with modern pad controls:
  Llamatron: 2112 (1991) and Super Gridrunner, the bonus game on the same disk
  Trip-A-Tron (1988; this is its 1992 shareware release), the light synthesiser
  Revenge of the Mutant Camels (1991, the 1 MB STE version) and its TT Megamix (1991: written for the
    Atari TT - 32 bullets instead of 8, twice the objects)
  Andes Attack (1988)
  Defender 2 (Arcadia 1990: Defender, Defender II and Stargate)
  Hardcore (1991), Yak's unfinished 5-level evaluation demo
  Photon Storm (Arc 1990)
The original game code runs unchanged inside a small Atari ST built into this program, plus a few optional
improvements listed below.

The games are (c) Jeff Minter / Llamasoft. Llamatron, Trip-A-Tron and Revenge were shareware ("please spread
this game everywhere"); the disks in the "game" folder come from Jeff Minter's own site. Some arrived on
crack compilation disks: only the Llamasoft games' own files were kept (the cracks' trainers are offered as
options instead).


SETTING UP
----------
Unpack the zip anywhere and run llamatron.exe. Nothing to install. The games are the disk images (and
Trip-A-Tron's folder) in the "game" folder.

Settings, high scores and a log file are kept in the "userdata" folder next to the program (each game other
than Llamatron and Gridrunner in a folder of its own there).

Steam (and the Steam Deck): on the Settings disk, GENERAL, ADD TO STEAM puts the program in your Steam
library as a game, with its own library art (the pictures are in Extras\Steam art). On Windows, close Steam
first if it isn't there yet. On a Steam Deck, add llamatron.exe as a non-Steam game in desktop mode (Proton),
start it from Steam, then ADD TO STEAM gives that shortcut its art. Steam shows the art once restarted.

CRT shaders for other games: Extras\CRT shaders has the CRT+ and CRT++ looks as RetroArch slang presets, for
ShaderGlass (Shader, Import custom) or RetroArch (copy the folder into shaders\shaders_slang). Every setting is
a shader parameter; their README.md explains them. MIT licence.

More disks: put any Atari ST disk image (.msa, .st or .stx) in the "game" folder and it appears as a disk of
its own on the start screen, named after the file, with simple pad controls (left stick and A = the
joystick, right stick = the mouse, RT / LT = its buttons, Start = Space, B = Esc, Back = settings). If it
has several programs on it, pick one on its how-to-play page. A picture named like the image (.png) next to
it becomes its label.


THE START SCREEN
----------------
The disks on a carousel: pick a game.
  Left / right    turn the carousel (LB / RB and the mouse wheel too)
  A or Start      play the disk in the middle
  Y               how to play (and its options: trainers, which program)
  X               the game's docs, where there are some: the disk's own notes or reader, Gridrunner's
                  GAME SECRETS pages (A next page, B back), Trip-A-Tron's manual (the PDF is in Extras\),
                  Andes Attack: Yak's essay on its reviews, from Gridrunner, Photon Storm's manual
                  (typed in by Parasite of LSD; game\docs). In the text viewer: up /
                  down scroll, LB / RB or the right stick turn pages, B back
  B, B            quit
Keyboard: cursor keys, Enter, H = how to play, X = docs, F9 = CRT look, Esc twice = quit.


SETTINGS
--------
The last disk on the carousel, and Back (keyboard F1) in any game: everything worth changing - the remaster,
run-ahead, the CRT look and its sliders, fullscreen, and each game's own controls. In a game the game waits
while they're up and the picture behind shows what a change does.
  Up / down       pick                      Left / right    change (hold for sliders)
  A               toggle                    X               back to the default
  LB / RB         the previous / next section
  Back            carry on with the game    B               leave the game, back to the disks
Changes are saved at once. A * marks the ones that take effect when the game next starts.
Sections marked [+] / [-] fold away: A on the heading (or right / left) opens and closes them.
CONTROLS (in a game, Back): the game's functions with the pad buttons on each. A on a row, then press a
button: it moves to that function (pressing one already on the row takes it off). X puts a row's own buttons
back; RESET CONTROLS all of them. Per game; Start and Back stay as they are; the sticks aren't remapped.
In every game Start is the game's own pause (Defender 2 has none: Start freezes it).


LLAMATRON CONTROLS - XBOX PAD
-----------------------------
Title screen:
  A               start a twin-stick game (left stick moves, right stick shoots - "Robotron style")
  X               start a classic one-stick game (shots go the way you move, A locks the fire angle)
  Left / right    choose 1 PLAYER, PLAYER + DROID or TEAM (TEAM: two players, plug in a second pad)
  Up / down       1 or 2
  RB              analog aim on / off (twin-stick shots in any direction instead of 8; off by default)

In the game:
  Left stick      move
  Right stick     shoot (twin-stick mode)
  A               fire / lock the fire angle (one-stick mode)
  Start           pause

Pause screen:
  Start           resume
  Y               the Ozric tentacle on / off (play with it using the left stick and A)
  B               quit to the title screen

A second pad plays player 2 in TEAM mode.


LLAMATRON CONTROLS - KEYBOARD
-----------------------------
  W A S D, Space      stick 1 (move) and its fire
  Cursor keys, Enter  stick 2 (shoot in twin-stick mode) and its fire
  P / O               pause / resume (and on the pause screen: H 50/60 Hz, S samples on/off, T tentacle, G back)
  X                   (in play) quit to the title screen
  Esc                 pause screen: quit to title
  F1                  settings (Esc there: back to the disks)
  F11 or Alt+Enter    fullscreen / window
  F9                  CRT look: off / CRT+ / CRT++
  F8                  the remaster (4x, interpolated unlocked frame rate) on / off - every game but Trip-A-Tron
  F7                  run-ahead 0-3 frames with the remaster (default 2: quickest response)


SUPER GRIDRUNNER CONTROLS - XBOX PAD
------------------------------------
In the game:
  Left stick      move the ship (the further you push, the faster; d-pad left / right: slowly)
  RT or A         fire
  LT or X (hold)  call the nosecone back to the ship
  RB / LB         turn the nosecone's shot direction one step up / down (d-pad up / down too)
  Right stick     the same: flick up or down
  Y               voluntary martyrdom (costs a life)
  Start           pause; B on the pause screen quits to the title

Title screen:
  A               play (on the level select: d-pad left / right picks the level, up / down moves between
                  the buttons, A presses the one you're on; levels 1-4
                  to begin with, then up to the furthest level you've reached - it unlocks when you die there)
  X               options (d-pad up / down, A changes, B back)
  Y               game secrets
  LB              more info

Name entry (a high score): d-pad picks a letter, A types it, B deletes, Start is OK. Typing on the keyboard
works too.

The mouse plays it the original way: move the ship with the mouse, the buttons fire.


TRIP-A-TRON - XBOX PAD
----------------------
Played like an instrument. The basics:
  Left stick      move the light source           Right stick     spirograph: the stick is the position
  The starfields and lasers are in the other demo: press Start to load it, then pick from the A or Z row
  (effect wheel or d-pad) for starfields, or B for the lasers on the number row; Start again goes back.
  While a starfield is running the right stick flies it:
                  push up to speed into the stars, pull back to reverse, left / right to spin, RT to boost;
                  let go and it settles back to its own speed
  RT              paint - how hard you squeeze sets how thickly patterns flow (light = sparse pulses)
  LT              the right mouse button (what it does is set in Global Parameters: VECTOR locks the light
                  into a dead-straight line, CENTRE brings it back to the middle)
  R3              latch painting on / off         D-pad U / D     previous / next symmetry
  D-pad L / R     previous / next effect (the keyboard's Q, A and Z rows)
  LB / RB (tap)   previous / next palette         A               surprise me (effect, symmetry, palette)
  X               stop all patterns (panic)       B               the number row in turn (palettes;
  Start           the other demo (starfields)                     the lasers in the starfield demo)
  L3 (or F12)     music: off / pulse / full - it listens to whatever your PC is playing
  Back            settings (and leaving, back to the disks)
Hold to choose (a picture shows the choices while you hold):
  LB held         the effect wheel: point the right stick at an effect, let go of LB to choose it;
                  the d-pad left / right changes keyboard row. Each key is labelled with what it does in the
                  demo that's loaded (read from its KML program); greyed keys do nothing there
  RB held         the symmetry wheel, the same way; hold Y too and it sets the pre-symmetry instead
  Y               a tap: the Control Panel up / down. Held: the plot routine (each stage shown on / off) - LB pre-symmetry, A remap,
                  X cube, B shear, Start resize, RB post-symmetry on / off (F1-F6); d-pad up / down steps
                  the pre-symmetry
On the Control Panel the d-pad jumps from icon to icon and names it, with what A (left button) and X (right
button) do there; the sticks are the mouse (LB held: slowly, for the small buttons; in the tool screens the
d-pad nudges a pixel at a time). B goes back: out of any screen (Sillyscope, Colour Cooker, the sequencers,
Disk...) to the Control Panel, closes an alert or a Sym / Cube panel, and on the bare panel puts it away.
Settings (Back) lists every control (TRIP-A-TRON CONTROLS: pick a row for the details) and has its feel: light speed, stick curve, FLOW (inertia for long glides), paint pressure on /
off, GYRO (turn the pad to move the light, like a pen - Steam Deck, PlayStation and Switch pads) and a rumble
on the beat in music mode.
Keyboard and mouse work as on the ST (F1-F6 are its own: the settings are on the pad's Back only). The first
time it installs itself into userdata\tripatron (a few seconds). Its README is on the how-to-play page.


REVENGE OF THE MUTANT CAMELS - XBOX PAD
---------------------------------------
  Left stick / d-pad   move the camel             A, X, RT        fire
  Start                pause                      B (paused)      back to the menu
  A second pad         player 2 (the goat) in the two-player game
Keyboard: cursor keys + Space, P pause, O resume. Yak's notes are on the how-to-play page.
The TT Megamix plays the same way. It was the shareware release: its shareware screen comes first (fire
goes on).


ANDES ATTACK - XBOX PAD
-----------------------
  Left stick up / down   altitude                 Left stick left / right   face that way
  LT                     thrust: how far you squeeze sets the speed (A: full thrust)
  RT                     fire                     B                         smart bomb
  X or Y                 reverse
  LB or RB (hold)        shield (its energy drains while it's up)
  Start                  pause
  (Settings: CONTROLS = STICK THRUST: the stick's left / right thrusts that way, the older scheme.)
  High score name: up / down picks a letter, A (or RT, Start) enters it.
Mouse and keyboard as the original: mouse up / down, left button thrust, right reverse, Left Shift fire,
Control smart bomb, Alt (hold) shield, Right Shift pause, Space resume.


DEFENDER 2 - XBOX PAD
---------------------
  Title: the d-pad picks Defender, Defender II or Stargate, A (or RT, Start) starts it; then A = one player.
  Left stick up / down   climb / dive             Left stick left / right   face that way
  LT                     thrust: how far you squeeze sets the speed (A: full thrust)
  RT                     fire                     B                         smart bomb
  X or Y                 reverse
  LB                     inviso (Stargate)        RB                        ship type (Defender II)
  (Settings: CONTROLS = STICK THRUST: the stick's left / right thrusts that way, the older scheme.)
  Start                  pause (freezes it: the game has no pause of its own)
  Initials (a high score): the d-pad picks the letter, A fixes it, X deletes.
The crack's trainer (invincibility, friendly sprites) is on the how-to-play page, and its notes. It runs in
its 60 Hz mode (as the arcade did; the game adjusts its speeds), on a 60 / 120 Hz display mode in fullscreen.


PHOTON STORM - XBOX PAD
-----------------------
  Left stick       steer the ship (push far: fly)  Right stick     fire in any direction
  A                fire where the ship points      X               thrust
  RT               smart bomb                      LT (hold)       boost (while its gauge lasts)
  LB / RB (hold)   single shots instead of the three-way spread
  Start            pause                           B (paused)      quit to the title
  Title: A = one player, X = two players, Y = controls: 360 twin stick (the default: aim, flight and shots
  at any angle; the ship's picture still has its 16 steps) or the original 16 directions (one stick).
Mouse as the original: where it points from the middle is the direction; left button fire, right thrust.
Control smart bomb, Left Shift boost, Alt single shots. Right Shift pauses (Space resumes, Q quits).


HARDCORE - XBOX PAD
-------------------
The guns ride the four edges of the field and fire by themselves; the puck ricochets around.
  Left stick       move the guns                  Right stick     put them at that spot
  A or RT          side lasers                    Y               puck intelligence on / off
  Start            pause
Keyboard: P pause, O resume, S strobes, N next level. Yak's notes for its testers are on the how-to-play page.


THE REMASTER: WHAT'S BETTER, AND WHAT ISN'T CHANGED
---------------------------------------------------
Every game here is the original program, byte for byte, running in a small Atari ST inside this app. The
waves, enemies, scoring, sound and music are the originals', worked out the way they were in 1991. The
improvements sit on top of the game and leave it alone. We checked: with the remaster on, the game's memory
is the same as without it, and its picture at the ST's own resolution matches pixel for pixel.

What's better - the headlines:
- CRT+ and CRT++ (F9 or Settings: off / CRT+ / CRT++). CRT+ is an accurate, beautiful CRT built for these
  games: a soft beam with scanlines that swell on bright colours, an aperture-grille phosphor mask, real
  halation glow and true blacks, the picture keeping its size, in the shape it had on a UK TV in 1991.
  CRT++ is a Llamasoft-inspired facelift: psychedelic feedback trails that zoom, swirl and colour-cycle
  behind the action, with composite colour bleed and a boosted glow. Each game has its own tuned look, and
  you can tune it further in Settings. Extras\CRT shaders has both looks for ShaderGlass and RetroArch.
- An interpolated, unlocked frame rate (the remaster: on by default; F8 or Settings). During play the ST's
  processor gets more power, so every game draws all 50 of its frames a second instead of 13-25, at exactly
  its original speed. Then moving things are drawn at 4x the ST's resolution and interpolated between the
  game's frames at your monitor's full refresh rate - 60, 120, 144, 240 Hz or more: the only limit is your
  display. Title screens, attract demos, scrollers, bonus stages and high score entry too (every game but
  Trip-A-Tron, which paints straight into the screen). Photon Storm's ship turns at its exact angle instead
  of the ST's 16 steps; Llamatron's analog shots fly at their true positions.
- Pad controls tuned by hand for every game: each designed for that game, play-tested on a real pad and the
  Steam Deck and tuned until it felt right (see each game above). Every button can be remapped (Settings in
  a game, CONTROLS); button names follow your pad (Xbox, PlayStation, Switch).
- Trip-A-Tron, fully working - its tool screens, KML programs and the whole manual - played like an
  instrument, with our music-reactive module (L3): it listens to whatever your PC is playing and performs
  along with it, while your own input always wins.

And:
- Run-ahead (F7 or Settings, default 2 frames): a copy of the game runs a frame or two ahead with your
  controls, so the picture answers the pad quicker than the original did on a real ST.
- Rumble from the game itself: a lost life, a smart bomb, every kill (Settings, GENERAL, RUMBLE).
- Even volume: every game is brought to the same loudness, with a master volume (Settings, GENERAL).
- No screen flashes: the white flash on every kill and the red and multicolour strobes are removed (they can
  be uncomfortable or a risk for photosensitive players). Trip-A-Tron's flashing palettes (4, 5, 6) pulse
  gently instead; Revenge's flashes are softened; Hardcore starts with its own strobes off.
- Fullscreen is borderless (Alt+Enter or F11 for a window; Alt+Tab without the screen blanking). Variable
  refresh (G-Sync / FreeSync) screens show every frame evenly; fullscreen_exclusive=1 picks a refresh rate
  that is a multiple of the game's 50 Hz for even motion on screens without it.

What we changed on purpose, and how to get the original back:
- 50 frames a second ............... --cpu=1, or grid_50fps=0, d2_50fps=0, photonstorm_50fps=0
- the remaster, run-ahead, CRT .... F8, F7 (0), F9 (off), or Settings
- no flashes ....................... --flashes, trip_calm_background=0, hc_strobes=1
- Photon Storm at any angle ........ Y on its title: the original 16 directions
- Llamatron analog aim ............. off unless you turn it on (RB on its title)
- Andes Attack / Defender 2 arcade controls ... Settings, CONTROL SCHEME; the mouse and keys work as on the ST
- Gridrunner: snakes waiting under the top rail show 1.5 seconds before they drop (in the original they lurk
  half-hidden, out of reach of your shots) ... --entry-lane
- Gridrunner: the level select unlocks the level you died on (the original: the one before) ...
  grid_unlock_level_died=0
- Defender 2 runs in its own 60 Hz mode, as the arcade did ... defender2_60hz=0
- Photon Storm no longer crashes in the warp after a won bonus (the original divides by zero about one warp
  in ten): a fix, not a choice.


SETTINGS (userdata\settings.txt)
--------------------------------
Written on the first run with every setting and its default. Edit it while the game runs: it's reread.
  fullscreen_exclusive    1 = fullscreen switches the display to a multiple of the game's refresh rate
                          (even motion without G-Sync / FreeSync; the screen blinks when switching away)
  remaster                1 = the remaster (4x, interpolated frame rate), 0 = the ST's own picture
  remaster_ahead          run-ahead with the remaster: 0..3 frames (2)
  crt                     0 = off, 1 = CRT+, 2 = CRT++ (each game can have its own: <game>_crt)
  volume, volume_normalise    master volume (0..1) and the even loudness across games (1 = on)
  rumble                  1 = pad rumble from the game's events
  button_names            0 = from your pad, 1 Xbox, 2 PlayStation, 3 Nintendo
  crt_scanlines           how dark the gaps between lines are (0..1)
  crt_beam_min / _max     line thickness for dark / bright colours
  crt_blur                horizontal softness (0 = sharp)
  crt_mask, crt_mask_bleed    the RGB phosphor stripes and how much they blend
  crt_halation, crt_halation_size   the glow around bright things
  crt_curvature           a curved tube (0 = flat; the edges stay in place)
  crt_contrast, crt_brightness, crt_saturation, crt_temperature, crt_overflow   colour
  analog_aim              Llamatron's analog aim (also RB on its title screen)
  grid_stick_speed, grid_stick_curve, grid_stick_deadzone, grid_dpad_speed   Gridrunner's ship movement
  grid_stick_smooth, grid_stick_stop   a light inertia: frames to speed up / to stop (0 = instant)
  (Most of these are on the Settings disk too.)
  grid_fire_mode_swap     1 = flip which way a right-stick flick turns the nosecone shot
  grid_50fps              1 = Gridrunner at 50 frames a second (0 = the original)
  grid_unlock_level_died  1 = the level select unlocks the level you died on (0 = the original: the one before,
                          from level 6)
  d2_50fps                1 = Defender 2 at 50 (60) frames a second (0 = the original 25 / 30)
  defender2_60hz          1 = Defender 2 in its 60 Hz mode, as the arcade (its speeds adjusted); 0 = 50 Hz
  hc_strobes              1 = keep Hardcore's strobes on
  andes_direct_thrust     1 = Andes Attack's speed follows how far the stick is pushed (experimental);
                          0 = the original build-up and glide (the default)
  andes_altitude          Andes Attack's altitude: 1 = original (default: the stick is the mouse), 0 = direct (the stick
                          moves the ship at andes_stick_speed); also in Settings
  andes_stick_speed, andes_stick_curve   Andes Attack's climb / dive speed (3 px a frame at full) and curve
  andes_thrust_ease       with andes_direct_thrust: how quickly the speed follows (0.5 = full in ~0.8 s)
  photonstorm_50fps       1 = Photon Storm at 50 frames a second (0 = the original 25)
  ps_analog               1 = Photon Storm's 360 twin stick controls (0 = the original 16 directions; Y at its title)
  trip_calm_background    0 = Trip-A-Tron's palettes flash as originally
  trip_music              Trip-A-Tron's music mode (0 off, 1 pulse, 2 full; also L3 / F12)
  trip_stick_speed, trip_stick_curve, trip_stick_deadzone, trip_spiro_size   Trip-A-Tron's sticks
  trip_flow (0..1), trip_pressure, trip_gyro, trip_gyro_speed, trip_rumble   Trip-A-Tron's feel
The CRT look needs a Direct3D 12 graphics card (any from the last ten years); without one the picture is
shown plain.


COMMAND LINE OPTIONS
--------------------
  --window              start in a window
  --flashes             keep the original screen flashes
  --aspect=square       square pixels (16:10) instead of the TV shape
  --refresh=desktop     with fullscreen_exclusive=1: keep the desktop's refresh rate
  --cpu=1               the original processor speed during play (the original stutter)
  --aim=8 / --aim=analog   aiming for this run only
  --disk=<file>         use this disk image
  --program=GRIDRUNR.PRG   start Super Gridrunner directly, no start screen (also TRON1MEG.PRG, TRIP.PRG,
                        REVSTE1M.TOS, ANDES.BOY, DEFENDR2.PRG, HARDCORE.PRG, PHOTON.YAK)
  --entry-lane          Super Gridrunner: the original entry lane (snakes hidden until they come down)


CREDITS
-------
Llamatron, Super Gridrunner, Trip-A-Tron, Revenge of the Mutant Camels, Andes Attack, Hardcore: Jeff Minter
(yak) / Llamasoft. Defender 2: Jeff Minter for Arcadia, 1990. Photon Storm: Jeff Minter / Arc, 1990.
This PC version uses SDL 3, the Musashi 68000 emulator and SoftFloat (see licenses\THIRD-PARTY-NOTICES.txt).
The remaster and the CRT shaders are this project's own. The disk labels on the start screen were
hand-written by the author. Trip-A-Tron's manual: James "Piku" Grimwood's 2003 transcription. Photon
Storm's manual: typed in by Parasite of LSD.

Thanks, Yak.
