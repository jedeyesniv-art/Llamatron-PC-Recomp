// Pad 1 as a snapshot per frame - the real pad, or a scripted one (tests: --press=N:g...) - with button edges,
// for the per-game controls (rmc.cpp, andes.cpp, defender2.cpp, hardcore.cpp, generic).
#pragma once

#include <SDL3/SDL.h>

#include <cstdint>

namespace pad {

struct State {
  bool connected = false;
  float lx = 0, ly = 0, rx = 0, ry = 0;  // -1..1, y down
  float lt = 0, rt = 0;                  // 0..1
  uint32_t buttons = 0;                  // bit = SDL_GamepadButton
  bool Held(SDL_GamepadButton b) const { return buttons >> b & 1; }
};

// Once per game frame: reads pad 1 (or the test pad set for this frame). Returns the snapshot.
const State& Poll(SDL_Gamepad* real);
const State& Now();
bool Pressed(SDL_GamepadButton b);   // went down this frame
bool Released(SDL_GamepadButton b);  // went up this frame
bool RtPressed();                    // the right trigger crossed half way this frame
bool LtPressed();
// Tests: the pad for the next Poll.
void SetTest(const State& s);
void Reset();

// The stick (or d-pad) as joystick bits (st::kJoyUp...), 8 directions, dead zone 0.35.
uint8_t JoyBits(const State& s, bool right_stick = false);

}  // namespace pad
