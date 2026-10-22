// Gridrunner (GRIDRUNR.PRG): what we know about its state, and its controls on a pad.
//
// The game is played with the mouse: the ship sits at the mouse pointer, left button = fire, right button =
// the nosecone (held: it comes back to the ship and they move as one, quad widebeams; released: it detaches).
// Keys: right Shift = pause; left Shift / Alt = the direction of the shots that come out of a detached
// nosecone (5 modes, up to down); Control = "voluntary martyrdom" (the ship explodes, losing a life, and takes
// the vulnerable enemies with it; its ghost fights on for a few seconds; lives counter $12D2A). The game's own
// manual ("game secrets") is text at $10EC3. All menus are point and click.
// On a pad:
//   left stick       moves the pointer (= the ship): speed from a response curve (settings: grid_stick_*)
//   d-pad            moves it slowly, for precision (grid_dpad_speed)
//   RT, A            left button (fire, click)        LT   right button (the nose)
//   right stick      flick up / down: nosecone shot direction one step (up..down)   RB   Control (martyrdom)
//   Back, Start      pause / resume                   B    pause: quit to the title; title: quit (twice)
// The point-and-click screens get shortcuts (virtual clicks: the pointer is put on the target and the left
// button pressed for a few frames):
//   title         A play, X options (left Shift + click), Y game secrets (Alt + click), LB more info (Control +
//                 click)
//   level select  d-pad left / right = [-] / [+], A = LET'S BLAST!!
//   name entry    d-pad = move from letter to letter, A = pick it, B = delete, Start = done; typing on the
//                 keyboard picks letters (Backspace = delete, Return = done)
//   options       d-pad up / down = row, A = select, B = back (its quit row)
#pragma once

#include <SDL3/SDL.h>

#include <cstdint>

#include "machine.h"

namespace grid {

// Game states ($4B632, dispatched from the VBL through the table at $12636).
enum State {
  kPlay = 1, kIntro = 3, kTitle = 5, kLevelSelect = 6, kNameEntry = 9, kOptions = 10, kPausing = 11, kPause = 12
};
int GameState(st::Machine& m);

// Rereads the grid_* settings (on start and when settings.txt changes).
void LoadSettings();

bool HandleEvent(const SDL_Event& e, st::Machine* m);
// Once per frame: pad 1 (may be null) and the host mouse's movement and buttons (bit 1 left, bit 0 right).
void Update(st::Machine* m, SDL_Gamepad* pad, int mouse_dx, int mouse_dy, uint8_t mouse_buttons);
const char* Hint(st::Machine* m, bool have_pad);
bool QuitRequested();
// Hide objects in the entry lane under the top rail until they can be shot (default on; --entry-lane = off).
void SetHideEntryLane(bool on);

// Tests: a scripted pad for the next Update (sticks -1..1, triggers 0..1, RB).
struct TestPad {
  float lx = 0, ly = 0, ry = 0, lt = 0, rt = 0;
  bool rb = false;
};
void SetTestPad(const TestPad& t);

}  // namespace grid
