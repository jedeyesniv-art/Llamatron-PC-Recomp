#include "gridrunner.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "log.h"
#include "settings.h"

namespace grid {
namespace {

// Gridrunner's variables (absolute: the program unpacks itself where TOS loaded it, at $10100).
constexpr uint32_t kStateVar = 0x4B632, kPointerX = 0x54DD0, kPointerY = 0x54DD2;
// Frames per drawn frame: the main loop only draws (the game logic runs in the VBL, $4B55A) and waits for this
// many VBLs between frames (set to 2 once at $4B2C8: 25 frames/s). 1 = a new picture every VBL.
constexpr uint32_t kFramesPerDraw = 0x4D2E4;

// ST scancodes we press for the player.
constexpr uint8_t kRShift = 0x36, kLShift = 0x2A, kCtrl = 0x1D, kAlt = 0x38, kSpace = 0x39, kQ = 0x10;

struct Tuning {
  double speed = 6.0;      // pointer speed at full deflection, pixels per frame (the screen is 320 x 200)
  double curve = 2.0;      // response curve exponent: 1 = linear, higher = gentler near the centre
  double deadzone = 0.15;  // stick dead zone (0..1)
  double dpad = 1.0;       // d-pad speed, pixels per frame
  // The fire direction modes ($49130) run 0 up, 1 up and out, 2 sideways, 3 down and out, 4 down; left Shift
  // steps towards 0, Alt towards 4. Right stick up = left Shift (towards up), unless swapped.
  int fire_mode_swap = 0;
  int draw_50 = 1;  // draw 50 frames/s in play (the original: 25)
} g_tune;

struct Tap {
  uint8_t code;
  int frames;
};
std::vector<Tap> g_taps;
float g_px = 0, g_py = 0;  // pointer movement not applied yet (sub-pixel)
uint8_t g_held_keys = 0;   // bit 2 Control (held by the pad)
int g_flick = 0;           // right stick: -1 up, 1 down, 0 back near the centre (ready for the next flick)
uint64_t g_quit_armed_ms = 0;
bool g_quit = false;
bool g_running = false;  // the game has unpacked itself and reached its title
bool g_test = false;
TestPad g_test_pad;

uint16_t Get16(st::Machine& m, uint32_t a) { return uint16_t(m.Ram()[a] << 8 | m.Ram()[a + 1]); }
void Put16(st::Machine& m, uint32_t a, uint16_t v) { m.Ram()[a] = uint8_t(v >> 8), m.Ram()[a + 1] = uint8_t(v); }

void TapKey(st::Machine* m, uint8_t code, int frames = 3) {
  m->KeyEvent(code, true);
  g_taps.push_back({code, frames});
}

void Hold(st::Machine* m, int bit, uint8_t code, bool down) {
  bool was = g_held_keys & (1 << bit);
  if (was == down) return;
  g_held_keys = uint8_t(down ? g_held_keys | (1 << bit) : g_held_keys & ~(1 << bit));
  m->KeyEvent(code, down);
}

void PauseButton(st::Machine* m) {
  int s = GameState(*m);
  if (s == kPlay) TapKey(m, kRShift);
  else if (s == kPause) TapKey(m, kSpace);
}

void MenuButton(st::Machine* m) {
  int s = GameState(*m);
  if (s == kPause) {
    TapKey(m, kQ);
    LOG("gridrunner: quit to the title");
  } else if (s == kTitle) {
    if (SDL_GetTicks() < g_quit_armed_ms) g_quit = true;
    else g_quit_armed_ms = SDL_GetTicks() + 3000;
  }
}

// A stick through the response curve: pixels per frame.
void Curve(float x, float y, float* vx, float* vy) {
  float mag = std::sqrt(x * x + y * y);
  *vx = *vy = 0;
  if (mag <= g_tune.deadzone) return;
  double t = std::min(1.0, (mag - g_tune.deadzone) / (1.0 - g_tune.deadzone));
  double v = g_tune.speed * std::pow(t, g_tune.curve);
  *vx = float(x / mag * v), *vy = float(y / mag * v);
}

}  // namespace

int GameState(st::Machine& m) { return Get16(m, kStateVar); }

void LoadSettings() {
  g_tune.speed = settings::Get("grid_stick_speed", 6.0);
  g_tune.curve = settings::Get("grid_stick_curve", 2.0);
  g_tune.deadzone = std::clamp(settings::Get("grid_stick_deadzone", 0.15), 0.0, 0.9);
  g_tune.dpad = settings::Get("grid_dpad_speed", 1.0);
  g_tune.fire_mode_swap = settings::Get("grid_fire_mode_swap", 0.0) != 0;
  g_tune.draw_50 = settings::Get("grid_50fps", 1.0) != 0;
  LOG("gridrunner stick: speed %.2f, curve %.2f, dead zone %.2f, d-pad %.2f", g_tune.speed, g_tune.curve,
      g_tune.deadzone, g_tune.dpad);
}

bool HandleEvent(const SDL_Event& e, st::Machine* m) {
  if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
    switch (e.gbutton.button) {
      case SDL_GAMEPAD_BUTTON_BACK:
      case SDL_GAMEPAD_BUTTON_START: PauseButton(m); return true;
      case SDL_GAMEPAD_BUTTON_EAST: MenuButton(m); return true;
      default: return false;
    }
  }
  if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_ESCAPE && !e.key.repeat) {
    MenuButton(m);
    return true;
  }
  return false;
}

void Update(st::Machine* m, SDL_Gamepad* pad, int mouse_dx, int mouse_dy, uint8_t mouse_buttons) {
  for (size_t i = 0; i < g_taps.size();) {  // release tapped keys
    if (--g_taps[i].frames > 0) {
      ++i;
      continue;
    }
    m->KeyEvent(g_taps[i].code, false);
    g_taps.erase(g_taps.begin() + long(i));
  }
  // 50 frames/s while playing (with the faster CPU); the original 25 elsewhere (other screens may count draws).
  // Only once the game is running: before it has unpacked itself these addresses hold its packed data.
  g_running |= GameState(*m) == kTitle;
  if (g_running) Put16(*m, kFramesPerDraw, g_tune.draw_50 && GameState(*m) == kPlay ? 1 : 2);
  uint8_t buttons = mouse_buttons;
  bool test = g_test;
  g_test = false;
  if (pad || test) {
    auto axis = [&](SDL_GamepadAxis a) {
      if (test) {
        switch (a) {
          case SDL_GAMEPAD_AXIS_LEFTX: return g_test_pad.lx;
          case SDL_GAMEPAD_AXIS_LEFTY: return g_test_pad.ly;
          case SDL_GAMEPAD_AXIS_RIGHTY: return g_test_pad.ry;
          case SDL_GAMEPAD_AXIS_LEFT_TRIGGER: return g_test_pad.lt;
          case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: return g_test_pad.rt;
          default: return 0.0f;
        }
      }
      return SDL_GetGamepadAxis(pad, a) / 32767.0f;
    };
    auto button = [&](SDL_GamepadButton b) {
      return test ? b == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER && g_test_pad.rb : SDL_GetGamepadButton(pad, b);
    };
    // The pointer: left stick through the curve, d-pad at a fixed slow speed.
    float vx, vy;
    Curve(axis(SDL_GAMEPAD_AXIS_LEFTX), axis(SDL_GAMEPAD_AXIS_LEFTY), &vx, &vy);
    if (button(SDL_GAMEPAD_BUTTON_DPAD_LEFT)) vx -= float(g_tune.dpad);
    if (button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) vx += float(g_tune.dpad);
    if (button(SDL_GAMEPAD_BUTTON_DPAD_UP)) vy -= float(g_tune.dpad);
    if (button(SDL_GAMEPAD_BUTTON_DPAD_DOWN)) vy += float(g_tune.dpad);
    if (g_running && (vx != 0 || vy != 0)) {
      g_px += vx, g_py += vy;
      int dx = int(g_px), dy = int(g_py);
      g_px -= float(dx), g_py -= float(dy);
      // Straight into the game's pointer (as its mouse handler would, clamped the same way).
      Put16(*m, kPointerX, uint16_t(std::clamp(int(int16_t(Get16(*m, kPointerX))) + dx, 0, 319)));
      Put16(*m, kPointerY, uint16_t(std::clamp(int(int16_t(Get16(*m, kPointerY))) + dy, 0, 199)));
    } else {
      g_px = g_py = 0;
    }
    if (axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 0.4f || button(SDL_GAMEPAD_BUTTON_SOUTH)) buttons |= 2;  // left
    if (axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 0.4f) buttons |= 1;                                      // right
    // Nosecone shot direction: one step per flick of the right stick up or down, like a button (the game would
    // repeat while its key is held: a 2-frame tap is one step); the stick must come back near the centre first.
    float ry = axis(SDL_GAMEPAD_AXIS_RIGHTY);
    if (g_flick == 0 && std::fabs(ry) > 0.5f) {
      g_flick = ry < 0 ? -1 : 1;
      bool towards_up = (g_flick < 0) != g_tune.fire_mode_swap;
      TapKey(m, towards_up ? kLShift : kAlt, 2);
    } else if (g_flick != 0 && std::fabs(ry) < 0.3f) {
      g_flick = 0;
    }
    Hold(m, 2, kCtrl, button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER));
  } else {
    Hold(m, 2, kCtrl, false);
  }
  m->MouseInput(mouse_dx, mouse_dy, buttons);
}

const char* Hint(st::Machine* m, bool have_pad) {
  int s = GameState(*m);
  if (s == kPause) return have_pad ? "Back: resume   B: quit to title" : "Space: resume   Q or Esc: quit to title";
  if (s == kTitle && SDL_GetTicks() < g_quit_armed_ms)
    return have_pad ? "Press B again to quit" : "Press Esc again to quit";
  return nullptr;
}

bool QuitRequested() { return g_quit; }

void SetTestPad(const TestPad& t) {
  g_test = true;
  g_test_pad = t;
}

}  // namespace grid
