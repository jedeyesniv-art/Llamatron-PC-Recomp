#include "pad.h"

#include <cmath>

#include "machine.h"

namespace pad {
namespace {

State g_now, g_prev, g_test;
bool g_have_test = false;

}  // namespace

const State& Poll(SDL_Gamepad* real) {
  g_prev = g_now;
  if (g_have_test) {
    g_now = g_test;
    g_now.connected = true;
    g_have_test = false;
  } else if (real) {
    State s;
    s.connected = true;
    auto axis = [&](SDL_GamepadAxis a) { return SDL_GetGamepadAxis(real, a) / 32767.0f; };
    s.lx = axis(SDL_GAMEPAD_AXIS_LEFTX), s.ly = axis(SDL_GAMEPAD_AXIS_LEFTY);
    s.rx = axis(SDL_GAMEPAD_AXIS_RIGHTX), s.ry = axis(SDL_GAMEPAD_AXIS_RIGHTY);
    s.lt = axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER), s.rt = axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
    for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT && b < 32; ++b)
      if (SDL_GetGamepadButton(real, SDL_GamepadButton(b))) s.buttons |= 1u << b;
    g_now = s;
  } else {
    g_now = State{};
  }
  return g_now;
}

const State& Now() { return g_now; }
bool Pressed(SDL_GamepadButton b) { return g_now.Held(b) && !g_prev.Held(b); }
bool Released(SDL_GamepadButton b) { return !g_now.Held(b) && g_prev.Held(b); }
bool RtPressed() { return g_now.rt > 0.5f && g_prev.rt <= 0.5f; }
bool LtPressed() { return g_now.lt > 0.5f && g_prev.lt <= 0.5f; }

void SetTest(const State& s) {
  g_test = s;
  g_have_test = true;
}

void Reset() {
  g_now = g_prev = State{};
  g_have_test = false;
}

uint8_t JoyBits(const State& s, bool right_stick) {
  float x = right_stick ? s.rx : s.lx, y = right_stick ? s.ry : s.ly;
  uint8_t b = 0;
  if (x * x + y * y >= 0.35f * 0.35f) {
    float angle = std::atan2(-y, x);  // up = +90 degrees
    int sector = int(std::lround(angle / (3.14159265f / 4))) & 7;  // 0 = right, counter-clockwise
    static const uint8_t kDir[8] = {st::kJoyRight, st::kJoyRight | st::kJoyUp,   st::kJoyUp,
                                    st::kJoyUp | st::kJoyLeft, st::kJoyLeft, st::kJoyLeft | st::kJoyDown,
                                    st::kJoyDown, st::kJoyDown | st::kJoyRight};
    b = kDir[sector];
  }
  if (!right_stick) {
    if (s.Held(SDL_GAMEPAD_BUTTON_DPAD_UP)) b |= st::kJoyUp;
    if (s.Held(SDL_GAMEPAD_BUTTON_DPAD_DOWN)) b |= st::kJoyDown;
    if (s.Held(SDL_GAMEPAD_BUTTON_DPAD_LEFT)) b |= st::kJoyLeft;
    if (s.Held(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) b |= st::kJoyRight;
  }
  if ((b & st::kJoyUp) && (b & st::kJoyDown)) b &= uint8_t(~(st::kJoyUp | st::kJoyDown));
  if ((b & st::kJoyLeft) && (b & st::kJoyRight)) b &= uint8_t(~(st::kJoyLeft | st::kJoyRight));
  return b;
}

}  // namespace pad
