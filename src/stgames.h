// Pad controls for the other Llamasoft ST games (PLAN-ST-GAMES.md): Revenge of the Mutant Camels and Andes
// Attack. Each reads the game's state from its RAM, polls pad 1 (pad.h) and feeds the ST's joystick, mouse and
// keyboard. Keys from the PC keyboard go to the ST as they are (plus the cursor keys as a joystick in RMC).
//
// Revenge of the Mutant Camels (REVENGE.TOS): its VBL calls a per-state hook ($42E18): $1118E play (and the
// attract demo), $10526 pause, $10294 title, $107FA the menus. Keys (its own table, $42D91 flags): P pause,
// O resume, X abort to the menu (play only).
//   left stick / d-pad = joystick 1, A / X / RT / Start = fire; Back = pause / resume; B paused = back to the
//   menu; B twice elsewhere = back to the disks.
// Andes Attack (ANDES.BOY): state $12EEE (1 play, 3 paused, 5 title / demo). Mouse: y = altitude ($2B682),
// left button = thrust, right button = reverse (facing $1C088: 0 right, 1 left); Left Shift = fire; Right
// Shift = pause, Space = resume.
//   left stick up / down = altitude; left / right = thrust that way (pushing against the facing reverses first);
//   A / RT = fire, LT = thrust, X = reverse, Y = smart bomb (Left Control); Back = pause / resume; B twice =
//   back to the disks. Away from play
//   A / RT is the left mouse button (start).
#pragma once

#include <SDL3/SDL.h>

#include <cstdint>

#include "machine.h"

namespace stgames {

enum class Game { kRmc, kAndes };

void Start(Game g);
// Once per frame, after pad::Poll: pad and keyboard joystick -> the ST; the host mouse's movement and buttons
// (bit 1 left, bit 0 right) are added in.
void Update(st::Machine* m, int mouse_dx, int mouse_dy, uint8_t mouse_buttons);
// Keyboard events (Esc = B; RMC: cursor keys + Space as joystick 1). Returns true if used.
bool HandleKey(const SDL_Event& e, st::Machine* m);
const char* Hint(st::Machine* m, bool have_pad);
bool QuitRequested();
// The background's flashes toned down (Machine::SetCalmBackground) right now.
bool Calm(const st::Machine& m);

}  // namespace stgames
