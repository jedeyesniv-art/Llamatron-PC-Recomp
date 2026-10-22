// Host input -> the ST's two joysticks and keyboard.
//
// Llamatron reads joystick 1 (the joystick port) to move; in its two-joystick mode joystick 0 (the mouse port)
// aims the fire direction - twin-stick controls. On an Xbox pad:
//   left stick / d-pad  = joystick 1 directions     A = joystick 1 fire (starts a one-stick game)
//   right stick         = joystick 0 directions     Start, RB, RT = joystick 0 fire (starts a twin-stick game)
//   Back                = pause / unpause (the game's P and O keys)
// On the title screen joystick 1 left/right picks 1 PLAYER / PLAYER + DROID / TEAM, up/down 1 or 2 players.
// A second pad, if connected, drives joystick 0 entirely (left stick + A), for two players (TEAM); then pad 1's
// Start is joystick 1 fire.
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

}  // namespace input
