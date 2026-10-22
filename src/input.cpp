#include "input.h"

#include <cmath>
#include <vector>

#include "game.h"
#include "log.h"
#include "machine.h"

namespace input {
namespace {

std::vector<SDL_Gamepad*> g_pads;  // in connection order: [0] = player 1
bool g_keys[SDL_SCANCODE_COUNT] = {};
bool g_a_held = false, g_a_to_stick0 = false;  // pad 1's A: which stick's fire it is while held

// ST keys we tap for the player: pressed now, released a few frames later (the game polls key flags per VBL).
constexpr uint8_t kKeyP = 0x19, kKeyO = 0x18, kKeyX = 0x2D, kKeyT = 0x14, kKeyG = 0x22;
struct Tap {
  uint8_t code;
  int frames;
};
std::vector<Tap> g_taps;
int g_quit_to_title = 0;      // frames left waiting for the game to resume before tapping X (quit to title)
uint64_t g_quit_armed_ms = 0; // B at the title: quit if pressed again before this time
bool g_quit = false;
uint64_t g_aim_hint_ms = 0;   // show the analog aim setting until then (title screen)
bool g_was_title = false;

void TapKey(st::Machine* m, uint8_t code) {
  m->KeyEvent(code, true);
  g_taps.push_back({code, 3});
}

// Back: the game's pause (P) / resume (O).
void PauseButton(st::Machine* m) {
  if (game::Playing(*m)) TapKey(m, kKeyP);
  else if (game::Paused(*m)) TapKey(m, kKeyO);
}

// Y: on the pause screen, the tentacle on (T) / off (G).
void TentacleButton(st::Machine* m) {
  if (game::Paused(*m)) TapKey(m, game::Tentacle(*m) ? kKeyG : kKeyT);
}

// B (Esc): from the pause screen back to the title (the game's X key only works while playing: resume first);
// at the title, quit (press twice).
void MenuButton(st::Machine* m) {
  if (game::Paused(*m)) {
    TapKey(m, kKeyO);
    g_quit_to_title = 50;
  } else if (game::AtTitle(*m)) {
    if (SDL_GetTicks() < g_quit_armed_ms) g_quit = true;
    else g_quit_armed_ms = SDL_GetTicks() + 3000;
  }
}

// A stick as 8-way joystick bits: 45-degree sectors, centred on the axes.
uint8_t StickBits(SDL_Gamepad* pad, SDL_GamepadAxis ax, SDL_GamepadAxis ay) {
  float x = SDL_GetGamepadAxis(pad, ax) / 32767.0f, y = SDL_GetGamepadAxis(pad, ay) / 32767.0f;
  if (x * x + y * y < 0.35f * 0.35f) return 0;
  float angle = std::atan2(-y, x);  // up = +90 degrees
  int sector = int(std::lround(angle / (3.14159265f / 4))) & 7;  // 0 = right, counter-clockwise
  static const uint8_t kDir[8] = {st::kJoyRight, st::kJoyRight | st::kJoyUp,   st::kJoyUp,
                                  st::kJoyUp | st::kJoyLeft, st::kJoyLeft, st::kJoyLeft | st::kJoyDown,
                                  st::kJoyDown, st::kJoyDown | st::kJoyRight};
  return kDir[sector];
}

uint8_t DpadBits(SDL_Gamepad* pad) {
  uint8_t b = 0;
  if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_UP)) b |= st::kJoyUp;
  if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) b |= st::kJoyDown;
  if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) b |= st::kJoyLeft;
  if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) b |= st::kJoyRight;
  return b;
}

// Up and down (or left and right) together can't happen on a real joystick.
uint8_t Clean(uint8_t b) {
  if ((b & (st::kJoyUp | st::kJoyDown)) == (st::kJoyUp | st::kJoyDown)) b &= ~(st::kJoyUp | st::kJoyDown);
  if ((b & (st::kJoyLeft | st::kJoyRight)) == (st::kJoyLeft | st::kJoyRight)) b &= ~(st::kJoyLeft | st::kJoyRight);
  return b;
}

bool IsJoystickKey(SDL_Scancode k) {
  switch (k) {
    case SDL_SCANCODE_W: case SDL_SCANCODE_A: case SDL_SCANCODE_S: case SDL_SCANCODE_D:
    case SDL_SCANCODE_SPACE: case SDL_SCANCODE_UP: case SDL_SCANCODE_DOWN: case SDL_SCANCODE_LEFT:
    case SDL_SCANCODE_RIGHT: case SDL_SCANCODE_RETURN:
      return true;
    default:
      return false;
  }
}

// SDL scancode -> Atari ST scancode (0 = none). The ST's codes follow the IBM PC's set 1 for the main block.
uint8_t StScancode(SDL_Scancode k) {
  if (k >= SDL_SCANCODE_1 && k <= SDL_SCANCODE_9) return uint8_t(2 + (k - SDL_SCANCODE_1));
  if (k >= SDL_SCANCODE_F1 && k <= SDL_SCANCODE_F10) return uint8_t(0x3B + (k - SDL_SCANCODE_F1));
  switch (k) {
    case SDL_SCANCODE_ESCAPE: return 0x01;
    case SDL_SCANCODE_0: return 0x0B;
    case SDL_SCANCODE_MINUS: return 0x0C;
    case SDL_SCANCODE_EQUALS: return 0x0D;
    case SDL_SCANCODE_BACKSPACE: return 0x0E;
    case SDL_SCANCODE_TAB: return 0x0F;
    case SDL_SCANCODE_Q: return 0x10; case SDL_SCANCODE_W: return 0x11; case SDL_SCANCODE_E: return 0x12;
    case SDL_SCANCODE_R: return 0x13; case SDL_SCANCODE_T: return 0x14; case SDL_SCANCODE_Y: return 0x15;
    case SDL_SCANCODE_U: return 0x16; case SDL_SCANCODE_I: return 0x17; case SDL_SCANCODE_O: return 0x18;
    case SDL_SCANCODE_P: return 0x19;
    case SDL_SCANCODE_LEFTBRACKET: return 0x1A;
    case SDL_SCANCODE_RIGHTBRACKET: return 0x1B;
    case SDL_SCANCODE_RETURN: return 0x1C;
    case SDL_SCANCODE_LCTRL: case SDL_SCANCODE_RCTRL: return 0x1D;
    case SDL_SCANCODE_A: return 0x1E; case SDL_SCANCODE_S: return 0x1F; case SDL_SCANCODE_D: return 0x20;
    case SDL_SCANCODE_F: return 0x21; case SDL_SCANCODE_G: return 0x22; case SDL_SCANCODE_H: return 0x23;
    case SDL_SCANCODE_J: return 0x24; case SDL_SCANCODE_K: return 0x25; case SDL_SCANCODE_L: return 0x26;
    case SDL_SCANCODE_SEMICOLON: return 0x27;
    case SDL_SCANCODE_APOSTROPHE: return 0x28;
    case SDL_SCANCODE_GRAVE: return 0x29;
    case SDL_SCANCODE_LSHIFT: return 0x2A;
    case SDL_SCANCODE_BACKSLASH: return 0x2B;
    case SDL_SCANCODE_Z: return 0x2C; case SDL_SCANCODE_X: return 0x2D; case SDL_SCANCODE_C: return 0x2E;
    case SDL_SCANCODE_V: return 0x2F; case SDL_SCANCODE_B: return 0x30; case SDL_SCANCODE_N: return 0x31;
    case SDL_SCANCODE_M: return 0x32;
    case SDL_SCANCODE_COMMA: return 0x33;
    case SDL_SCANCODE_PERIOD: return 0x34;
    case SDL_SCANCODE_SLASH: return 0x35;
    case SDL_SCANCODE_RSHIFT: return 0x36;
    case SDL_SCANCODE_LALT: case SDL_SCANCODE_RALT: return 0x38;
    case SDL_SCANCODE_SPACE: return 0x39;
    case SDL_SCANCODE_CAPSLOCK: return 0x3A;
    case SDL_SCANCODE_HOME: return 0x47;
    case SDL_SCANCODE_UP: return 0x48;
    case SDL_SCANCODE_KP_MINUS: return 0x4A;
    case SDL_SCANCODE_LEFT: return 0x4B;
    case SDL_SCANCODE_RIGHT: return 0x4D;
    case SDL_SCANCODE_KP_PLUS: return 0x4E;
    case SDL_SCANCODE_DOWN: return 0x50;
    case SDL_SCANCODE_INSERT: return 0x52;
    case SDL_SCANCODE_DELETE: return 0x53;
    case SDL_SCANCODE_PAGEUP: return 0x62;    // Help
    case SDL_SCANCODE_PAGEDOWN: return 0x61;  // Undo
    case SDL_SCANCODE_KP_DIVIDE: return 0x65;
    case SDL_SCANCODE_KP_MULTIPLY: return 0x66;
    case SDL_SCANCODE_KP_7: return 0x67; case SDL_SCANCODE_KP_8: return 0x68; case SDL_SCANCODE_KP_9: return 0x69;
    case SDL_SCANCODE_KP_4: return 0x6A; case SDL_SCANCODE_KP_5: return 0x6B; case SDL_SCANCODE_KP_6: return 0x6C;
    case SDL_SCANCODE_KP_1: return 0x6D; case SDL_SCANCODE_KP_2: return 0x6E; case SDL_SCANCODE_KP_3: return 0x6F;
    case SDL_SCANCODE_KP_0: return 0x70;
    case SDL_SCANCODE_KP_PERIOD: return 0x71;
    case SDL_SCANCODE_KP_ENTER: return 0x72;
    default: return 0;
  }
}

void OpenPad(SDL_JoystickID id) {
  for (SDL_Gamepad* p : g_pads)
    if (SDL_GetGamepadID(p) == id) return;
  if (SDL_Gamepad* p = SDL_OpenGamepad(id)) {
    g_pads.push_back(p);
    LOG("gamepad %zu connected: %s", g_pads.size(), SDL_GetGamepadName(p));
  }
}

}  // namespace

void Init() {
  int n = 0;
  if (SDL_JoystickID* ids = SDL_GetGamepads(&n)) {
    for (int i = 0; i < n; ++i) OpenPad(ids[i]);
    SDL_free(ids);
  }
}

void Shutdown() {
  for (SDL_Gamepad* p : g_pads) SDL_CloseGamepad(p);
  g_pads.clear();
}

bool HandleEvent(const SDL_Event& e, st::Machine* m) {
  switch (e.type) {
    case SDL_EVENT_GAMEPAD_ADDED:
      OpenPad(e.gdevice.which);
      return true;
    case SDL_EVENT_GAMEPAD_REMOVED:
      for (size_t i = 0; i < g_pads.size(); ++i) {
        if (SDL_GetGamepadID(g_pads[i]) == e.gdevice.which) {
          SDL_CloseGamepad(g_pads[i]);
          g_pads.erase(g_pads.begin() + long(i));
          LOG("gamepad %zu disconnected", i + 1);
          break;
        }
      }
      return true;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
      if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && e.gbutton.button == SDL_GAMEPAD_BUTTON_BACK) {
        PauseButton(m);
        return true;
      }
      if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && e.gbutton.button == SDL_GAMEPAD_BUTTON_NORTH) {
        TentacleButton(m);
        return true;
      }
      if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && e.gbutton.button == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER &&
          game::AtTitle(*m)) {  // RB at the title: analog aim on / off
        game::SetAnalogAim(!game::AnalogAim());
        g_aim_hint_ms = SDL_GetTicks() + 3000;
        LOG("analog aim %s", game::AnalogAim() ? "on" : "off");
        return true;
      }
      if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && e.gbutton.button == SDL_GAMEPAD_BUTTON_EAST) {
        MenuButton(m);
        return true;
      }
      return false;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
      bool down = e.type == SDL_EVENT_KEY_DOWN;
      if (e.key.scancode < SDL_SCANCODE_COUNT) g_keys[e.key.scancode] = down;
      if (e.key.repeat || IsJoystickKey(e.key.scancode)) return true;
      if (e.key.scancode == SDL_SCANCODE_ESCAPE) {
        if (down) MenuButton(m);
        return true;
      }
      if (uint8_t code = StScancode(e.key.scancode)) m->KeyEvent(code, down);
      return true;
    }
    default:
      return false;
  }
}

bool QuitRequested() { return g_quit; }

const char* Hint(st::Machine* m) {
  if (game::Paused(*m))
    return g_pads.empty() ? "O: resume   Esc: quit to title\nT / G: tentacle on / off"
                          : "Back: resume   B: quit to title\nY: tentacle (left stick plays)";
  bool title = game::AtTitle(*m);
  if (title && !g_was_title) g_aim_hint_ms = SDL_GetTicks() + 4000;  // shown for a while when the title comes up
  g_was_title = title;
  if (title && SDL_GetTicks() < g_quit_armed_ms)
    return g_pads.empty() ? "Press Esc again to quit" : "Press B again to quit";
  if (title && !g_pads.empty() && SDL_GetTicks() < g_aim_hint_ms)
    return game::AnalogAim() ? "RB: analog aim ON" : "RB: analog aim OFF (8 directions)";
  return nullptr;
}

void Update(st::Machine* m) {
  for (size_t i = 0; i < g_taps.size();) {  // release tapped keys
    if (--g_taps[i].frames > 0) {
      ++i;
      continue;
    }
    m->KeyEvent(g_taps[i].code, false);
    g_taps.erase(g_taps.begin() + long(i));
  }
  if (g_quit_to_title && --g_quit_to_title >= 0 && game::Playing(*m)) {
    TapKey(m, kKeyX);
    g_quit_to_title = 0;
    LOG("quit to the title");
  }

  uint8_t j0 = 0, j1 = 0;
  auto key = [](SDL_Scancode k) { return g_keys[k]; };
  if (key(SDL_SCANCODE_W)) j1 |= st::kJoyUp;
  if (key(SDL_SCANCODE_S)) j1 |= st::kJoyDown;
  if (key(SDL_SCANCODE_A)) j1 |= st::kJoyLeft;
  if (key(SDL_SCANCODE_D)) j1 |= st::kJoyRight;
  if (key(SDL_SCANCODE_SPACE)) j1 |= st::kJoyFire;
  if (key(SDL_SCANCODE_UP)) j0 |= st::kJoyUp;
  if (key(SDL_SCANCODE_DOWN)) j0 |= st::kJoyDown;
  if (key(SDL_SCANCODE_LEFT)) j0 |= st::kJoyLeft;
  if (key(SDL_SCANCODE_RIGHT)) j0 |= st::kJoyRight;
  if (key(SDL_SCANCODE_RETURN)) j0 |= st::kJoyFire;

  bool aim = false;
  if (!g_pads.empty()) {
    SDL_Gamepad* p = g_pads[0];
    auto button = [&](SDL_GamepadButton b) { return SDL_GetGamepadButton(p, b); };
    j1 |= StickBits(p, SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY) | DpadBits(p);
    if (button(SDL_GAMEPAD_BUTTON_WEST)) j1 |= st::kJoyFire;  // X: always stick 1 (a one-stick game)
    if (g_pads.size() == 1) {  // twin-stick: the right stick is joystick 0
      j0 |= StickBits(p, SDL_GAMEPAD_AXIS_RIGHTX, SDL_GAMEPAD_AXIS_RIGHTY);
      float ax = SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_RIGHTX) / 32767.0f;
      float ay = SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_RIGHTY) / 32767.0f;
      aim = ax * ax + ay * ay >= 0.35f * 0.35f;  // the same dead zone as the 8-way direction
      if (aim) game::SetAim(true, ax, ay);
      if (button(SDL_GAMEPAD_BUTTON_START) || (button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) && !game::AtTitle(*m)) ||
          SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16000)
        j0 |= st::kJoyFire;
      // A at the title starts the twin-stick game (fire on stick 2), elsewhere it's stick 1's fire. Decided when
      // it's pressed, so holding it through the start doesn't turn into the other stick's fire.
      bool a = button(SDL_GAMEPAD_BUTTON_SOUTH);
      if (a && !g_a_held) g_a_to_stick0 = game::AtTitle(*m) && game::TwinStickAllowed(*m);
      g_a_held = a;
      if (a) (g_a_to_stick0 ? j0 : j1) |= st::kJoyFire;
    } else if (button(SDL_GAMEPAD_BUTTON_SOUTH) || button(SDL_GAMEPAD_BUTTON_START)) {
      j1 |= st::kJoyFire;
    }
  }
  if (g_pads.size() >= 2) {
    SDL_Gamepad* p = g_pads[1];
    j0 |= StickBits(p, SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY) | DpadBits(p);
    if (SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_SOUTH)) j0 |= st::kJoyFire;
  }
  if (!aim) game::SetAim(false, 0, 0);
  m->SetJoystick(0, Clean(j0));
  m->SetJoystick(1, Clean(j1));
}

}  // namespace input
