# Release notes for 2.0.0 (draft: not published yet)

Paste the text below the line into the GitHub release. Before then, set `VERSION 2.0.0` in `CMakeLists.txt` and
build the zip with `Tools\package_portable.ps1 -Release`.

---

## Llamasoft on the ST 2.0 - Remaster

**The same games, byte for byte, made to look and feel the way you remember them, only better.**

Every game is still the original 68000 program running in a small Atari ST. What's new is everything layered
on top of it.

### CRT+ and CRT++
**CRT+** is an accurate, beautiful CRT built for these games:
- a soft beam, with scanlines that swell on bright colours
- an aperture-grille phosphor mask
- real halation glow, computed in linear light
- true blacks

The picture keeps its true size and its UK TV shape.

**CRT++** is a Llamasoft-inspired facelift. Minter-style feedback trails zoom, swirl and colour-cycle behind
the action, with composite colour bleed and a boosted glow. Each game ships with its own tuned look, and every
setting can be tweaked live.

Both looks also come as presets for **ShaderGlass and RetroArch** (`Extras\CRT shaders`, MIT), for everything
else you play.

### An interpolated, unlocked frame rate
Every game now draws all 50 of its frames a second (the ST managed 13-25), at exactly its original speed. On
top of that, moving things are drawn at **4x the ST's resolution** and **interpolated at your monitor's full
refresh rate**: 60, 120, 144, 240 Hz or more. The only limit is your display.

It covers title screens, attract demos, scrollers, bonus stages, warps and high score entry too. Photon Storm's
ship turns at its exact angle instead of the ST's 16 steps.

The game underneath is untouched: its memory with the remaster on matches a run without it, and its picture at
the ST's own resolution matches pixel for pixel.

### Pad controls, tuned by hand for every game
Every game has controls designed for it, play-tested on a real pad and the Steam Deck, and tuned until it felt
right. None of them is a generic joystick mapping.
- **Llamatron and Photon Storm:** true twin-stick shooters.
- **Andes Attack and Defender 2:** arcade Defender controls, with analog thrust on the trigger. Andes
  Attack's thrust builds frame for frame like the original's mouse.
- **Super Gridrunner:** the mouse game steered with the stick, with a light inertia.
- **Mutant Camels:** they spit wherever the right stick points.
- **Menus and high score names:** all on the pad.
- **Remapping:** every button, per game.
- **Button names:** in Xbox, PlayStation or Switch terms.
- **Rumble:** driven by the game itself: lives lost, smart bombs, kills.

### Trip-A-Tron, fully working, with a music-reactive module
Minter's light synthesiser runs complete:
- its tool screens, with their labels
- its KML effect programs
- its whole manual (text and PDF)

On a pad it's an instrument:
- pressure-sensitive painting on the trigger
- effect and symmetry wheels, every key labelled from the loaded program
- a starfield you fly with the right stick
- gyro aiming

Our **music module** listens to whatever your PC is playing and performs along with it. The light dances to
the bass, beats paint, and the palette, effects and symmetry move with the music. Your own input always wins.

### Quicker than the real ST
**Run-ahead** (on by default) runs a copy of the game a frame or two ahead with your controls. Measured on
Photon Storm, the picture now answers the pad about 40 ms sooner than the original did on its own hardware.

### And more
- **Start screen:** the game you're pointing at plays live behind the carousel.
- **Settings panel:** reachable from any game, with the game paused behind it.
- **Docs:** each game's notes and manuals are one button away.
- **Volume:** every game is evened out to the same loudness, with a master volume.
- **Steam:** **Add to Steam** puts it in your library with its own art. It plays on the Steam Deck.
- **Fix:** Photon Storm no longer crashes in the warp after a won bonus. That's a bug in the original, which
  divides by zero about one warp in ten.

### Respecting the originals
Nothing about how the games play has changed: enemies, waves, scoring, difficulty, sound and music are all
the originals'. Every deliberate change can be switched back, and the README lists each one with its switch.
Press **F8** at any time to see the ST's own picture.

**Download `LlamasoftOnTheST-2.0.0-windows.zip`, unpack it anywhere and run `llamatron.exe`.** All the games
are included; `README.txt` has every game's controls.

The collection: Llamatron: 2112 and Super Gridrunner, Trip-A-Tron, Revenge of the Mutant Camels (1 MB STE and
the TT Megamix), Andes Attack, Defender 2, Photon Storm and Hardcore.

`LlamasoftOnTheST-2.0.0-symbols.zip` is the debug PDB (only for crash reports).
