#include "gridrunner.h"

#include <algorithm>
#include <cmath>
#include <deque>
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

// Name entry: the 10 x 4 on-screen keyboard (cells of 20 pixels from (65, 80), characters at $1096E: a-z ! . ? -,
// ^ = delete, _ = done); its "letter flying to the name" animation runs while $10966 or $105BC is set.
constexpr uint32_t kNameKeys = 0x1096E, kNameBusy1 = 0x10966, kNameBusy2 = 0x105BC;
// The other screens' click areas (from their code): level select $11F06, options $10A70.
constexpr int kMinusX = 115, kPlusX = 200, kLevelButtonsY = 120, kBlastX = 157, kBlastY = 172;
constexpr int kOptionRows[4] = {84, 104, 134, 186};  // pause mode demo, freeze, reset message, quit (back)

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

// Virtual clicks: the pointer put on (x, y) (x < 0: where it is), an optional modifier key held, the left
// button pressed for 4 frames.
struct Click {
  int x, y;
  uint8_t modifier;
};
std::deque<Click> g_clicks;
int g_click_frame = -1;  // the front click's progress (-1: not started)
int g_prev_state = -1;
uint64_t g_title_hint_ms = 0;

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

void QueueClick(int x, int y, uint8_t modifier = 0) { g_clicks.push_back({x, y, modifier}); }

// The pointer to the name entry cell (col, row), kept on the keyboard (row 3 has only ^ and _ in columns 8, 9).
void NameCell(st::Machine* m, int col, int row) {
  row = std::clamp(row, 0, 3);
  col = row == 3 ? (col >= 9 ? 9 : 8) : std::clamp(col, 0, 9);
  Put16(*m, kPointerX, uint16_t(75 + col * 20));
  Put16(*m, kPointerY, uint16_t(90 + row * 20));
}

void NameMove(st::Machine* m, int dcol, int drow) {
  int col = std::clamp((int16_t(Get16(*m, kPointerX)) - 65) / 20, 0, 9);
  int row = std::clamp((int16_t(Get16(*m, kPointerY)) - 80) / 20, 0, 3);
  if (row == 3 && dcol) col = dcol < 0 ? 8 : 9;
  else col += dcol;
  NameCell(m, col, row + drow);
}

// Clicks the name entry cell holding character c (if there is one).
void NameChar(st::Machine* m, char c) {
  for (int i = 0; i < 40; ++i) {
    if (m->Ram()[kNameKeys + i] != uint8_t(c)) continue;
    QueueClick(75 + (i % 10) * 20, 90 + (i / 10) * 20);
    return;
  }
}

void OptionsMove(st::Machine* m, int d) {
  int y = int16_t(Get16(*m, kPointerY)), row = 0;
  for (int i = 1; i < 4; ++i)
    if (std::abs(kOptionRows[i] - y) < std::abs(kOptionRows[row] - y)) row = i;
  row = std::clamp(row + d, 0, 3);
  Put16(*m, kPointerX, 160);
  Put16(*m, kPointerY, uint16_t(kOptionRows[row]));
}

// Pad buttons on the point-and-click screens. True if used.
bool MenuShortcut(st::Machine* m, uint8_t button) {
  switch (GameState(*m)) {
    case kTitle:
      if (button == SDL_GAMEPAD_BUTTON_WEST) return QueueClick(-1, 0, kLShift), true;
      if (button == SDL_GAMEPAD_BUTTON_NORTH) return QueueClick(-1, 0, kAlt), true;
      if (button == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) return QueueClick(-1, 0, kCtrl), true;
      return false;
    case kLevelSelect:
      if (button == SDL_GAMEPAD_BUTTON_DPAD_LEFT) return QueueClick(kMinusX, kLevelButtonsY), true;
      if (button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) return QueueClick(kPlusX, kLevelButtonsY), true;
      if (button == SDL_GAMEPAD_BUTTON_SOUTH) return QueueClick(kBlastX, kBlastY), true;
      return false;
    case kNameEntry:
      switch (button) {
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return NameMove(m, -1, 0), true;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return NameMove(m, 1, 0), true;
        case SDL_GAMEPAD_BUTTON_DPAD_UP: return NameMove(m, 0, -1), true;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return NameMove(m, 0, 1), true;
        case SDL_GAMEPAD_BUTTON_SOUTH: return QueueClick(-1, 0), true;
        case SDL_GAMEPAD_BUTTON_EAST: return NameChar(m, '^'), true;
        case SDL_GAMEPAD_BUTTON_START: return NameChar(m, '_'), true;
        default: return false;
      }
    case kOptions:
      switch (button) {
        case SDL_GAMEPAD_BUTTON_DPAD_UP: return OptionsMove(m, -1), true;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return OptionsMove(m, 1), true;
        case SDL_GAMEPAD_BUTTON_SOUTH: return QueueClick(-1, 0), true;
        case SDL_GAMEPAD_BUTTON_EAST: return QueueClick(160, kOptionRows[3]), true;
        default: return false;
      }
    default:
      return false;
  }
}

// The screens where the pad's d-pad and A are shortcuts, not pointer moves and the plain left button.
bool ShortcutScreen(int s) { return s == kLevelSelect || s == kNameEntry || s == kOptions; }

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
    if (g_running && MenuShortcut(m, e.gbutton.button)) return true;
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
  // Name entry by keyboard: the key's character is clicked on the on-screen keyboard.
  if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && g_running && GameState(*m) == kNameEntry) {
    SDL_Scancode k = e.key.scancode;
    bool shift = e.key.mod & SDL_KMOD_SHIFT;
    if (k >= SDL_SCANCODE_A && k <= SDL_SCANCODE_Z) NameChar(m, char('a' + (k - SDL_SCANCODE_A)));
    else if (k == SDL_SCANCODE_PERIOD) NameChar(m, '.');
    else if (k == SDL_SCANCODE_MINUS) NameChar(m, '-');
    else if (k == SDL_SCANCODE_1 && shift) NameChar(m, '!');
    else if (k == SDL_SCANCODE_SLASH && shift) NameChar(m, '?');
    else if (k == SDL_SCANCODE_BACKSPACE) NameChar(m, '^');
    else if (k == SDL_SCANCODE_RETURN) NameChar(m, '_');
    else return false;
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
    bool shortcuts = ShortcutScreen(GameState(*m));
    if (!shortcuts) {
      if (button(SDL_GAMEPAD_BUTTON_DPAD_LEFT)) vx -= float(g_tune.dpad);
      if (button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) vx += float(g_tune.dpad);
      if (button(SDL_GAMEPAD_BUTTON_DPAD_UP)) vy -= float(g_tune.dpad);
      if (button(SDL_GAMEPAD_BUTTON_DPAD_DOWN)) vy += float(g_tune.dpad);
    }
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
    if (axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 0.4f || (button(SDL_GAMEPAD_BUTTON_SOUTH) && !shortcuts))
      buttons |= 2;  // left
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
  // The front virtual click (after any name entry animation has finished).
  if (!g_clicks.empty() && g_running) {
    const Click& c = g_clicks.front();
    if (g_click_frame < 0 && !(GameState(*m) == kNameEntry && (Get16(*m, kNameBusy1) || Get16(*m, kNameBusy2))))
      g_click_frame = 0;
    if (g_click_frame == 0) {
      if (c.x >= 0) Put16(*m, kPointerX, uint16_t(c.x)), Put16(*m, kPointerY, uint16_t(c.y));
      if (c.modifier) m->KeyEvent(c.modifier, true);
    }
    if (g_click_frame >= 1 && g_click_frame <= 4) buttons |= 2;
    if (g_click_frame == 5 && c.modifier) m->KeyEvent(c.modifier, false);
    if (g_click_frame >= 0 && ++g_click_frame > 8) {
      g_clicks.pop_front();
      g_click_frame = -1;
    }
  }
  if (g_running) {
    int s = GameState(*m);
    if (s != g_prev_state) {
      if (s == kTitle) g_title_hint_ms = SDL_GetTicks() + 5000;
      if (s == kNameEntry) NameCell(m, 0, 0);  // start on "a"
      if (s != kNameEntry && s != kLevelSelect && s != kOptions) g_clicks.clear(), g_click_frame = -1;
      g_prev_state = s;
    }
  }
  m->MouseInput(mouse_dx, mouse_dy, buttons);
}

const char* Hint(st::Machine* m, bool have_pad) {
  int s = GameState(*m);
  if (s == kPause) return have_pad ? "Back: resume   B: quit to title" : "Space: resume   Q or Esc: quit to title";
  if (s == kTitle && SDL_GetTicks() < g_quit_armed_ms)
    return have_pad ? "Press B again to quit" : "Press Esc again to quit";
  if (!have_pad) return nullptr;
  switch (s) {
    case kTitle:
      return SDL_GetTicks() < g_title_hint_ms ? "A: play   X: options   B: quit\nY: game secrets   LB: more info" : nullptr;
    case kLevelSelect: return "D-pad: level - / +   A: let's blast!";
    case kNameEntry: return "D-pad: letter   A: pick   B: delete\nStart: done";
    case kOptions: return "D-pad: choose   A: select   B: back";
    default: return nullptr;
  }
}

bool QuitRequested() { return g_quit; }

void SetTestPad(const TestPad& t) {
  g_test = true;
  g_test_pad = t;
}

}  // namespace grid
