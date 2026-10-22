LLAMATRON - PC
==============

Jeff Minter's Llamatron: 2112 (Atari ST, Llamasoft 1991) and Super Gridrunner, the bonus game on the same
disk, running natively on Windows with modern pad controls. The original game code runs unchanged inside a
small Atari ST built into this program, plus a few optional improvements listed below.

Llamatron is (c) 1991 Jeff Minter / Llamasoft. It was released as shareware; this program contains no part
of the games - it plays your own copy of the game disk. The disk image is on Jeff Minter's site.


SETTING UP
----------
Put your Llamatron disk image (the .msa or .st file of the Atari ST disk) into the "game" folder next to
llamatron.exe, then run llamatron.exe. Nothing to install.

Settings, high scores (SCORES.YAK, HISCORES.COW) and a log file are kept in the "userdata" folder next to the
program.


THE START SCREEN
----------------
Two floppy disks: pick a game.
  Left / right    choose Llamatron or Super Gridrunner
  A or Start      play it
  Y               how to play
  X               CRT look on / off (also F9, here and in the games)
  B, B            quit
From either game, press B twice on its title screen to come back to the disks.
Keyboard: cursor keys, Enter, H = how to play, C = CRT, Esc twice = quit.


LLAMATRON CONTROLS - XBOX PAD
-----------------------------
Title screen:
  A               start a twin-stick game (left stick moves, right stick shoots - "Robotron style")
  X               start a classic one-stick game (shots go the way you move, A locks the fire angle)
  Left / right    choose 1 PLAYER, PLAYER + DROID or TEAM (TEAM: two players, plug in a second pad)
  Up / down       1 or 2
  RB              analog aim on / off (twin-stick shots in any direction instead of 8; off by default)
  B, B            back to the start screen

In the game:
  Left stick      move
  Right stick     shoot (twin-stick mode)
  A               fire / lock the fire angle (one-stick mode)
  Back            pause

Pause screen:
  Back            resume
  Y               the Ozric tentacle on / off (play with it using the left stick and A)
  B               quit to the title screen

A second pad plays player 2 in TEAM mode.


LLAMATRON CONTROLS - KEYBOARD
-----------------------------
  W A S D, Space      stick 1 (move) and its fire
  Cursor keys, Enter  stick 2 (shoot in twin-stick mode) and its fire
  P / O               pause / resume (and on the pause screen: H 50/60 Hz, S samples on/off, T tentacle, G back)
  X                   (in play) quit to the title screen
  Esc                 pause screen: quit to title; title screen: quit (press twice)
  F11 or Alt+Enter    fullscreen / window
  F9                  CRT look on / off


SUPER GRIDRUNNER CONTROLS - XBOX PAD
------------------------------------
In the game:
  Left stick      move the ship (the further you push, the faster; d-pad: slowly)
  RT or A         fire
  LT (hold)       call the nosecone back to the ship
  Right stick     flick up or down to change the nosecone's shot direction
  RB              voluntary martyrdom (costs a life)
  Back or Start   pause; B on the pause screen quits to the title

Title screen:
  A               play (on the level select: d-pad left / right picks the level, A blasts off)
  X               options (d-pad up / down, A changes, B back)
  Y               game secrets
  LB              more info
  B, B            back to the start screen

Name entry (a high score): d-pad picks a letter, A types it, B deletes, Start is OK. Typing on the keyboard
works too.

The mouse plays it the original way: move the ship with the mouse, the buttons fire.


IMPROVEMENTS OVER THE ORIGINAL
------------------------------
- Smooth: during play the ST's processor runs faster, so the screen updates 50 times a second instead of
  16-25 (the busy waves used to stutter badly). The game itself runs at exactly its original speed.
- Fullscreen picks a refresh rate that is a multiple of the game's 50 Hz (100 Hz on a 120 Hz screen, 50 Hz on
  60 Hz screens that offer it), so every frame is shown evenly. Variable refresh (G-Sync / FreeSync) screens
  are smooth too.
- No screen flashes: the white flash on every kill and the red and multicolour strobes are removed (they can
  be uncomfortable or a risk for photosensitive players). Gameplay is unchanged.
- The picture has the shape it had on a UK TV in 1991.
- Optional analog aim (RB on the title screen).
- Super Gridrunner: smooth 50 frames a second; snakes waiting in the entry lane at the top show up 1.5
  seconds before they come down, instead of appearing out of nowhere (--entry-lane keeps the original).
- CRT look (on by default; X on the start screen or F9 to switch): scanlines, a phosphor mask and a faint
  glow, with true blacks. The picture keeps its size and shape.


SETTINGS (userdata\settings.txt)
--------------------------------
Written on the first run with every setting and its default. Edit it while the game runs: it's reread.
  crt                     1 = CRT look on, 0 = off
  crt_scanlines           how dark the gaps between lines are (0..1)
  crt_beam_min / _max     line thickness for dark / bright colours
  crt_blur                horizontal softness (0 = sharp)
  crt_mask, crt_mask_bleed    the RGB phosphor stripes and how much they blend
  crt_halation, crt_halation_size   the glow around bright things
  crt_curvature           a curved tube (0 = flat; the edges stay in place)
  crt_contrast, crt_brightness, crt_saturation, crt_temperature, crt_overflow   colour
  analog_aim              Llamatron's analog aim (also RB on its title screen)
  grid_stick_speed, grid_stick_curve, grid_stick_deadzone, grid_dpad_speed   Gridrunner's ship movement
  grid_fire_mode_swap     1 = flip which way a right-stick flick turns the nosecone shot
  grid_50fps              1 = Gridrunner at 50 frames a second (0 = the original)
The CRT look needs a Direct3D 12 graphics card (any from the last ten years); without one the picture is
shown plain.


COMMAND LINE OPTIONS
--------------------
  --window              start in a window
  --flashes             keep the original screen flashes
  --aspect=square       square pixels (16:10) instead of the TV shape
  --refresh=desktop     fullscreen at the desktop's refresh rate
  --cpu=1               the original processor speed during play (the original stutter)
  --aim=8 / --aim=analog   aiming for this run only
  --disk=<file>         use this disk image
  --program=GRIDRUNR.PRG   start Super Gridrunner directly (TRON1MEG.PRG: Llamatron), no start screen
  --entry-lane          Super Gridrunner: the original entry lane (snakes hidden until they come down)


CREDITS
-------
Llamatron: Jeff Minter (yak) / Llamasoft, 1991.
This PC version uses SDL 3, the Musashi 68000 emulator and SoftFloat (see licenses\THIRD-PARTY-NOTICES.txt).
The CRT shader is this project's own. The disk labels on the start screen were hand-written by the author.
