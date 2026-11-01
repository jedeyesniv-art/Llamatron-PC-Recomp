# Plan: the next level - run-ahead, then native "remaster" rendering

Written 2026-11-01, after 1.5.0 (Photon Storm 360-degree twin stick). The owner's next major step, after any
bug fixes from their own testing. It records the discussion and the decisions so far; nothing here is built yet.
Keep it up to date as the work goes, and delete it when done.

## Where we are (why this, not "60 fps")
- Every game moves things a fixed number of whole pixels per 50 Hz tick, with the logic in the VBL. On a VRR
  screen we already show 50 distinct, evenly spaced frames a second: as smooth as the games' own motion allows.
- True 60 Hz is not worth chasing: running the logic at 60 makes the game 20% faster (Llamatron's and
  Hardcore's own 60 Hz modes do that); rescaling every speed by 5/6 means rewriting movement, collisions and
  timings into fractional positions the code doesn't have. Fixed 60 Hz screens without VRR keep a 5-in-6
  hitch; that's unavoidable without the renderer below.
- Static recompilation: no. The interpreter costs ~2.5% of a core; recompiling adds risk (self-modifying code,
  CPU-timed screens) and nothing the player can feel.

## Step 1: run-ahead (lower input lag in every game) - do first
- What: each frame, save the machine, run 1-2 frames ahead with the current input, show that frame, roll back
  (as RetroArch). Cuts 20-40 ms of pad-to-screen delay in every game, with no game code touched. For the
  twin-stick shooters a bigger change in feel than 50 -> 60 fps.
- We already have the pieces: Machine is copyable, cpu::SaveContext / RestoreContext / SetMachine, the
  sandbox (no picture, sound, files) used by Gridrunner's look-ahead; a frame costs a fraction of a ms.
- Open problems: sound must come from the real timeline, not the run-ahead copy; files (high score saves) only
  from the real machine; the faster CPU in play (x8) multiplies the cost (still small); games reading the pad
  in the main loop rather than the VBL may need checking.
- Measure first: input-to-screen latency with and without (a scripted press + the frame where the picture
  changes), per game. A setting (run_ahead = 0 / 1 / 2) so the owner can compare.

## Step 2: native remaster renderer, one game at a time - Photon Storm as the pilot
- What: instead of showing the ST's framebuffer as it is, read the game's object tables each tick and draw the
  moving objects ourselves, moved smoothly between ticks at the display's rate (100 / 120 / 144 Hz). A hybrid:
  the ST picture for backgrounds, HUD and text; the game's own drawing of the moving objects suppressed by
  hooks on its draw routines (proven: Gridrunner's entry-lane hook on $1655E skips drawing); ours on top.
- Buys: Photon Storm's ship at its exact angle (the analog heading is already known) instead of 16 steps;
  enemies and shots at 120 Hz; later Llamatron at 120 Hz with smooth shots; maybe the Camels' animation
  (6.25 Hz walk cycle is the artwork itself - a judgement call with the owner).
- Costs: each game's object inventory (every type, its sprite frames, special cases like Gridrunner's linked
  snakes); moving objects between ticks adds up to a frame of latency (run-ahead offsets it); teleports,
  wrap-around and explosions must not smear.
- Pilot: Photon Storm - the best understood after the analog work, the most visible win, and it shares the
  Andes Attack engine. Proof of concept first: the ship drawn at its exact angle + the enemies at 120 Hz;
  then decide with the owner whether to take it further. Then Llamatron (the flagship), on the plumbing.

## What carries over between games (estimate: first game 1, engine sibling 0.2-0.3, other Jeff game 0.4-0.5)
1. Infrastructure, fully reusable (well over half of the first game's work): compositing native sprites over the
   ST picture, interpolation with teleport / wrap detection, ST interleaved bitplane sprite decoding to
   textures, draw-suppression hooks, object-table inventory tools (as Tools\grid_objects.py).
2. Jeff's habits, seen in this project: logic in the VBL and drawing in the main loop (Llamatron, Gridrunner,
   Revenge, Hardcore, Defender 2, Photon Storm); a frame divisor in RAM (the 50 fps trick: Gridrunner, then
   Defender 2 and Photon Storm in minutes); colour flash lists into palette 0 (Llamatron, Revenge, Hardcore);
   a VBL hook pointer naming the screen (Revenge, Hardcore). Object tables likely alike: fixed-size slots, x / y
   first, a type field, -1 = free.
3. Game specifics, only within a family: Photon Storm <-> Andes Attack (same engine; input, state and mouse
   handler already proved alike); the three Revenge builds (same code, other addresses: one effort, three
   disks, as stgames::SetProgram); Llamatron / Gridrunner share only the style; Defender 2 and Hardcore stand
   alone; Trip-A-Tron doesn't apply (a light synth, no sprites).
