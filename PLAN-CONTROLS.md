# Plan: per-game button remapping (agreed with the owner 2026-11-06; BUILT 2026-11-07 - see CLAUDE.md "Button
remapping"; triggers listed too, in-game only, not Trip-A-Tron / docs readers)

## What the owner asked for
- Remap the pad's **buttons** per game (the d-pad counts as buttons; not the sticks). **Per game only**, no shared
  default.
- In the settings panel, as a **list of the game's functions with the buttons attached** - a control listing that
  is edited on the fly.
- **Start and Back stay fixed** (never listed, never attachable).

## Panel (the game's section of src/settings_menu.cpp)
```
CONTROLS
  FIRE            A
  SMART BOMB      B
  REVERSE         X  Y
  SHIELD          LB  RB
  UP / DOWN / LEFT / RIGHT   (d-pad, a row each)
  RESET CONTROLS
```
- A on a row listens for one button press: that button is attached to this function (moved from another function
  if it was there); pressing a button already on the row takes it off. A function may have several buttons; a
  button belongs to at most one function.
- Takes effect at once, saved per game: `<id>_controls_<function>=a,x` (the Camels' STE and TT builds share
  `rmc`, as their panel section does).
- Triggers and sticks are not listed (analog uses: RT squeeze thrust in Andes / Defender 2). Could be added later as
  on/off buttons.

## Underneath
- Each function is one of the game's **own** (logical) buttons: FIRE = "whatever the game does with A". Physical X
  attached to FIRE acts as A everywhere in that game (menus, name entry too).
- A physical button that is a default of some function but now attached to none does nothing. Buttons no function
  lists (e.g. L3) keep doing nothing until attached.
- One remap applied on all three read paths, no per-game control code changes:
  1. pad.cpp `Poll` (the snapshot: Camels, Andes, Defender 2, Hardcore, Photon Storm, docs readers);
  2. direct `SDL_GetGamepadButton` reads -> a `pad::` helper (input.cpp: Llamatron + generic user disks;
     gridrunner.cpp; tripatron.cpp);
  3. `SDL_EVENT_GAMEPAD_BUTTON_*` events translated once in main's event loop before the games' handlers
     (input.cpp, gridrunner.cpp) - many-to-one maps can give duplicate downs / early ups: the handlers are
     edge-triggered, check them.
- The launcher and the settings panel read the physical buttons (no lock-out). Scripted test input (--press g...)
  bypasses the remap, so the smoke tests stay valid.

## Per-game function lists
Built from each game's how-to-play page (games.cpp `help`) and its control code: Llamatron, Gridrunner,
Trip-A-Tron, Camels (one set for both builds), Andes, Defender 2, Hardcore, Photon Storm (incl. SMART BOMB RT,
BOOST LT, SINGLE SHOT LB / RB - triggers as buttons there); generic user disks get
A / B / X / Y rows (joystick fire, Space, Esc...). Rows that only matter on one screen say so ("X: ONE-STICK START
(TITLE)"). Andes and Defender 2 have two control schemes (andes_controls / d2_controls): the list follows the one
selected.

## Caveats
- How-to-play pages and in-game hint lines name the default buttons: show "CONTROLS CHANGED" on the how-to-play page
  when a game has a remap; rewriting the names from the map can come later.

## Verify
- Smoke test (no remap) and a scripted pad run per game; then a remap per game (e.g. swap A / X) checked on the
  logical side; the panel's listen / move / remove / reset by --mpress / --menushot.
