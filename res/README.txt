LLAMATRON - PC
==============

Jeff Minter's Llamatron: 2112 (Atari ST, Llamasoft 1991), running natively on Windows with modern pad
controls. The original game code runs unchanged inside a small Atari ST built into this program, plus a few
optional improvements listed below.

Llamatron is (c) 1991 Jeff Minter / Llamasoft. It was released as shareware; this program contains no part
of the game - it plays your own copy of the game disk.


SETTING UP
----------
Put your Llamatron disk image (the .msa or .st file of the Atari ST disk) into the "game" folder next to
llamatron.exe, then run llamatron.exe. Nothing to install.

Settings, high scores (SCORES.YAK) and a log file are kept in the "userdata" folder next to the program.


CONTROLS - XBOX PAD
-------------------
Title screen:
  A               start a twin-stick game (left stick moves, right stick shoots - "Robotron style")
  X               start a classic one-stick game (shots go the way you move, A locks the fire angle)
  Left / right    choose 1 PLAYER, PLAYER + DROID or TEAM (TEAM: two players, plug in a second pad)
  Up / down       1 or 2
  RB              analog aim on / off (twin-stick shots in any direction instead of 8; off by default)
  B, B            quit

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


CONTROLS - KEYBOARD
-------------------
  W A S D, Space      stick 1 (move) and its fire
  Cursor keys, Enter  stick 2 (shoot in twin-stick mode) and its fire
  P / O               pause / resume (and on the pause screen: H 50/60 Hz, S samples on/off, T tentacle, G back)
  X                   (in play) quit to the title screen
  Esc                 pause screen: quit to title; title screen: quit (press twice)
  F11 or Alt+Enter    fullscreen / window


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


COMMAND LINE OPTIONS
--------------------
  --window              start in a window
  --flashes             keep the original screen flashes
  --aspect=square       square pixels (16:10) instead of the TV shape
  --refresh=desktop     fullscreen at the desktop's refresh rate
  --cpu=1               the original processor speed during play (the original stutter)
  --aim=8 / --aim=analog   aiming for this run only
  --disk=<file>         use this disk image


CREDITS
-------
Llamatron: Jeff Minter (yak) / Llamasoft, 1991.
This PC version uses SDL 3, the Musashi 68000 emulator and SoftFloat (see licenses\THIRD-PARTY-NOTICES.txt).
