#include "stgames.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
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
float g_mouse_x = 0, g_mouse_y = 0;
int g_stick_dir = 0;     // left stick pushed left (-1) / right (1): one reverse per push
int g_reverse_frames = 0;  // the right button held for a few frames (a reverse)
bool g_fire_held = false;

// The high score name: typed (Crawio, $1A38C: letters, Space = "_"), 3 of them ($1BC96 counts; the cursor
// $1BC92 is negative when no name is being entered).
constexpr uint32_t kAndesNameCursor = 0x1BC92;
constexpr int kAndesScores = 5;
const char kLetters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ ";
const uint8_t kLetterCodes[27] = {0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32, 0x31,
                                  0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C, 0x39};
int g_letter = 0;
int g_letter_repeat = 0;  // up / down held: frames to the next letter
bool g_letter_held = false;

bool AndesNameEntry(const st::Machine& m) {
  return Get16(m, kAndesState) == kAndesScores && Get16(m, kAndesNameCursor) >= 0;
}

// Up / down (d-pad or stick, repeating) choose a letter, A types it.
void NameEntryPad(st::Machine* m, const pad::State& p) {
  int dir = p.Held(SDL_GAMEPAD_BUTTON_DPAD_UP) || p.ly < -0.5f ? -1
            : p.Held(SDL_GAMEPAD_BUTTON_DPAD_DOWN) || p.ly > 0.5f ? 1 : 0;
  if (dir == 0) {
    g_letter_repeat = 0;
  } else if (g_letter_repeat == 0 || --g_letter_repeat == 0) {
    bool first = !g_letter_held;
    g_letter = (g_letter + dir + 27) % 27;
    g_letter_repeat = first ? 18 : 6;  // held: a pause, then quicker
  }
  g_letter_held = dir != 0;
  if (pad::Pressed(SDL_GAMEPAD_BUTTON_SOUTH) || pad::Pressed(SDL_GAMEPAD_BUTTON_START)) TapKey(m, kLetterCodes[g_letter]);
}

void AndesUpdate(st::Machine* m, int mdx, int mdy, uint8_t mbuttons) {
  const pad::State& p = pad::Now();
  int state = Get16(*m, kAndesState);
  bool play = state == kAndesPlay;
  if (AndesNameEntry(*m)) {
    NameEntryPad(m, p);
    if (pad::Pressed(SDL_GAMEPAD_BUTTON_EAST)) ArmQuit();
    m->MouseInput(mdx, mdy, mbuttons);
    return;
  }
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

// ---- Defender 2 ----

constexpr uint32_t kD2State = 0x4B086, kD2PtrX = 0x62456, kD2PtrY = 0x62458, kD2Scheme = 0x1BCC4,
                   kD2Facing = 0x1B4B8;
constexpr int kD2Play = 0, kD2Title = 2, kD2Screen = 8;
// State 8's screens, by its sub-handler ($1F200): player select, high score initials (the letter is the
// pointer's x / 8; left button fixes it, right deletes), others (game over...).
constexpr uint32_t kD2Sub = 0x1F200, kD2SubPlayers = 0x1157E, kD2SubName = 0x1134C;
constexpr uint8_t kD2Up = 0x1D, kD2Down = 0x2A, kD2Thrust = 0x1C, kD2Reverse = 0x38, kD2Fire = 0x2B,
                  kD2Bomb = 0x36, kD2Inviso = 0x3A, kD2Tab = 0x0F;
struct Point {
  int x, y;
};
// The title's boxes: Defender, Defender II, Stargate; Trainer, Options, History.
const Point kD2Boxes[6] = {{75, 143}, {160, 149}, {243, 143}, {22, 190}, {160, 193}, {298, 190}};
const Point kD2PlayerBoxes[2] = {{75, 60}, {190, 60}};  // the figures (the words don't take clicks)
int g_d2_sel = 1, g_d2_players = 0;
int g_d2_click_in = 0, g_d2_click_frames = 0;  // a click due in n frames / held for n more
Point g_d2_click_at{0, 0};
bool g_host_paused = false;
uint8_t g_d2_held[128] = {};  // keys we hold down on the ST
float g_d2_pwm = 0;           // climb / dive pulses (analog altitude)

void Put16(st::Machine* m, uint32_t a, int v) {
  m->Ram()[a] = uint8_t(v >> 8);
  m->Ram()[a + 1] = uint8_t(v);
}

void Hold(st::Machine* m, uint8_t code, bool down) {
  if (bool(g_d2_held[code]) == down) return;
  g_d2_held[code] = down;
  m->KeyEvent(code, down);
}

void ReleaseAll(st::Machine* m) {
  for (int c = 0; c < 128; ++c) Hold(m, uint8_t(c), false);
}

void D2Click(Point p, int delay) {
  g_d2_click_at = p;
  g_d2_click_in = delay;
}

void Defender2Update(st::Machine* m, int mdx, int mdy, uint8_t mbuttons) {
  const pad::State& p = pad::Now();
  if (pad::Pressed(SDL_GAMEPAD_BUTTON_BACK)) {
    g_host_paused = !g_host_paused;
    if (g_host_paused) ReleaseAll(m);
  }
  if (pad::Pressed(SDL_GAMEPAD_BUTTON_EAST)) ArmQuit();
  if (g_host_paused) return;
  int state = Get16(*m, kD2State);
  uint8_t buttons = mbuttons;
  // A click on its way: the pointer to the spot, the left button held for a few frames.
  if (g_d2_click_in > 0 && --g_d2_click_in == 0) g_d2_click_frames = 5;
  if (g_d2_click_frames > 0) {
    Put16(m, kD2PtrX, g_d2_click_at.x), Put16(m, kD2PtrY, g_d2_click_at.y);
    if (--g_d2_click_frames < 4) buttons |= 2;
  }
  if (state == kD2Play) {
    if (p.connected && Get16(*m, kD2Scheme) == 0) Put16(m, kD2Scheme, 1);  // the keyboard scheme
    // Climb / dive: d-pad full; the stick pulses the key in proportion (the game moves a fixed step).
    float v = std::fabs(p.ly) > 0.2f ? (std::fabs(p.ly) - 0.2f) / 0.8f * (p.ly < 0 ? -1 : 1) : 0.0f;
    if (p.Held(SDL_GAMEPAD_BUTTON_DPAD_UP)) v = -1;
    if (p.Held(SDL_GAMEPAD_BUTTON_DPAD_DOWN)) v = 1;
    bool step = false;
    if (v != 0) {
      g_d2_pwm += std::fabs(v);
      if (g_d2_pwm >= 1) g_d2_pwm -= 1, step = true;
    } else {
      g_d2_pwm = 0;
    }
    Hold(m, kD2Up, step && v < 0);
    Hold(m, kD2Down, step && v > 0);
    // Left / right: thrust that way; against the facing, reverse first.
    int dir = p.lx > 0.5f || p.Held(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) ? 1
              : p.lx < -0.5f || p.Held(SDL_GAMEPAD_BUTTON_DPAD_LEFT) ? -1
              : std::fabs(p.lx) < 0.3f ? 0 : g_stick_dir;
    int facing = Get16(*m, kD2Facing) < 0 ? -1 : 1;
    if (dir != 0 && dir != g_stick_dir && dir != facing) g_reverse_frames = 3;
    g_stick_dir = dir;
    if (pad::Pressed(SDL_GAMEPAD_BUTTON_WEST)) g_reverse_frames = 3;
    Hold(m, kD2Reverse, g_reverse_frames > 0);
    if (g_reverse_frames > 0) --g_reverse_frames;
    Hold(m, kD2Thrust, (dir != 0 && g_reverse_frames == 0) || p.lt > 0.5f);
    Hold(m, kD2Fire, p.Held(SDL_GAMEPAD_BUTTON_SOUTH) || p.rt > 0.5f);
    Hold(m, kD2Inviso, p.Held(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER));
    if (pad::Pressed(SDL_GAMEPAD_BUTTON_NORTH)) TapKey(m, kD2Bomb);
    if (pad::Pressed(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) TapKey(m, kD2Tab);
  } else {
    ReleaseAll(m);
    g_stick_dir = 0;
    if (state == kD2Title) {
      int sel = g_d2_sel;
      if (pad::Pressed(SDL_GAMEPAD_BUTTON_DPAD_LEFT)) sel = sel % 3 == 0 ? sel + 2 : sel - 1;
      if (pad::Pressed(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) sel = sel % 3 == 2 ? sel - 2 : sel + 1;
      if (pad::Pressed(SDL_GAMEPAD_BUTTON_DPAD_UP) || pad::Pressed(SDL_GAMEPAD_BUTTON_DPAD_DOWN)) sel = (sel + 3) % 6;
      if (sel != g_d2_sel) {
        g_d2_sel = sel;
        Put16(m, kD2PtrX, kD2Boxes[sel].x), Put16(m, kD2PtrY, kD2Boxes[sel].y);
      }
      if (pad::Pressed(SDL_GAMEPAD_BUTTON_SOUTH) || pad::Pressed(SDL_GAMEPAD_BUTTON_START)) {
        TapKey(m, kKeySpace);  // "press Space" first, then the box can be clicked
        D2Click(kD2Boxes[g_d2_sel], 50);
      }
    } else if (state == kD2Screen && Get32(*m, kD2Sub) == kD2SubPlayers) {
      int sel = g_d2_players;
      if (pad::Pressed(SDL_GAMEPAD_BUTTON_DPAD_LEFT) || pad::Pressed(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) sel ^= 1;
      if (sel != g_d2_players) {
        g_d2_players = sel;
        Put16(m, kD2PtrX, kD2PlayerBoxes[sel].x), Put16(m, kD2PtrY, kD2PlayerBoxes[sel].y);
      }
      if (pad::Pressed(SDL_GAMEPAD_BUTTON_SOUTH) || pad::Pressed(SDL_GAMEPAD_BUTTON_START))
        D2Click(kD2PlayerBoxes[g_d2_players], 1);
    } else {  // other screens (the initials, options...): the pointer; the d-pad steps it 8 px (one letter)
      if (p.Held(SDL_GAMEPAD_BUTTON_SOUTH) || p.rt > 0.5f) buttons |= 2;
      if (p.Held(SDL_GAMEPAD_BUTTON_WEST) || p.lt > 0.5f) buttons |= 1;
      int sx = pad::Pressed(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) ? 8 : pad::Pressed(SDL_GAMEPAD_BUTTON_DPAD_LEFT) ? -8 : 0;
      int sy = pad::Pressed(SDL_GAMEPAD_BUTTON_DPAD_DOWN) ? 8 : pad::Pressed(SDL_GAMEPAD_BUTTON_DPAD_UP) ? -8 : 0;
      if (sx || sy) {
        Put16(m, kD2PtrX, std::clamp(Get16(*m, kD2PtrX) / 8 * 8 + 4 + sx, 4, 316));
        Put16(m, kD2PtrY, std::clamp(Get16(*m, kD2PtrY) + sy, 0, 199));
      }
      float x = std::fabs(p.lx) > 0.15f ? p.lx : 0, y = std::fabs(p.ly) > 0.15f ? p.ly : 0;
      g_mouse_x += x * std::fabs(x) * 5, g_mouse_y += y * std::fabs(y) * 5;
    }
  }
  int dx = int(g_mouse_x), dy = int(g_mouse_y);
  g_mouse_x -= float(dx), g_mouse_y -= float(dy);
  m->MouseInput(mdx + dx, mdy + dy, buttons);
}

}  // namespace

void Start(Game g) {
  g_game = g;
  g_quit = false;
  g_quit_armed_ms = 0;
  g_taps.clear();
  g_delay = 0;
  g_mouse_x = g_mouse_y = 0;
  g_stick_dir = g_reverse_frames = 0;
  g_d2_sel = 1, g_d2_players = 0;
  g_d2_click_in = g_d2_click_frames = 0;
  g_host_paused = false;
  std::fill(std::begin(g_d2_held), std::end(g_d2_held), uint8_t(0));
  g_d2_pwm = 0;
  g_letter = 0, g_letter_repeat = 0;
  g_fire_held = false;
  std::fill(std::begin(g_keys), std::end(g_keys), false);
}

void Update(st::Machine* m, int mouse_dx, int mouse_dy, uint8_t mouse_buttons) {
  RunTaps(m);
  if (g_game == Game::kRmc) RmcUpdate(m, mouse_dx, mouse_dy, mouse_buttons);
  else if (g_game == Game::kAndes) AndesUpdate(m, mouse_dx, mouse_dy, mouse_buttons);
  else Defender2Update(m, mouse_dx, mouse_dy, mouse_buttons);
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
  if (g_host_paused) return have_pad ? "PAUSED   Back: resume   B twice: leave" : "PAUSED";
  if (g_game == Game::kDefender2 && have_pad && Get16(*m, kD2State) == kD2Screen && Get32(*m, kD2Sub) == kD2SubName)
    return "Left/Right: letter   A: fix it   X: delete";
  if (g_game == Game::kRmc && RmcPaused(*m))
    return have_pad ? "Back: resume   B: back to the menu" : "O: resume   Esc: back to the menu";
  if (g_game == Game::kAndes && AndesNameEntry(*m) && have_pad) {
    static char buf[64];
    std::snprintf(buf, sizeof(buf), "YOUR NAME:  [%c]   Up/Down: letter   A: enter it", kLetters[g_letter]);
    return buf;
  }
  if (g_game == Game::kAndes && Get16(*m, kAndesState) == kAndesPaused)
    return have_pad ? "Back: resume   B twice: leave the game" : "Space: resume";
  return nullptr;
}

bool QuitRequested() { return g_quit; }

bool HostPaused() { return g_host_paused; }

bool Calm(const st::Machine& m) {
  if (g_game == Game::kRmc) return RmcPlaying(m);
  // Not Defender 2: its rasters change colour 0 between lines (the smart bomb's white strobe needs a patch).
  return false;
}

}  // namespace stgames
