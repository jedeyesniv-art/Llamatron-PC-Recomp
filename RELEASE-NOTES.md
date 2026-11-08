# Release notes (draft for the next release, 2.0.0: not published yet)

Paste the text below the line into the GitHub release. Before then, change the version in `CMakeLists.txt` and
build the zip with `Tools\package_portable.ps1 -Release`.

---

## Llamasoft on the ST 2.0: the remaster

**The same games, byte for byte, made to look and feel the way you remember them, only better.**

Every game is still the original 68000 program running in a small Atari ST. What's new is everything layered
on top:

### Smooth at any refresh rate
Moving things are now drawn at **4x the ST's resolution** and slide between the game's frames at your
display's own rate: 60, 120 or 144 Hz. It's not just play: title screens, attract demos, scrollers, bonus
stages, warps and high score entry too. Photon Storm's ship turns at its exact angle instead of the ST's 16
steps. The game underneath is untouched: its memory with the remaster on matches a run without it, and its
picture at the ST's own resolution matches pixel for pixel.

### Quicker than the real ST
**Run-ahead** (on by default) runs a copy of the game a frame or two ahead with your controls. Measured on
Photon Storm, the picture now answers the pad about 40 ms sooner than the original did on its own hardware.

### CRT+ and CRT++
A CRT look built for these games: a soft beam, scanlines that swell on bright colours, a phosphor mask and a
real halation glow. **CRT++** adds Minter-style feedback trails, with colour that streams and swirls behind
everything. Each game has its own tuned look. Both looks also ship as presets for **ShaderGlass and
RetroArch** (`Extras\CRT shaders`), so you can use them on everything else you play.

### Made for a pad
- **Andes Attack and Defender 2:** arcade Defender controls, with analog thrust on the trigger.
- **Mutant Camels:** spit wherever the right stick points.
- **Photon Storm:** the bomb, boost and single-shot keys are on the pad.
- **Trip-A-Tron:** played like an instrument:
  - pressure-sensitive painting on the trigger
  - effect and symmetry wheels on the shoulders
  - a starfield you fly with the right stick
  - effect names read from the loaded program
- **Remapping:** every button can be remapped per game.
- **Button names:** controls are shown in Xbox, PlayStation or Switch terms.
- **Rumble:** driven by the game itself: lives lost, smart bombs, kills.

### And more
- **Start screen:** the game you're pointing at plays live behind the carousel.
- **Settings panel:** reachable from any game, with the game paused behind it.
- **Docs:** each game's notes and manuals are one button away. Trip-A-Tron's full manual is included, and so is
  Photon Storm's.
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
