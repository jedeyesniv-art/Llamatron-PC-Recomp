#include "stgames.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "log.h"
#include "pad.h"

namespace stgames {
namespace {

Game g_game = Game::kRmc;
bool g_quit = false;
uint64_t g_quit_armed_ms = 0;
std::string g_hint;
bool g_keys[SDL_SCANCODE_COUNT] = {};

struct Tap {
  uint8_t code;
  int frames;
};
std::vector<Tap> g_taps;
int g_delayed_key = 0, g_delay = 0;  // a key to tap after some frames (RMC: X once the game has resumed)

uint32_t Get32(const st::Machine& m, uint32_t a) {
  const uint8_t* p = m.Ram() + a;
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
int Get16(const st::Machine& m, uint32_t a) { return int16_t(m.Ram()[a] << 8 | m.Ram()[a + 1]); }

void TapKey(st::Machine* m, uint8_t code, int frames = 3) {
  m->KeyEvent(code, true);
  g_taps.push_back({code, frames});
}

void RunTaps(st::Machine* m) {
  for (size_t i = 0; i < g_taps.size();) {
    if (--g_taps[i].frames > 0) {
      ++i;
      continue;
    }
    m->KeyEvent(g_taps[i].code, false);
    g_taps.erase(g_taps.begin() + long(i));
  }
  if (g_delay > 0 && --g_delay == 0) TapKey(m, uint8_t(g_delayed_key));
}

// B twice (within 3 s): leave the game.
void ArmQuit() {
  if (SDL_GetTicks() < g_quit_armed_ms) g_quit = true;
  else g_quit_armed_ms = SDL_GetTicks() + 3000;
}

// ---- Revenge of the Mutant Camels ----

constexpr uint32_t kRmcHook = 0x42E18, kRmcPlay = 0x1118E, kRmcPause = 0x10526;
constexpr uint8_t kKeyP = 0x19, kKeyO = 0x18, kKeyX = 0x2D, kKeySpace = 0x39, kKeyLShift = 0x2A, kKeyRShift = 0x36,
                  kKeyLCtrl = 0x1D;

bool RmcPlaying(const st::Machine& m) { return Get32(m, kRmcHook) == kRmcPlay; }
bool RmcPaused(const st::Machine& m) { return Get32(m, kRmcHook) == kRmcPause; }

void RmcBack(st::Machine* m) {
  if (RmcPaused(*m)) TapKey(m, kKeyO);
  else if (RmcPlaying(*m)) TapKey(m, kKeyP);
}

void RmcB(st::Machine* m) {
  if (RmcPaused(*m)) {  // resume, then X (it only works in play): back to the menu
    TapKey(m, kKeyO);
    g_delayed_key = kKeyX, g_delay = 10;
  } else {
    ArmQuit();
  }
}

void RmcUpdate(st::Machine* m, int mdx, int mdy, uint8_t mbuttons) {
  const pad::State& p = pad::Now();
  uint8_t j1 = pad::JoyBits(p);
  if (p.Held(SDL_GAMEPAD_BUTTON_SOUTH) || p.Held(SDL_GAMEPAD_BUTTON_WEST) || p.Held(SDL_GAMEPAD_BUTTON_START) ||
      p.rt > 0.5f)
    j1 |= st::kJoyFire;
  // The keyboard: cursor keys + Space / Right Ctrl.
  if (g_keys[SDL_SCANCODE_UP]) j1 |= st::kJoyUp;
  if (g_keys[SDL_SCANCODE_DOWN]) j1 |= st::kJoyDown;
  if (g_keys[SDL_SCANCODE_LEFT]) j1 |= st::kJoyLeft;
  if (g_keys[SDL_SCANCODE_RIGHT]) j1 |= st::kJoyRight;
  if (g_keys[SDL_SCANCODE_SPACE] || g_keys[SDL_SCANCODE_RCTRL]) j1 |= st::kJoyFire;
  if ((j1 & st::kJoyUp) && (j1 & st::kJoyDown)) j1 &= uint8_t(~(st::kJoyUp | st::kJoyDown));
  if ((j1 & st::kJoyLeft) && (j1 & st::kJoyRight)) j1 &= uint8_t(~(st::kJoyLeft | st::kJoyRight));
  m->SetJoystick(1, j1);
  if (pad::Pressed(SDL_GAMEPAD_BUTTON_BACK)) RmcBack(m);
  if (pad::Pressed(SDL_GAMEPAD_BUTTON_EAST)) RmcB(m);
  m->MouseInput(mdx, mdy, mbuttons);
}

// ---- Andes Attack ----

constexpr uint32_t kAndesState = 0x12EEE, kAndesFacing = 0x1C088;
constexpr int kAndesPlay = 1, kAndesPaused = 3;
float g_mouse_y = 0;
int g_stick_dir = 0;     // left stick pushed left (-1) / right (1): one reverse per push
int g_reverse_frames = 0;  // the right button held for a few frames (a reverse)
bool g_fire_held = false;

void AndesUpdate(st::Machine* m, int mdx, int mdy, uint8_t mbuttons) {
  const pad::State& p = pad::Now();
  int state = Get16(*m, kAndesState);
  bool play = state == kAndesPlay;
  uint8_t buttons = mbuttons;
  // Altitude: the stick's up / down moves the mouse (down = lower), through a curve: 4 px a frame at full.
  float y = std::fabs(p.ly) > 0.15f ? p.ly : 0;
  if (y != 0) {
    float k = (std::fabs(y) - 0.15f) / 0.85f;
    g_mouse_y += (y < 0 ? -1 : 1) * 4.0f * k * k;
  }
  if (p.Held(SDL_GAMEPAD_BUTTON_DPAD_UP)) g_mouse_y -= 2;
  if (p.Held(SDL_GAMEPAD_BUTTON_DPAD_DOWN)) g_mouse_y += 2;
  int dy = int(g_mouse_y);
  g_mouse_y -= float(dy);
  if (play) {
    // Left / right: thrust that way; against the facing, reverse first (the right button, once per push).
    int dir = p.lx > 0.5f || p.Held(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) ? 1
              : p.lx < -0.5f || p.Held(SDL_GAMEPAD_BUTTON_DPAD_LEFT) ? -1
              : std::fabs(p.lx) < 0.3f ? 0 : g_stick_dir;
    int facing = Get16(*m, kAndesFacing) ? -1 : 1;
    if (dir != 0 && dir != g_stick_dir && dir != facing) g_reverse_frames = 3;
    g_stick_dir = dir;
    if (pad::Pressed(SDL_GAMEPAD_BUTTON_WEST)) g_reverse_frames = 3;
    if (g_reverse_frames > 0) --g_reverse_frames, buttons |= 1;
    else if ((dir != 0 && dir == facing) || p.lt > 0.5f) buttons |= 2;
    // Fire: Left Shift held.
    bool fire = p.Held(SDL_GAMEPAD_BUTTON_SOUTH) || p.rt > 0.5f;
    if (fire != g_fire_held) m->KeyEvent(kKeyLShift, fire), g_fire_held = fire;
    if (pad::Pressed(SDL_GAMEPAD_BUTTON_NORTH)) TapKey(m, kKeyLCtrl);  // smart bomb
  } else {
    if (g_fire_held) m->KeyEvent(kKeyLShift, false), g_fire_held = false;
    if (p.Held(SDL_GAMEPAD_BUTTON_SOUTH) || p.rt > 0.5f) buttons |= 2;
    if (p.Held(SDL_GAMEPAD_BUTTON_WEST) || p.lt > 0.5f) buttons |= 1;
    g_stick_dir = 0;
  }
  if (pad::Pressed(SDL_GAMEPAD_BUTTON_BACK)) {
    if (play) TapKey(m, kKeyRShift);
    else if (state == kAndesPaused) TapKey(m, kKeySpace);
  }
  if (pad::Pressed(SDL_GAMEPAD_BUTTON_EAST)) ArmQuit();
  m->MouseInput(mdx, mdy + dy, buttons);
}

}  // namespace

void Start(Game g) {
  g_game = g;
  g_quit = false;
  g_quit_armed_ms = 0;
  g_taps.clear();
  g_delay = 0;
  g_mouse_y = 0;
  g_stick_dir = g_reverse_frames = 0;
  g_fire_held = false;
  std::fill(std::begin(g_keys), std::end(g_keys), false);
}

void Update(st::Machine* m, int mouse_dx, int mouse_dy, uint8_t mouse_buttons) {
  RunTaps(m);
  if (g_game == Game::kRmc) RmcUpdate(m, mouse_dx, mouse_dy, mouse_buttons);
  else AndesUpdate(m, mouse_dx, mouse_dy, mouse_buttons);
}

bool HandleKey(const SDL_Event& e, st::Machine* m) {
  if (e.type != SDL_EVENT_KEY_DOWN && e.type != SDL_EVENT_KEY_UP) return false;
  bool down = e.type == SDL_EVENT_KEY_DOWN;
  if (e.key.scancode < SDL_SCANCODE_COUNT) g_keys[e.key.scancode] = down;
  if (e.key.repeat) return true;
  if (e.key.scancode == SDL_SCANCODE_ESCAPE) {
    if (down) g_game == Game::kRmc ? RmcB(m) : ArmQuit();
    return true;
  }
  if (g_game == Game::kRmc) {  // the cursor keys and Space are the joystick
    switch (e.key.scancode) {
      case SDL_SCANCODE_UP: case SDL_SCANCODE_DOWN: case SDL_SCANCODE_LEFT: case SDL_SCANCODE_RIGHT:
      case SDL_SCANCODE_SPACE: case SDL_SCANCODE_RCTRL:
        return true;
      default:
        break;
    }
  }
  return false;  // to the ST
}

const char* Hint(st::Machine* m, bool have_pad) {
  if (SDL_GetTicks() < g_quit_armed_ms)
    return have_pad ? "Press B again to leave the game" : "Press Esc again to leave the game";
  if (g_game == Game::kRmc && RmcPaused(*m))
    return have_pad ? "Back: resume   B: back to the menu" : "O: resume   Esc: back to the menu";
  if (g_game == Game::kAndes && Get16(*m, kAndesState) == kAndesPaused)
    return have_pad ? "Back: resume   B twice: leave the game" : "Space: resume";
  return nullptr;
}

bool QuitRequested() { return g_quit; }

bool Calm(const st::Machine& m) { return g_game == Game::kRmc && RmcPlaying(m); }

}  // namespace stgames
