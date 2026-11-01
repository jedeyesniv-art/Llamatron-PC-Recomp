# Llamasoft on the ST

Jeff Minter's Atari ST games - **Llamatron: 2112** and **Super Gridrunner**, **Trip-A-Tron**, **Revenge of
the Mutant Camels**, **Andes Attack**, **Defender 2**, **Photon Storm** and the unfinished **Hardcore** - as a native Windows app
with modern pad controls: twin-stick play on an Xbox controller, smooth 50 fps, flashes removed or toned down
and an optional CRT look. The original 68000 game code runs unchanged on the
[Musashi](https://github.com/kstenerud/Musashi) interpreter inside a small Atari ST (hardware and TOS emulated
at a high level), with SDL 3 for video, sound and input. A hobby project, not affiliated with or endorsed by
Jeff Minter or Llamasoft.

The release zip includes the games, from the disks Jeff Minter distributes on his site (Llamatron, Trip-A-Tron
and Revenge were shareware: "please spread this game everywhere"). Where they came on crack compilation disks,
only the Llamasoft games' own files are kept.

## Features

- Twin-stick "Robotron style" play on a pad (A on the title), or the classic one-stick mode (X); optional
  analog aim in any direction (RB on the title). TEAM mode with a second pad.
- Super Gridrunner with the stick moving the ship, triggers firing, the nosecone on the shoulder buttons, and
  pad-driven menus and name entry. The mouse still works the original way.
- Smooth: the ST's processor runs faster during play, so the screen updates 50 times a second instead of
  16-25. The game logic runs at exactly its original speed.
- Fullscreen at a refresh rate that is a multiple of 50 Hz, so every frame is shown evenly; VRR screens too.
- No screen flashes (the kill flash and strobes are removed; `--flashes` keeps them).
- Pad controls for every game (Trip-A-Tron's light synth on the sticks, with a music-reactive mode; the mouse
  games Andes Attack, Defender 2 and Hardcore on the sticks; menus and high score names on the d-pad).
- A start screen with the disks on a carousel, each game's how-to-play page and its docs; drop any other ST
  disk image (.msa, .st, .stx) into `game\` and it appears too.
- A CRT shader (F9). TOS console text, medium resolution, STE DMA sound and colours, Pasti .stx disks.

## Playing

Download the zip from [Releases](../../releases), unpack it anywhere and run `llamatron.exe`. The included
`README.txt` has the controls and options. Settings and high scores go to `userdata\` next to the exe.

## Building from source

Requirements: Windows, Visual Studio 2026 with the C++ workload and its Clang/LLVM tools (CMake and Ninja come with it), Python 3 (only for the tools).

```
git clone --recursive https://github.com/jedeyesniv-art/Llamatron-PC-Recomp
cd Llamatron-PC-Recomp
cmd /c build.bat
```

That gives `build\llamatron.exe`. It looks for the disk image in `game\` next to the exe, the exe's folder, or
the nearest `Private\` folder up the tree. `Tools\package_portable.ps1 -Release` builds the release zip.
`CLAUDE.md` has the detailed notes on the game's internals and the emulation.

## Licence

This project's code: MIT No Attribution (see `LICENSE`). Third-party code: SDL (zlib), Musashi (MIT-style),
SoftFloat 2b, ayumi levels (MIT), miniaudio (public domain / MIT-0) - see `licenses\THIRD-PARTY-NOTICES.txt`.
The games are (c) Jeff Minter / Llamasoft.
