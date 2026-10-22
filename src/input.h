// Host input -> the ST's two joysticks and keyboard.
//
// Llamatron reads joystick 1 (the joystick port) to move; in its two-joystick mode joystick 0 (the mouse port)
// aims the fire direction - twin-stick controls. On an Xbox pad:
//   left stick / d-pad  = joystick 1 directions     A = joystick 1 fire; but on the title screen A is joystick 0
//                                                     fire = starts the twin-stick game (where the mode allows)
//   right stick         = joystick 0 directions     X = joystick 1 fire (on the title: a one-stick game)
//                                                   Start, RB, RT = joystick 0 fire
//   Back                = pause / resume (the game's P and O keys)
//   Y                   = paused: the Ozric tentacle on / off (the game's T and G keys)
//   B                   = paused: quit to the title (the game's X key, which only works in play); title: quit
//                         the program (press twice). Keyboard: Esc.
// On the title screen joystick 1 left/right picks 1 PLAYER / PLAYER + DROID / TEAM, up/down 1 or 2 players.
// A second pad, if connected, drives joystick 0 entirely (left stick + A), for two players (TEAM); then pad 1's
// A, X and Start are joystick 1 fire.
// Keyboard: WASD = joystick 1, Space = its fire; cursor keys = joystick 0, Enter = its fire. Other keys go to
// the ST keyboard.
#pragma once

#include <SDL3/SDL.h>

#include <cstdint>

namespace st {
class Machine;
}

namespace input {

void Init();
void Shutdown();
// Handles an SDL event (gamepad hotplug, keys). Returns true if it was consumed.
bool HandleEvent(const SDL_Event& e, st::Machine* m);
// Sends the current joystick states to the machine (call once per frame).
void Update(st::Machine* m);
// The player asked to quit the program (B twice at the title).
bool QuitRequested();
// A line to show over the picture (pause screen help, quit confirmation), or null.
const char* Hint(st::Machine* m);

}  // namespace input
