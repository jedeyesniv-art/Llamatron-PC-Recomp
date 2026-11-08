#include "stgames.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "cpu.h"
#include "input.h"
#include "log.h"
#include "pad.h"
#include "remaster.h"
#include "remaster_andes.h"
#include "remaster_d2.h"
#include "remaster_rmc.h"
#include "settings.h"

namespace stgames {
namespace {

Game g_game = Game::kRmc;
bool g_quit = false;
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
void Put16(st::Machine* m, uint32_t a, int v) {
  m->Ram()[a] = uint8_t(v >> 8);
  m->Ram()[a + 1] = uint8_t(v);
}
void Put32(st::Machine* m, uint32_t a, uint32_t v) { Put16(m, a, int(v >> 16)), Put16(m, a + 2, int(v & 0xFFFF)); }

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
  if (g_delay > 0 && --g_delay == 0) {
    if (g_delayed_key < 0) m->KeyEvent(0x2A, false);  // Left Shift (held for a page key) up
    else TapKey(m, uint8_t(g_delayed_key));
  }
}

// ---- Revenge of the Mutant Camels ----

// The 512K build's addresses; the 1 MB STE build (REVSTE1M.TOS) has the same code elsewhere.
uint32_t kRmcHook = 0x42E18, kRmcPlay = 0x1118E, kRmcPause = 0x10526;
bool g_rmc_tt = false;  // REVTT.TOS
// The TT Megamix's shareware screen ignores fire until its frame counter reaches 2000 (40 s): "cmpi.w #$7D0,
// $3D94E; ble" at $102A0. The ble becomes a nop (owner 2026-11-01: keep the screen, fire skips it).
constexpr uint32_t kTtNagWait = 0x102A0;
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
  }
}

// Twin stick (setting rmc_twin_stick: 0 off, 1 eight ways, 2 360, default): the camel spits all the time in
// the joystick's direction (its README: Up jumps, Down sits, fire held = neither, to aim up from the ground). In
// play ($12778, STE) the joystick byte d0 sets the shot velocity from an 8-way table ($38D34: 16.16 longs
// $38D24 / $38D28, 3 px a frame, neutral = right) and walk / jump / sit from the same bits. With the right stick
// pushed (on 0.4, off 0.3), a hook on the move.w d0,d1 after the table read writes its direction instead, so the
// left stick only moves, jumps and sits. Speeds as the table's: 3 / max(|cos|, |sin|). The confuzer ($3BFDE >= 0)
// mirrors left / right in solo / assist ($3933C 0 / 1): the aim too; two players ($3933C 2): it swaps the
// players' joysticks, so the aim is left alone then. The goat's turret ($17B9C) reads the same velocity.
struct RmcAimAddrs {
  uint32_t hook, dx, dy, confuse, mode;
};
constexpr RmcAimAddrs kRmcAimSte = {0x1279C, 0x38D24, 0x38D28, 0x3BFDE, 0x3933C};
constexpr RmcAimAddrs kRmcAimTt = {0x127D4, 0x39062, 0x39066, 0x3C370, 0x3963E};  // Tools/mapaddr.py
const RmcAimAddrs* g_rmc_aim = nullptr;  // null: a build without the hook (the 512K one)
int g_rmc_twin = 2;
bool g_rmc_aim_on = false;  // the right stick is pushed: its angle (screen: 0 right, y down)
double g_rmc_aim_angle = 0;

void RmcOnAim(st::Machine& m) {
  const RmcAimAddrs& a = *g_rmc_aim;
  cpu::Set(cpu::Reg(cpu::D0 + 1), (cpu::Get(cpu::Reg(cpu::D0 + 1)) & 0xFFFF0000u) | (cpu::Get(cpu::D0) & 0xFFFF));  // move.w d0,d1
  cpu::Set(cpu::PC, a.hook + 2);
  if (!g_rmc_aim_on || g_rmc_twin == 0) return;
  int16_t confused = int16_t(Get16(m, a.confuse)), mode = int16_t(Get16(m, a.mode));
  if (confused >= 0 && mode == 2) return;
  double c = std::cos(g_rmc_aim_angle), s = std::sin(g_rmc_aim_angle);
  if (g_rmc_twin == 1) {  // eight ways: the table's own values
    const double q = 0.78539816339744831;  // 45 degrees
    double k = std::round(g_rmc_aim_angle / q) * q;
    c = std::round(std::cos(k)), s = std::round(std::sin(k));
  } else {
    double big = std::max(std::fabs(c), std::fabs(s));
    c /= big, s /= big;
  }
  if (confused >= 0) c = -c;
  Put32(&m, a.dx, uint32_t(int32_t(std::lround(3 * c * 65536))));
  Put32(&m, a.dy, uint32_t(int32_t(std::lround(3 * s * 65536))));
}

void RmcInstallAim(st::Machine* m) {
  if (!g_rmc_aim || Get16(*m, g_rmc_aim->hook) == 0x4AFC) return;
  // move.w d0,d1, after the table's writes of dx and dy (move.w (a0,d1.w),dx; move.w $20(a0,d1.w),dy)
  const RmcAimAddrs& a = *g_rmc_aim;
  if (Get16(*m, a.hook) != 0x3200 || Get16(*m, a.hook - 16) != 0x33F0 || Get32(*m, a.hook - 12) != a.dx ||
      Get16(*m, a.hook - 8) != 0x33F0 || Get32(*m, a.hook - 4) != a.dy)
    return;  // not unpacked yet (or not this build)
  Put16(m, a.hook, 0x4AFC);
  m->AddHook(a.hook, RmcOnAim);
  LOG("mutant camels: twin stick hook installed (%s)", g_rmc_twin == 2 ? "360" : g_rmc_twin == 1 ? "8 ways" : "off");
}

void RmcUpdate(st::Machine* m, int mdx, int mdy, uint8_t mbuttons) {
  if (g_rmc_tt && Get16(*m, kTtNagWait) == 0x0C79 && Get16(*m, kTtNagWait + 2) == 0x07D0 &&
      Get32(*m, kTtNagWait + 4) == 0x0003D94E && Get16(*m, kTtNagWait + 8) == 0x6FF6)
    Put16(m, kTtNagWait + 8, 0x4E71);
  remaster_rmc::Update(m);
  const pad::State& p = pad::Now();
  if (RmcPlaying(*m)) RmcInstallAim(m);
  {
    float r = std::hypot(p.rx, p.ry);
    g_rmc_aim_on = r > (g_rmc_aim_on ? 0.3f : 0.4f);
    if (g_rmc_aim_on) g_rmc_aim_angle = std::atan2(double(p.ry), double(p.rx));
  }
  uint8_t j1 = pad::JoyBits(p);
  if (p.Held(SDL_GAMEPAD_BUTTON_SOUTH) || p.Held(SDL_GAMEPAD_BUTTON_WEST) || p.rt > 0.5f) j1 |= st::kJoyFire;
  // The keyboard: cursor keys + Space / Right Ctrl.
  if (g_keys[SDL_SCANCODE_UP]) j1 |= st::kJoyUp;
  if (g_keys[SDL_SCANCODE_DOWN]) j1 |= st::kJoyDown;
  if (g_keys[SDL_SCANCODE_LEFT]) j1 |= st::kJoyLeft;
  if (g_keys[SDL_SCANCODE_RIGHT]) j1 |= st::kJoyRight;
  if (g_keys[SDL_SCANCODE_SPACE] || g_keys[SDL_SCANCODE_RCTRL]) j1 |= st::kJoyFire;
  if ((j1 & st::kJoyUp) && (j1 & st::kJoyDown)) j1 &= uint8_t(~(st::kJoyUp | st::kJoyDown));
  if ((j1 & st::kJoyLeft) && (j1 & st::kJoyRight)) j1 &= uint8_t(~(st::kJoyLeft | st::kJoyRight));
  m->SetJoystick(1, j1);
  // Player 2 (Two-Player: the goat, joystick 0 - $13804 reads it; the confuzer swaps the two sticks) on pad 2.
  m->SetJoystick(0, input::SecondPadJoystick());
  if (pad::Pressed(SDL_GAMEPAD_BUTTON_START)) RmcBack(m);
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
constexpr uint32_t kAndesAlt = 0x2B682, kAndesSpeed = 0x14BC4;
float g_alt = -1;
float g_andes_speed = 3, g_andes_curve = 1.5f;
float g_andes_ease = 0.5f, g_thrust_frac = 0;  // speed steps a frame (direct thrust)
bool g_andes_direct = false;
bool g_andes_stock_alt = true;  // andes_altitude 1 (default, owner 2026-11-07): the stick is the mouse (the game's own altitude handling)
bool g_shield_held = false;
constexpr uint8_t kKeyAlt = 0x38;

// Arcade controls (owner 2026-11-04, after the arcade Defender: andes_controls / d2_controls = 1, the default):
// the stick's left / right only turns the ship round, thrust is a button - RT analog, X the game's own.
bool g_andes_arcade = true, g_d2_arcade = true;
bool g_cruise = false;  // analog thrust: at the trigger's speed last frame (held there)

// A clear sideways push (60% out, within ~35 degrees of level; held from 40% while it stays mostly sideways),
// so climbing or diving at an angle doesn't flip the ship. The d-pad counts at once. -1 left, 1 right, 0 none.
int SidePush(const pad::State& p, int was) {
  bool r = p.Held(SDL_GAMEPAD_BUTTON_DPAD_RIGHT), l = p.Held(SDL_GAMEPAD_BUTTON_DPAD_LEFT);
  if (r != l) return r ? 1 : -1;
  float ax = std::fabs(p.lx), ay = std::fabs(p.ly);
  int side = p.lx > 0 ? 1 : -1;
  if (was == side && ax > 0.4f && ay < ax) return was;
  if (ax > 0.6f && ay < ax * 0.7f) return side;
  return 0;
}

// How far RT is squeezed, 0..1 (a little dead zone at both ends).
float TriggerThrust(const pad::State& p) { return std::clamp((p.rt - 0.06f) / 0.86f, 0.0f, 1.0f); }

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
  if (pad::Pressed(SDL_GAMEPAD_BUTTON_SOUTH) || pad::Pressed(SDL_GAMEPAD_BUTTON_START) || pad::RtPressed()) TapKey(m, kLetterCodes[g_letter]);
}

void AndesUpdate(st::Machine* m, int mdx, int mdy, uint8_t mbuttons) {
  const pad::State& p = pad::Now();
  remaster_andes::Update(m);
  int state = Get16(*m, kAndesState);
  bool play = state == kAndesPlay;
  if (AndesNameEntry(*m)) {
    NameEntryPad(m, p);
    m->MouseInput(mdx, mdy, mbuttons);
    return;
  }
  uint8_t buttons = mbuttons;
  int dy = 0;
  if (play && p.connected && !g_andes_stock_alt) {
    // Altitude: the game takes it straight from the mouse ($2B682 = y - mouse dy, 0x20..0xBE; the ship follows
    // the next frame, no easing), so it's written directly: the stick is its speed, through a gentle curve
    // (andes_stick_speed px a frame at full - 3, owner: 6 too sensitive - and andes_stick_curve).
    float cur = float(Get16(*m, kAndesAlt));
    if (g_alt < 0 || std::fabs(g_alt - cur) >= 1) g_alt = cur;  // (the PC mouse may have moved it)
    float y = std::fabs(p.ly) > 0.15f ? p.ly : 0;
    if (y != 0) {
      float k = (std::fabs(y) - 0.15f) / 0.85f;
      g_alt += (y < 0 ? -1 : 1) * g_andes_speed * std::pow(k, g_andes_curve);
    }
    if (p.Held(SDL_GAMEPAD_BUTTON_DPAD_UP)) g_alt -= 3;
    if (p.Held(SDL_GAMEPAD_BUTTON_DPAD_DOWN)) g_alt += 3;
    g_alt = std::clamp(g_alt, 32.0f, 190.0f);
    Put16(m, kAndesAlt, int(g_alt));
  } else {
    // Other screens - and play with ALTITUDE ORIGINAL (andes_altitude 1, owner 2026-11-07): the stick moves the
    // ST mouse (4 px a frame at full, quadratic) and the game moves the ship as it does for the mouse.
    float y = std::fabs(p.ly) > 0.15f ? p.ly : 0;
    if (y != 0) {
      float k = (std::fabs(y) - 0.15f) / 0.85f;
      g_mouse_y += (y < 0 ? -1 : 1) * 4.0f * k * k;
    }
    if (play && p.Held(SDL_GAMEPAD_BUTTON_DPAD_UP)) g_mouse_y -= 3;
    if (play && p.Held(SDL_GAMEPAD_BUTTON_DPAD_DOWN)) g_mouse_y += 3;
    dy = int(g_mouse_y);
    g_mouse_y -= float(dy);
    g_alt = -1;
  }
  if (play && g_andes_arcade) {
    // Arcade: left / right turn round (the right button for a few frames, once per push), X / Y reverse
    // (owner 2026-11-06: X was full thrust; RT does that).
    int dir = SidePush(p, g_stick_dir);
    int facing = Get16(*m, kAndesFacing) ? -1 : 1;
    if (dir != 0 && dir != g_stick_dir && dir != facing) g_reverse_frames = 3;
    g_stick_dir = dir;
    if (pad::Pressed(SDL_GAMEPAD_BUTTON_NORTH) || pad::Pressed(SDL_GAMEPAD_BUTTON_WEST)) g_reverse_frames = 3;
    float t = TriggerThrust(p);
    if (g_reverse_frames > 0) {
      --g_reverse_frames, buttons |= 1;
    } else if (t > 0) {
      // RT: the trigger sets a speed (the index $14BC4, 0..19). Below it the thrust is held and the game climbs
      // at its own rate; there it is held at that speed (the game's step past it taken back); above it (the
      // trigger eased off) the game's glide.
      int target = std::max(1, int(std::lround(t * 19)));
      int speed = Get16(*m, kAndesSpeed);
      if (speed == target + 1 && g_cruise) Put16(m, kAndesSpeed, target), speed = target;
      g_cruise = speed <= target;
      if (g_cruise) buttons |= 2;
    } else {
      g_cruise = false;
    }
    bool fire = p.Held(SDL_GAMEPAD_BUTTON_SOUTH) || p.lt > 0.5f;
    if (fire != g_fire_held) m->KeyEvent(kKeyLShift, fire), g_fire_held = fire;
    if (pad::Pressed(SDL_GAMEPAD_BUTTON_EAST)) TapKey(m, kKeyLCtrl);  // smart bomb
    bool shield = p.Held(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) || p.Held(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
    if (shield != g_shield_held) m->KeyEvent(kKeyAlt, shield), g_shield_held = shield;
  } else if (play) {
    // Left / right: thrust that way; against the facing, reverse first (the right button, once per push).
    // (Direct thrust is proportional, so a small push counts: 0.2 / release 0.12; else half way / 0.3.)
    const float on = g_andes_direct ? 0.2f : 0.5f, off = g_andes_direct ? 0.12f : 0.3f;
    int dir = p.lx > on || p.Held(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) ? 1
              : p.lx < -on || p.Held(SDL_GAMEPAD_BUTTON_DPAD_LEFT) ? -1
              : std::fabs(p.lx) < off ? 0 : g_stick_dir;
    int facing = Get16(*m, kAndesFacing) ? -1 : 1;
    if (dir != 0 && dir != g_stick_dir && dir != facing) g_reverse_frames = 3;
    g_stick_dir = dir;
    if (pad::Pressed(SDL_GAMEPAD_BUTTON_WEST)) g_reverse_frames = 3;
    if (g_reverse_frames > 0) {
      --g_reverse_frames, buttons |= 1;
    } else if (g_andes_direct) {
      // Direct thrust (andes_direct_thrust): the game's speed index ($14BC4, 0..19; it normally climbs one step
      // every few frames while thrusting and sinks as slowly) follows how far the stick is pushed, one step a
      // frame - about 0.4 s from still to full. The thrust button stays held while moving (the exhaust).
      float push = std::max(0.0f, (std::fabs(p.lx) - 0.15f) / 0.75f);
      if (p.Held(SDL_GAMEPAD_BUTTON_DPAD_LEFT) || p.Held(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || p.lt > 0.5f) push = 1;
      int target = (dir != 0 && dir == facing) || p.lt > 0.5f ? int(std::lround(std::min(push, 1.0f) * 19)) : 0;
      int speed = Get16(*m, kAndesSpeed);
      g_thrust_frac += g_andes_ease;  // (owner: 1 a frame was too twitchy; 0.5 = 0 -> full in ~0.8 s)
      for (; g_thrust_frac >= 1; g_thrust_frac -= 1) {
        if (speed < target) ++speed;
        else if (speed > target) --speed;
      }
      Put16(m, kAndesSpeed, speed);
      if (target > 0) buttons |= 2;
    } else if ((dir != 0 && dir == facing) || p.lt > 0.5f) {
      buttons |= 2;
    }
    // Fire: Left Shift held.
    bool fire = p.Held(SDL_GAMEPAD_BUTTON_SOUTH) || p.rt > 0.5f;
    if (fire != g_fire_held) m->KeyEvent(kKeyLShift, fire), g_fire_held = fire;
    if (pad::Pressed(SDL_GAMEPAD_BUTTON_NORTH)) TapKey(m, kKeyLCtrl);  // smart bomb
    // Shield: Alternate held (Kbshift bit 3, $143B2: on while held, energy $1C440 drains 1 per 20 frames).
    bool shield = p.Held(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) || p.Held(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
    if (shield != g_shield_held) m->KeyEvent(kKeyAlt, shield), g_shield_held = shield;
  } else {
    if (g_fire_held) m->KeyEvent(kKeyLShift, false), g_fire_held = false;
    if (g_shield_held) m->KeyEvent(kKeyAlt, false), g_shield_held = false;
    if (p.Held(SDL_GAMEPAD_BUTTON_SOUTH) || p.rt > 0.5f) buttons |= 2;
    if (p.Held(SDL_GAMEPAD_BUTTON_WEST) || p.lt > 0.5f) buttons |= 1;
    g_stick_dir = 0;
  }
  if (pad::Pressed(SDL_GAMEPAD_BUTTON_START)) {
    if (play) TapKey(m, kKeyRShift);
    else if (state == kAndesPaused) TapKey(m, kKeySpace);
  }
  m->MouseInput(mdx, mdy + dy, buttons);
}

// ---- Defender 2 ----

// Frames are drawn every $4D1EC VBLs (2 = 25 frames/s, set at $109BC; the main loop $1B2D8 waits for the
// VBL's countdown $62454); the game logic is in the VBL ($1BCEA), so 1 only draws more often (verified: world
// position identical frame for frame). d2_50fps (default 1): 1 in play and on the VBL-timed attract screens
// (remaster_d2::DrawsEveryVbl), with the faster CPU (stgames::FastCpu for the attract screens).
constexpr uint32_t kD2Divisor = 0x4D1EC;
bool g_d2_50fps = true;
constexpr uint32_t kD2State = 0x4B086, kD2PtrX = 0x62456, kD2PtrY = 0x62458, kD2Scheme = 0x1BCC4,
                   kD2Facing = 0x1B4B8, kD2Vel = 0x1B4B4, kD2Top = 0x1B4CA, kD2Accel = 0x1B4BE,
                   kD2Dying = 0x2F2DE;  // the death countdown (300 frames; controls skipped)
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
int g_d2_frame = 0, g_d2_space_at = -1;  // on the title: when we sent Space (-1: not yet)
// The title's "press Space" wait: 1 until it has taken Space and shows the pointer, then 0. A Space sent the moment
// the title appears can be missed (owner 2026-11-07: no pointer, A did nothing): so it is sent again while this is 1.
constexpr uint32_t kD2WaitSpace = 0x1E6BE;
int g_d2_title_at = -1;  // when the title came up (g_d2_frame): the flag is only set a few frames in
const char* const kD2BoxNames[6] = {"DEFENDER", "DEFENDER II", "STARGATE", "TRAINER", "OPTIONS", "HISTORY"};
int g_d2_click_in = 0, g_d2_click_frames = 0;  // a click due in n frames / held for n more
Point g_d2_click_at{0, 0};
bool g_host_paused = false;
uint8_t g_d2_held[128] = {};  // keys we hold down on the ST
float g_d2_pwm = 0;           // climb / dive pulses (analog altitude)

void Hold(st::Machine* m, uint8_t code, bool down) {
  if (bool(g_d2_held[code]) == down) return;
  g_d2_held[code] = down;
  m->KeyEvent(code, down);
}

void ReleaseAll(st::Machine* m) {
  for (int c = 0; c < 128; ++c) Hold(m, uint8_t(c), false);
}

// Defender 2's high scores (owner 2026-11-08: "not saving"): the original never saved them - its three tables
// (Defender, Stargate, Defender II: 10 entries each of 8 ASCII digits + 3 initials, "yak" / "moo" / "cow" on the
// disk) are only in RAM, and its one file write is a screenshot saver. We keep them in the save folder: put back
// once the game has started (its VBL $4AF1A installed, the table valid), written when they change.
constexpr uint32_t kD2Scores = 0x325B2, kD2ScoresSize = 3 * 10 * 11, kD2Vbl = 0x4AF1A;
constexpr const char* kD2ScoresFile = "D2SCORES.DAT";
bool g_d2_scores_ready = false;  // put back (or found none) this run
std::string g_d2_scores;          // what the file holds

bool D2ScoresValid(const uint8_t* t) {
  for (uint32_t e = 0; e < 30; ++e) {
    const uint8_t* p = t + e * 11;
    for (int i = 0; i < 8; ++i)
      if (p[i] < '0' || p[i] > '9') return false;
    for (int i = 8; i < 11; ++i)
      if (!std::strchr("abcdefghijklmnopqrstuvwxyz!.?`- ", p[i]) || !p[i]) return false;
  }
  return true;
}

void D2Scores(st::Machine* m) {
  if (m->SaveDir().empty() || m->sandbox || Get32(*m, 0x70) != kD2Vbl) return;
  uint8_t* t = m->Ram() + kD2Scores;
  if (!D2ScoresValid(t)) return;
  const std::filesystem::path path = std::filesystem::path(m->SaveDir()) / kD2ScoresFile;
  if (!g_d2_scores_ready) {
    g_d2_scores_ready = true;
    std::ifstream in(path, std::ios::binary);
    std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (s.size() == kD2ScoresSize && D2ScoresValid(reinterpret_cast<const uint8_t*>(s.data()))) {
      std::memcpy(t, s.data(), kD2ScoresSize);
      g_d2_scores = s;
      LOG("defender 2: high scores put back from %s", kD2ScoresFile);
    } else {
      g_d2_scores.assign(reinterpret_cast<const char*>(t), kD2ScoresSize);  // the game's own: nothing to write yet
    }
    return;
  }
  if (std::memcmp(t, g_d2_scores.data(), kD2ScoresSize) == 0) return;
  g_d2_scores.assign(reinterpret_cast<const char*>(t), kD2ScoresSize);
  std::filesystem::path tmp = path;
  tmp += ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    out.write(g_d2_scores.data(), std::streamsize(g_d2_scores.size()));
    if (!out) return LOG("defender 2: can't write %s", tmp.string().c_str());
  }
  std::error_code ec;
  std::filesystem::rename(tmp, path, ec);
  if (ec) return LOG("defender 2: can't save the high scores (%s)", ec.message().c_str());
  LOG("defender 2: high scores saved");
}

void D2Click(Point p, int delay) {
  g_d2_click_at = p;
  g_d2_click_in = delay;
}

void Defender2Update(st::Machine* m, int mdx, int mdy, uint8_t mbuttons) {
  remaster_d2::Update(m);
  D2Scores(m);
  const pad::State& p = pad::Now();
  int state = Get16(*m, kD2State);
  // Start in play: the emulation paused (the game has no pause of its own); elsewhere Start is A.
  if (pad::Pressed(SDL_GAMEPAD_BUTTON_START) && (g_host_paused || state == kD2Play)) {
    g_host_paused = !g_host_paused;
    if (g_host_paused) ReleaseAll(m);
  }
  if (g_host_paused) return;
  uint8_t buttons = mbuttons;
  // A click on its way: the pointer to the spot, the left button held for a few frames.
  if (g_d2_click_in > 0 && --g_d2_click_in == 0) {
    if (state == kD2Title && Get16(*m, kD2WaitSpace) != 0) {  // the title missed its Space: again, then the click
      TapKey(m, kKeySpace), g_d2_space_at = g_d2_frame, g_d2_click_in = 50;
    } else {
      g_d2_click_frames = 5;
    }
  }
  if (g_d2_click_frames > 0) {
    Put16(m, kD2PtrX, g_d2_click_at.x), Put16(m, kD2PtrY, g_d2_click_at.y);
    if (--g_d2_click_frames < 4) buttons |= 2;
  }
  if (g_d2_50fps) {
    int want = remaster_d2::DrawsEveryVbl(*m) ? 1 : 2;
    if (Get16(*m, kD2Divisor) != want) Put16(m, kD2Divisor, want);
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
    if (g_d2_arcade) {
      // Arcade: left / right turn round (Alt for a few frames, once per push), X / Y reverse.
      int dir = SidePush(p, g_stick_dir);
      int facing = Get16(*m, kD2Facing) < 0 ? -1 : 1;
      if (dir != 0 && dir != g_stick_dir && dir != facing) g_reverse_frames = 3;
      g_stick_dir = dir;
      if (pad::Pressed(SDL_GAMEPAD_BUTTON_NORTH) || pad::Pressed(SDL_GAMEPAD_BUTTON_WEST)) g_reverse_frames = 3;
      Hold(m, kD2Reverse, g_reverse_frames > 0);
      bool thrust = false;
      float t = TriggerThrust(p);
      if (g_reverse_frames > 0) {
        --g_reverse_frames;
      } else if (t > 0 && Get16(*m, kD2Dying) > 0) {
        g_cruise = false;  // dead: the game skips the controls (and the ship keeps its speed), so leave it alone
      } else if (t > 0) {
        // RT: the trigger sets a speed, a share of the top speed $1B4CA (px a frame; the velocity $1B4B4 is
        // 16.16, + = right). Below it Return is held (the game's acceleration), there the velocity is held,
        // above it the game's friction slows the ship. (Held: the frame's acceleration past it taken back.)
        int64_t vel = int32_t(Get32(*m, kD2Vel));
        int64_t target = int64_t(t * double(Get16(*m, kD2Top)) * 65536.0);
        int64_t along = vel * facing;  // < 0: still moving the other way (thrust brakes)
        int64_t step = int32_t(Get32(*m, kD2Accel));
        if (along > target && along <= target + 2 * step && g_cruise) {
          uint32_t v32 = uint32_t(int32_t(target * facing));
          Put16(m, kD2Vel, int(v32 >> 16)), Put16(m, kD2Vel + 2, int(v32 & 0xFFFF));
          along = target;
        }
        g_cruise = along <= target;
        thrust = g_cruise;
      } else {
        g_cruise = false;
      }
      Hold(m, kD2Thrust, thrust);
      Hold(m, kD2Fire, p.Held(SDL_GAMEPAD_BUTTON_SOUTH) || p.lt > 0.5f);
      Hold(m, kD2Inviso, p.Held(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER));
      if (pad::Pressed(SDL_GAMEPAD_BUTTON_EAST)) TapKey(m, kD2Bomb);
      if (pad::Pressed(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) TapKey(m, kD2Tab);
    } else {
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
    }
  } else {
    ReleaseAll(m);
    g_stick_dir = 0;
    if (state != kD2Title) g_d2_space_at = g_d2_title_at = -1;
    if (state == kD2Title) {
      // The pointer only shows after "PRESS SPACE": the first d-pad press sends Space, so the choice is seen.
      bool dpad = pad::Pressed(SDL_GAMEPAD_BUTTON_DPAD_LEFT) || pad::Pressed(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) ||
                  pad::Pressed(SDL_GAMEPAD_BUTTON_DPAD_UP) || pad::Pressed(SDL_GAMEPAD_BUTTON_DPAD_DOWN);
      bool a = pad::Pressed(SDL_GAMEPAD_BUTTON_SOUTH) || pad::Pressed(SDL_GAMEPAD_BUTTON_START) || pad::RtPressed();
      if (g_d2_title_at < 0) g_d2_title_at = g_d2_frame;
      const bool waiting = Get16(*m, kD2WaitSpace) != 0 || g_d2_frame - g_d2_title_at < 30;
      if ((dpad || a) && waiting && (g_d2_space_at < 0 || g_d2_frame - g_d2_space_at > 25))
        TapKey(m, kKeySpace), g_d2_space_at = g_d2_frame;
      ++g_d2_frame;
      int sel = g_d2_sel;
      if (pad::Pressed(SDL_GAMEPAD_BUTTON_DPAD_LEFT)) sel = sel % 3 == 0 ? sel + 2 : sel - 1;
      if (pad::Pressed(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) sel = sel % 3 == 2 ? sel - 2 : sel + 1;
      if (pad::Pressed(SDL_GAMEPAD_BUTTON_DPAD_UP) || pad::Pressed(SDL_GAMEPAD_BUTTON_DPAD_DOWN)) sel = (sel + 3) % 6;
      g_d2_sel = sel;
      // Keep the pointer on the choice (the title may put it back after Space).
      if (!waiting && g_d2_click_frames == 0)
        Put16(m, kD2PtrX, kD2Boxes[sel].x), Put16(m, kD2PtrY, kD2Boxes[sel].y);
      // A: click the choice once the pointer is up (about a second after Space).
      if (a && g_d2_click_in == 0)
        D2Click(kD2Boxes[g_d2_sel], waiting ? std::max(1, g_d2_space_at + 50 - g_d2_frame) : 1);
    } else if (state == kD2Screen && Get32(*m, kD2Sub) == kD2SubPlayers) {
      int sel = g_d2_players;
      if (pad::Pressed(SDL_GAMEPAD_BUTTON_DPAD_LEFT) || pad::Pressed(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) sel ^= 1;
      if (sel != g_d2_players) {
        g_d2_players = sel;
        Put16(m, kD2PtrX, kD2PlayerBoxes[sel].x), Put16(m, kD2PtrY, kD2PlayerBoxes[sel].y);
      }
      if (pad::Pressed(SDL_GAMEPAD_BUTTON_SOUTH) || pad::Pressed(SDL_GAMEPAD_BUTTON_START) || pad::RtPressed())
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

// ---- Hardcore ----

constexpr uint32_t kHcHook = 0x338BA, kHcPlay = 0x10C82, kHcPaused = 0x11242, kHcX = 0x338C2, kHcY = 0x338C4;
constexpr int kHcMinX = 0x15, kHcMaxX = 0x101, kHcMinY = 0x16, kHcMaxY = 0xB2;
float g_hc_x = -1, g_hc_y = -1;  // the pointer as we move it (sub-pixel)
// Its own strobe switch (the S key, "Toggle Strobes off/on" in Yak's notes): $10D66, 0 = strobes. Set off
// once when play first starts (setting hc_strobes=1 keeps them); S still toggles.
constexpr uint32_t kHcStrobesOff = 0x10D66;
constexpr uint8_t kKeyI = 0x17;
bool g_hc_strobes = false, g_hc_strobes_done = false;

void HardcoreUpdate(st::Machine* m, int mdx, int mdy, uint8_t mbuttons) {
  remaster_rmc::Update(m);  // (the Camels' library: remaster_rmc.cpp's Hardcore addresses)
  const pad::State& p = pad::Now();
  uint32_t hook = Get32(*m, kHcHook);
  uint8_t buttons = mbuttons;
  if (p.Held(SDL_GAMEPAD_BUTTON_SOUTH) || p.rt > 0.5f) buttons |= 2;
  if (pad::Pressed(SDL_GAMEPAD_BUTTON_START)) {
    if (hook == kHcPlay) TapKey(m, kKeyP);
    else if (hook == kHcPaused) TapKey(m, kKeyO);
  }
  if (hook == kHcPlay && !g_hc_strobes_done) {
    g_hc_strobes_done = true;
    if (!g_hc_strobes) Put16(m, kHcStrobesOff, 1);
  }
  if (hook == kHcPlay && pad::Pressed(SDL_GAMEPAD_BUTTON_NORTH)) TapKey(m, kKeyI);  // puck intelligence
  if (hook == kHcPlay && p.connected) {
    // Where the pointer is now (the PC mouse may have moved it too).
    float cx = float(Get16(*m, kHcX)), cy = float(Get16(*m, kHcY));
    if (g_hc_x < 0 || std::fabs(g_hc_x - cx) >= 1) g_hc_x = cx;
    if (g_hc_y < 0 || std::fabs(g_hc_y - cy) >= 1) g_hc_y = cy;
    auto curve = [](float v) {
      float a = std::fabs(v);
      if (a < 0.15f) return 0.0f;
      float k = (a - 0.15f) / 0.85f;
      return (v < 0 ? -1 : 1) * 6.0f * k * k;  // 6 px a frame at full deflection
    };
    float dx = curve(p.lx), dy = curve(p.ly);
    if (p.Held(SDL_GAMEPAD_BUTTON_DPAD_LEFT)) dx -= 1;
    if (p.Held(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) dx += 1;
    if (p.Held(SDL_GAMEPAD_BUTTON_DPAD_UP)) dy -= 1;
    if (p.Held(SDL_GAMEPAD_BUTTON_DPAD_DOWN)) dy += 1;
    g_hc_x += dx, g_hc_y -= dy;  // y: up is bigger
    // The right stick: the pointer at that spot (centre = the middle of the field).
    if (p.rx * p.rx + p.ry * p.ry > 0.2f * 0.2f) {
      g_hc_x = (kHcMinX + kHcMaxX) / 2.0f + p.rx * (kHcMaxX - kHcMinX) / 2.0f;
      g_hc_y = (kHcMinY + kHcMaxY) / 2.0f - p.ry * (kHcMaxY - kHcMinY) / 2.0f;
    }
    g_hc_x = std::clamp(g_hc_x, float(kHcMinX), float(kHcMaxX));
    g_hc_y = std::clamp(g_hc_y, float(kHcMinY), float(kHcMaxY));
    Put16(m, kHcX, int(g_hc_x)), Put16(m, kHcY, int(g_hc_y));
  }
  m->MouseInput(mdx, mdy, buttons);
}

// ---- Docs ----

std::string g_docs_keys;
int g_docs_frame = 0, g_docs_repeat = 0;
constexpr uint8_t kKeyUp = 0x48, kKeyDown = 0x50, kKeyReturn = 0x1C, kKeyHelp = 0x62;
const uint8_t kAscii[26] = {0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32,
                            0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C};

void DocsUpdate(st::Machine* m, int mdx, int mdy, uint8_t mbuttons) {
  const pad::State& p = pad::Now();
  ++g_docs_frame;
  // The keys to type: one every 20 frames from frame 150 (the reader has started by then).
  if (g_docs_frame >= 150 && (g_docs_frame - 150) % 20 == 0 && !g_docs_keys.empty()) {
    char ch = char(std::tolower(uint8_t(g_docs_keys[0])));
    g_docs_keys.erase(0, 1);
    if (ch >= 'a' && ch <= 'z') TapKey(m, kAscii[ch - 'a']);
  }
  int dir = p.Held(SDL_GAMEPAD_BUTTON_DPAD_UP) || p.ly < -0.5f ? -1
            : p.Held(SDL_GAMEPAD_BUTTON_DPAD_DOWN) || p.ly > 0.5f ? 1 : 0;
  if (dir == 0) {
    g_docs_repeat = 0;
  } else if (--g_docs_repeat <= 0) {
    TapKey(m, dir < 0 ? kKeyUp : kKeyDown, 2);
    g_docs_repeat = g_docs_repeat < 0 ? 18 : 4;  // a pause, then quicker
    if (std::fabs(p.ly) > 0.9f) g_docs_repeat = 2;
  }
  int page = pad::Pressed(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) || pad::LtPressed() ? -1
             : pad::Pressed(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) || pad::RtPressed() ? 1 : 0;
  if (page) {
    m->KeyEvent(kKeyLShift, true);
    TapKey(m, page < 0 ? kKeyUp : kKeyDown, 2);
    g_delayed_key = -1, g_delay = 3;  // Shift up again after the cursor key
  }
  if (pad::Pressed(SDL_GAMEPAD_BUTTON_SOUTH)) TapKey(m, kKeyReturn);
  if (pad::Pressed(SDL_GAMEPAD_BUTTON_NORTH)) TapKey(m, kKeyHelp);
  if (pad::Pressed(SDL_GAMEPAD_BUTTON_EAST) || pad::Pressed(SDL_GAMEPAD_BUTTON_BACK)) g_quit = true;
  m->MouseInput(mdx, mdy, mbuttons);
}

// ---- Photon Storm ----

constexpr uint32_t kPsState = 0x44846, kPsX = 0x4F1C0, kPsY = 0x4F1C2;
// Frames are drawn every $466BE VBLs (2: 25 a second; the main loop $4622E waits for the VBL's countdown
// $4F1BE); the logic is in the VBL, so 1 in play only draws more often (same state and picture as the
// original, frame for frame). photonstorm_50fps (1): 1 in play, with the faster CPU.
constexpr uint32_t kPsDivisor = 0x466BE;
bool g_ps_50fps = true;
constexpr int kPsPlay = 1, kPsPaused = 10, kPsTitle = 7;
// 3: the bonus stage between levels (the quasi-3D tunnel of particles; entered at $18022 when the warp $1B102 is
// open and the ship reaches the world's middle; VBL handler $1BA22, frame routine $1C060; then 5, then play).
constexpr int kPsBonus = 3, kPsBonusEnd = 5;  // (5: after it, ~290 frames)
// The bonus won (its script's 'e' command, $1C634): 4 = the score counts up (~275 frames), then 6 = WARP (3D
// streaks, set up at $1C866, frame routine $1C9F4, ~405 frames), then GET READY (2) and the next level.
constexpr int kPsBonusWon = 4, kPsWarp = 6;
// The screens drawn every VBL with photonstorm_50fps (their animation is in the VBL) and captured by the remaster:
// play, the ARC logo (12), the title (7), the enemy guide (11), the high scores (8). Not the change-overs (2) or
// the pause (10). (games.cpp play_more: the faster CPU in the same states.)
// (10: the pause screen - a fountain of dots the stick moves; owner 2026-11-07: the held play items didn't belong.)
bool PsDrawsEveryVbl(int state) {
  return state == kPsPlay || state == 12 || state == kPsTitle || state == 11 || state == 8 || state == kPsPaused ||
         state == kPsBonus || state == kPsBonusEnd || state == kPsBonusWon || state == kPsWarp;
}
constexpr uint8_t kKeyQ = 0x10;
float g_ps_mx = 0, g_ps_my = 0;

// ---- Photon Storm: 360-degree analog (setting ps_analog, default 1) ----
// The ship sits in the middle; everything about its direction goes through 16 directions, index d: 0 left,
// 4 down, 8 right, 12 up (screen angle 180 - 22.5 d degrees, y down):
//   $27956 aim (from the pointer's 5x5 cell, -1 = middle), $27942 heading (the ship's sprite, 16 pre-rotated
//   frames at $2D53E: the ST can't rotate it, so it stays the nearest of 16), $27944 speed (0..$28).
//   $17D02: while thrusting (right button, bit 0 of $17BBA) the heading turns one step a VBL towards the aim.
//   $17DEC: the drift d6/d7 (px this VBL; the world moves the other way) from the speed table $27760: per
//     speed / 8 a step and a period, 4 sub-directions a quadrant (0, 26.6, 45, 63.4 degrees).
//   $18494: a shot in slot a0 ($27974, 16 bytes: x, y, dx, dy, sprite d & 7) flies along the aim (or the
//     heading if none): velocities from $27860 (12 a frame on the long axis); $18288 fires d-1, d, d+1.
//   $181DA: the shots move (dx/dy added each VBL; off the screen = freed); $15C30: the aim marker drawn at
//     160 + ($27A78[d]) / 120 + ($27A78[d + 16]) (14 on the long axis).
// Hooks give all of these the stick's exact angle: speeds stay the original's along the 16 directions
// (length / max(|cos|, |sin|), the game's square "circle"); the heading turns 22.5 degrees a VBL at most.
constexpr uint32_t kPsAim = 0x27956, kPsHeading = 0x27942, kPsSpeed = 0x27944, kPsButtons = 0x17BBA;
constexpr uint32_t kPsShots = 0x27974, kPsShotCount = 0x2D53A, kPsSpeedTable = 0x27760, kPsCellTable = 0x27958;
constexpr uint32_t kPsTurn = 0x17D02, kPsTurnTest = 0x17D0E, kPsTurnDone = 0x17D72, kPsTurnBlock = 0x27E56;
constexpr uint32_t kPsMove = 0x17DEC, kPsMoveDone = 0x17ECE;
constexpr uint32_t kPsShotNew = 0x18494, kPsShotNewNext = 0x184A4;
constexpr uint32_t kPsShotsMove = 0x181DA, kPsShotsMoveNext = 0x181E0;
// $43F8E projects the bonus tunnel and the WARP's streaks: x + x * z / (D - z), D = the word at $43F8C. The WARP
// raises D to +520 while its streaks' z run through 0..$1FFF, so z = D now and then divides by zero - the game
// has no handler for that (on an ST: bombs, back to the desktop; seen on the Deck 2026-11-07). Hooked at the
// "sub.w d5,d6" that makes the divisor: 0 becomes 1 (z one off D, which the game already meets: divs overflows).
constexpr uint32_t kPsProjectSub = 0x43FA2;
constexpr uint32_t kPsMarker = 0x15C30, kPsMarkerDraw = 0x15C54, kPsMarkerNone = 0x15C60, kPsMarkerTable = 0x15C3A;
constexpr double kPi = 3.14159265358979;
constexpr int kPsMaxShots = 32;
bool g_ps_analog = true;
bool g_ps_pad = false;        // the pad drives the ship this frame (turns are ours)
bool g_ps_boost = false, g_ps_single = false;  // Left Shift / Alternate held down on the ST by the pad
bool g_ps_aim_on = false;     // the sticks give an exact aim this frame (the right stick's, else the left's)
double g_ps_aim = 0;          // its angle (radians, screen: 0 right, y down)
int g_ps_aim_cell = -1;       // the direction the game makes of the pointer we wrote
bool g_ps_steer_on = false;   // twin stick: the left stick's angle, where the ship turns to
double g_ps_steer = 0;
bool g_ps_heading_on = false;  // g_ps_heading is in step with $27942
double g_ps_heading = 0, g_ps_fx = 0, g_ps_fy = 0;
struct PsShot {
  bool on = false;
  double fx, fy, vx, vy;
  int x, y;
} g_ps_shots[kPsMaxShots];

double PsAngle(int d) { return (180.0 - 22.5 * d) * kPi / 180; }
int PsIndex(double a) { return int(std::lround(8 - a * 8 / kPi)) & 15; }
double PsWrap(double a) { return std::remainder(a, 2 * kPi); }
// The game's square: (cos, sin) / max(|cos|, |sin|).
void PsBox(double a, double* x, double* y) {
  double c = std::cos(a), s = std::sin(a), k = 1 / std::max(std::fabs(c), std::fabs(s));
  *x = c * k, *y = s * k;
}
void SetWord(cpu::Reg r, int v) { cpu::Set(r, (cpu::Get(r) & 0xFFFF0000u) | uint16_t(v)); }

void PsOnProjectSub(st::Machine&) {
  uint16_t d6 = uint16_t(cpu::Get(cpu::Reg(cpu::D0 + 6)) - cpu::Get(cpu::Reg(cpu::D0 + 5)));
  SetWord(cpu::Reg(cpu::D0 + 6), d6 ? d6 : 1);
  cpu::Set(cpu::PC, kPsProjectSub + 2);
}

// The heading's exact angle, re-synced whenever the game set $27942 itself (a new ship...).
double PsHeading(st::Machine& m) {
  int h = Get16(m, kPsHeading) & 15;
  if (!g_ps_heading_on || PsIndex(g_ps_heading) != h) g_ps_heading = PsAngle(h), g_ps_heading_on = true;
  return g_ps_heading;
}

// The angle a new shot or the marker takes: the exact aim, plus the game's own offset from it (the triple
// shot's d - 1 / d + 1); no aim: the heading.
double PsShotAngle(st::Machine& m) {
  int d = Get16(m, kPsAim);
  if (d >= 0 && g_ps_aim_on && g_ps_aim_cell >= 0) {
    int off = ((d - g_ps_aim_cell + 8) & 15) - 8;
    return g_ps_aim + off * -22.5 * kPi / 180;
  }
  if (d >= 0) return PsAngle(d & 15);
  return PsHeading(m);
}

// $17D02 "btst #0,$17BBA": the turn towards the aim - with the pad, towards the left stick (the aim may be
// the right stick's).
void PsOnTurn(st::Machine& m) {
  bool thrust = m.Ram()[kPsButtons] & 1;
  if (!g_ps_analog || !g_ps_pad) {
    if (g_ps_analog) PsHeading(m);
    cpu::Set(cpu::PC, thrust ? kPsTurnTest : kPsTurnDone);  // as the original's beq
    return;
  }
  double h = PsHeading(m);
  if (g_ps_steer_on && thrust && !Get16(m, kPsTurnBlock)) {
    double diff = PsWrap(g_ps_steer - h), step = 22.5 * kPi / 180;
    int before = Get16(m, kPsHeading) & 15;
    h = std::fabs(diff) <= step ? g_ps_steer : h + (diff > 0 ? step : -step);
    g_ps_heading = PsWrap(h);
    int after = PsIndex(g_ps_heading);
    Put16(&m, kPsHeading, after);
    if (after != before && Get16(m, kPsSpeed) >= 0x30) Put16(&m, kPsSpeed, Get16(m, kPsSpeed) - 4);
  }
  cpu::Set(cpu::PC, kPsTurnDone);
}

// $17DEC "move.w $27944,d0": the drift this VBL -> d6 / d7.
void PsOnMove(st::Machine& m) {
  if (!g_ps_analog) {  // the original
    SetWord(cpu::D0, Get16(m, kPsSpeed));
    cpu::Set(cpu::PC, kPsMove + 6);
    return;
  }
  int idx = (Get16(m, kPsSpeed) >> 3) & 7;
  double step = Get16(m, kPsSpeedTable + 0x20 + idx * 2), period = Get16(m, kPsSpeedTable + 0x30 + idx * 2) + 1;
  double rate = step / period, x, y;
  PsBox(PsHeading(m), &x, &y);
  g_ps_fx += x * rate, g_ps_fy += y * rate;
  int dx = int(std::lround(g_ps_fx)), dy = int(std::lround(g_ps_fy));
  g_ps_fx -= dx, g_ps_fy -= dy;
  SetWord(cpu::Reg(cpu::D0 + 6), dx);
  SetWord(cpu::Reg(cpu::D0 + 7), dy);
  cpu::Set(cpu::PC, kPsMoveDone);
}

// $18494 "move.w $27956,d0": a new shot in slot a0; the game sets it up from the nearest direction (its sprite),
// we keep the exact velocity.
void PsOnShotNew(st::Machine& m) {
  if (!g_ps_analog) {  // the original: the aim, or the heading with none
    int d = Get16(m, kPsAim);
    SetWord(cpu::D0, d >= 0 ? d : Get16(m, kPsHeading));
    cpu::Set(cpu::PC, kPsShotNewNext);
    return;
  }
  double a = PsShotAngle(m);
  SetWord(cpu::D0, PsIndex(a));
  cpu::Set(cpu::PC, kPsShotNewNext);
  uint32_t a0 = cpu::Get(cpu::A0) & 0xFFFFFF;
  uint32_t slot = (a0 - kPsShots) / 16;
  if (a0 < kPsShots || slot >= kPsMaxShots) return;
  PsShot& s = g_ps_shots[slot];
  PsBox(a, &s.vx, &s.vy);
  s.vx *= 12, s.vy *= 12;
  s.fx = 160, s.fy = 120, s.x = 160, s.y = 120;  // where $184D6 puts it
  s.on = true;
}

// $181DA "move.w $2D53A,d5": the shots move; set each one's step for this VBL.
void PsOnShotsMove(st::Machine& m) {
  int count = Get16(m, kPsShotCount) + 1;
  SetWord(cpu::Reg(cpu::D0 + 5), count - 1);
  cpu::Set(cpu::PC, kPsShotsMoveNext);
  for (int i = 0; i < std::min(count, kPsMaxShots); ++i) {
    PsShot& s = g_ps_shots[i];
    if (!s.on) continue;
    uint32_t a = kPsShots + uint32_t(i) * 16;
    int x = Get16(m, a), y = Get16(m, a + 2);
    if (x < 0 || x != s.x || y != s.y) {  // freed (a hit), reused or moved by something else
      s.on = false;
      continue;
    }
    s.fx += s.vx, s.fy += s.vy;
    int nx = int(std::lround(s.fx)), ny = int(std::lround(s.fy));
    Put16(&m, a + 4, nx - x), Put16(&m, a + 6, ny - y);
    s.x = nx, s.y = ny;
  }
}

// $15C30 "move.w $27956,d0": the aim marker.
void PsOnMarker(st::Machine& m) {
  int d = Get16(m, kPsAim);
  if (d < 0) {
    cpu::Set(cpu::PC, kPsMarkerNone);
    return;
  }
  if (!g_ps_analog || !g_ps_aim_on) {
    SetWord(cpu::D0, d);
    cpu::Set(cpu::PC, kPsMarkerTable);
    return;
  }
  double x, y;
  PsBox(g_ps_aim, &x, &y);
  SetWord(cpu::D0, 160 + int(std::lround(14 * x)));
  SetWord(cpu::Reg(cpu::D0 + 1), 120 + int(std::lround(14 * y)));
  cpu::Set(cpu::PC, kPsMarkerDraw);
}

// ---- Photon Storm: the remaster (remaster.h) ----
// The main loop calls the screen's draw function (*$461BC; play: $1580C) at $4620C, the screen to draw into
// at $4F1B2. The moving things go through:
//   $44BA8 the sprite routine: a0 = the sprite (word type 0..7; 5 = through the pointer at +4, header 2 bytes
//     before it; +4 height, +6 width), d0 / d1 = top-left ($44C42 centres on them first), a1 the screen, a2 the
//     restore list (its end marker $FE0C at (a2)); $4630A set = into every screen and the background (the HUD:
//     left alone); clipped to lines $57FC6..$57FC8 ($25..$C7 in play) and groups $57FC0..$57FC2; $57FD0 a
//     colour some types use; $46420 = 0 while the main loop draws (another path otherwise). $14F3E..$14F7A
//     paint a HUD icon once into the background and the three screens in turn: left alone. $4620E (after the
//     draw call): the pass is complete (remaster::EndFrame: run-ahead shows the newest complete list).
//   the dot plotters $440BA (1 pixel), $44150 (2 wide), $442F2 (2 x 2, above line $C6), colour d2 (planes from
//     the table $44098, $20 apart), from line $440B8: the stars ($18C2C: 12 at $276B2, $15D70: 16 at $276FE,
//     x / y / brightness), particles, rings.
//   $15C72 the ship: 32 x 32 at (144, 104), frame $27942 of bank $2793E + $180BE (at $2D53E, $2000 a bank,
//     $200 a frame, 4 planes).
// Captured in play (remaster on): not drawn into the ST screen, but handed to remaster:: with the caller and
// a5 (the object, where there is one) as the key. A sprite seen for the first time is drawn by the game that
// once and drawn again by its routine in a sandbox between frames (remaster::DrawInSandbox) for its image.
constexpr uint32_t kPsDrawCall = 0x4620C, kPsScreen = 0x4F1B2, kPsAllScreens = 0x4630A;
constexpr uint32_t kPsSprite = 0x44BA8, kPsSpriteGo = 0x44BB4, kPsCentred = 0x44C6C;
constexpr uint32_t kPsClipTop = 0x57FC6, kPsClipBottom = 0x57FC8, kPsSpriteColour = 0x57FD0;
constexpr uint32_t kPsClipLeft = 0x57FC0, kPsClipRight = 0x57FC2;  // in 16-pixel groups
constexpr uint32_t kPsDrawMode = 0x46420;
constexpr uint32_t kPsHudPaint = 0x14F30, kPsHudPaintEnd = 0x14F80;  // HUD icons, once into the background + 3 screens  // 0 while the main loop draws (2 while it restores)
constexpr uint32_t kPsDot1 = 0x440BA, kPsDot2 = 0x44150, kPsDot4 = 0x442F2, kPsDotTop = 0x440B8, kPsDotPlanes = 0x44098;
// Set (the title: $14506): $442F2's dots go behind the picture, only where the background ($4641C) is colour 0.
constexpr uint32_t kPsDotBehind = 0x441EA;
// $442EA: $442F2 without the top line test (the ARC logo's and the title's stars). ($4427C: into every screen and
// the background - left alone.)
constexpr uint32_t kPsDot4Any = 0x442EA;
constexpr uint32_t kPsShipDraw = 0x15C72, kPsShipDone = 0x15D04, kPsShipFrames = 0x2D53E;
constexpr uint32_t kPsShipBankA = 0x2793E, kPsShipBankB = 0x180BE;
constexpr uint64_t kPsShipImage = 0x5000000000000000ull;
bool g_ps_capture = false;  // this frame's drawing goes to remaster
bool g_ps_remaster_failed = false;  // its hooks' instructions weren't there: don't try again this run
uint32_t g_ps_capture_screen = 0;

uint32_t Reg(int r) { return cpu::Get(cpu::Reg(r)); }
int RegW(int r) { return int16_t(Reg(r)); }
using remaster::Decoding;
void Rts(st::Machine& m) { remaster::Return(m); }
uint64_t PsKey(uint32_t caller, uint32_t object) { return remaster::Key(caller, object); }

// $4620C "jsr (a3)": the screen's drawing starts.
void PsOnDrawCall(st::Machine& m) {
  if (!Decoding(m)) {
    int state = Get16(m, kPsState);
    g_ps_capture = remaster::Enabled() && PsDrawsEveryVbl(state);
    g_ps_capture_screen = Get32(m, kPsScreen) & 0xFFFFFF;
    if (g_ps_capture) remaster::BeginFrame(g_ps_capture_screen);
    else remaster::Stop();
  }
  uint32_t sp = Reg(cpu::A7) - 4;
  Put32(&m, sp & 0xFFFFFF, kPsDrawCall + 2);
  cpu::Set(cpu::A7, sp);
  cpu::Set(cpu::PC, Reg(cpu::A0 + 3));
}

// $4620E "move.l $4F1B2,$466C2" (after the draw call): the pass is complete.
void PsOnDrawDone(st::Machine& m) {
  if (!Decoding(m) && g_ps_capture) remaster::EndFrame(g_ps_capture_screen);
  Put32(&m, 0x466C2, Get32(m, kPsScreen));
  cpu::Set(cpu::PC, kPsDrawCall + 12);
}

// The sprite's image id: the sprite, what it points to, the colour some types take.
uint64_t PsSpriteId(const st::Machine& m, uint32_t a0) {
  uint64_t id = a0 | uint64_t(uint16_t(Get16(m, kPsSpriteColour))) << 24;
  if (Get16(m, a0) == 5) id ^= uint64_t(Get32(m, a0 + 4) & 0xFFFFFF) << 40;
  return id;
}

// $44BA8 "cmp.w #$FE0C,d1".
void PsOnSprite(st::Machine& m) {
  auto original = [] {
    if ((Reg(cpu::D0 + 1) & 0xFFFF) == 0xFE0C) SetWord(cpu::Reg(cpu::D0 + 1), 0x1F3);
    cpu::Set(cpu::PC, kPsSpriteGo);
  };
  if (Decoding(m) || !g_ps_capture || Get16(m, kPsAllScreens) || (Reg(cpu::D0 + 1) & 0xFFFF) == 0xFE0C ||
      (Reg(cpu::A0 + 1) & 0xFFFFFF) != g_ps_capture_screen)
    return original();
  uint32_t a0 = Reg(cpu::A0) & 0xFFFFFF, sp = Reg(cpu::A7) & 0xFFFFFF, ret = Get32(m, sp) & 0xFFFFFF;
  if (ret >= kPsHudPaint && ret < kPsHudPaintEnd) return original();
  uint64_t id = PsSpriteId(m, a0);
  if (!remaster::HaveImage(id)) {
    {
      remaster::SandboxCall call;
      call.routine = kPsSprite;
      for (int r = 0; r < 16; ++r) call.regs[r] = Reg(r);
      uint32_t h = a0;
      if (Get16(m, a0) == 5) h = (Get32(m, a0 + 4) & 0xFFFFFF) - 2;
      int w = Get16(m, h + 6), ht = Get16(m, h + 4);  // (types 0 / 5; others: anything)
      call.at_x = (w > 0 && w <= 288 ? (320 - w) / 2 : 128) & ~15, call.at_y = ht > 0 && ht <= 180 ? (200 - ht) / 2 : 80;
      call.regs[0] = uint32_t(call.at_x), call.regs[1] = uint32_t(call.at_y);
      call.pokes = {{kPsAllScreens, 0}, {kPsClipTop, 0}, {kPsClipBottom, 199},
                    {kPsSpriteColour, uint16_t(Get16(m, kPsSpriteColour))}, {kPsDrawMode, uint16_t(Get16(m, kPsDrawMode))},
                    {kPsClipLeft, 0}, {kPsClipRight, 19}};
      remaster::QueueImage(id, call);
    }
    // (Kept at first sight too: main decodes the image right after the frame - remaster::DrawQueued - before it is shown.)
  }
  remaster::Item it;
  it.key = PsKey(ret == kPsCentred ? Get32(m, sp + 6) : ret, Reg(cpu::A0 + 5));
  it.x = float(RegW(cpu::D0)), it.y = float(RegW(cpu::D0 + 1));
  it.image = id;
  int type = Get16(m, a0);
  it.snap = type == 0 || type == 5 || type == 2 ? 2 : 1;  // these draw at even x ($44D82, $45632)
  it.clip_top = int16_t(Get16(m, kPsClipTop)), it.clip_bottom = int16_t(Get16(m, kPsClipBottom));
  remaster::Add(g_ps_capture_screen, it);
  Rts(m);  // not drawn (the restore list keeps its end marker)
}

// The dot plotters: d0 / d1 the place, d2 the colour; drawn only from line $440B8 to below `bottom`.
void PsDot(st::Machine& m, int w, int h, int bottom, bool behind = false, bool any_top = false) {
  remaster::Item it;
  it.behind = behind;
  it.kind = remaster::kDot;
  uint32_t sp = Reg(cpu::A7) & 0xFFFFFF;
  it.key = PsKey(Get32(m, sp), Reg(cpu::A0 + 5)) ^ 0x8000000000000000ull;
  int x = RegW(cpu::D0), c = RegW(cpu::D0 + 2) & 15, colour = 0;
  if (w == 2) x &= 0x1FE;
  for (int k = 0; k < 4; ++k) colour |= (Get16(m, kPsDotPlanes - 0x20 * k + c * 2) != 0) << k;
  int y = RegW(cpu::D0 + 1);
  it.x = float(x), it.y = float(y), it.colour = uint8_t(colour);
  it.w = uint8_t(w), it.h = uint8_t(h);
  it.clip_top = any_top ? 0 : int16_t(Get16(m, kPsDotTop)), it.clip_bottom = 199;
  if (y >= it.clip_top && y < bottom) remaster::Add(g_ps_capture_screen, it);  // else not drawn at all
  Rts(m);
}
bool PsDotCaught(const st::Machine& m) {
  return !Decoding(m) && g_ps_capture && (Reg(cpu::A0 + 1) & 0xFFFFFF) == g_ps_capture_screen;
}
// $440BA "cmp.w $440B8,d1; bpl $440C6; rts".
void PsOnDot1(st::Machine& m) {
  if (PsDotCaught(m)) return PsDot(m, 1, 1, 200);
  if (RegW(cpu::D0 + 1) >= Get16(m, kPsDotTop)) cpu::Set(cpu::PC, kPsDot1 + 0xC);
  else Rts(m);
}
// $44150 / $442F2 "andi.w #$1FE,d0".
void PsOnDot2(st::Machine& m) {
  if (PsDotCaught(m)) return PsDot(m, 2, 1, 200);
  SetWord(cpu::D0, RegW(cpu::D0) & 0x1FE);
  cpu::Set(cpu::PC, kPsDot2 + 4);
}
void PsOnDot4(st::Machine& m) {
  if (PsDotCaught(m)) return PsDot(m, 2, 2, 0xC6, Get16(m, kPsDotBehind) != 0);
  SetWord(cpu::D0, RegW(cpu::D0) & 0x1FE);
  cpu::Set(cpu::PC, kPsDot4 + 4);
}
// $442EA "andi.w #$1FE,d0; bra $44302".
void PsOnDot4Any(st::Machine& m) {
  if (PsDotCaught(m)) return PsDot(m, 2, 2, 0xC6, Get16(m, kPsDotBehind) != 0, true);
  SetWord(cpu::D0, RegW(cpu::D0) & 0x1FE);
  cpu::Set(cpu::PC, kPsDot4Any + 4);
}

// $15C72 "move.l #0,d1": the ship, at its exact heading.
void PsOnShip(st::Machine& m) {
  if (Decoding(m) || !g_ps_capture) {
    cpu::Set(cpu::Reg(cpu::D0 + 1), 0);
    cpu::Set(cpu::PC, kPsShipDraw + 6);
    return;
  }
  int bank = Get16(m, kPsShipBankA) + Get16(m, kPsShipBankB), frame = Get16(m, kPsHeading) & 15;
  uint64_t id = kPsShipImage | uint64_t(bank & 0xFF) << 4 | uint64_t(frame);
  if (!remaster::HaveImage(id)) {
    uint32_t at = kPsShipFrames + uint32_t(bank) * 0x2000 + uint32_t(frame) * 0x200;
    if (at + 0x200 <= st::kRamSize) remaster::SetImage(id, remaster::DecodePlanes(m.Ram() + at, 2, 32));
  }
  remaster::Item it;
  it.key = 0x5348495000000000ull;  // "SHIP"
  it.x = 144, it.y = 104, it.image = id;
  it.rotate = true;
  double exact = g_ps_analog ? PsHeading(m) : PsAngle(frame);
  if (PsIndex(exact) != frame) exact = PsAngle(frame);
  it.angle = float(exact), it.angle_base = float(PsAngle(frame));
  it.clip_top = int16_t(Get16(m, kPsClipTop)), it.clip_bottom = int16_t(Get16(m, kPsClipBottom));
  remaster::Add(g_ps_capture_screen, it);
  cpu::Set(cpu::PC, kPsShipDone);
}

void PsInstallRemaster(st::Machine* m) {
  struct H {
    uint32_t at;
    std::vector<uint8_t> code;
    void (*fn)(st::Machine&);
  };
  static const H kHooks[] = {
      {kPsDrawCall, {0x4E, 0x93}, PsOnDrawCall},                     // jsr (a3)
      {kPsDrawCall + 2, {0x23, 0xF9, 0x00, 0x04, 0xF1, 0xB2, 0x00, 0x04, 0x66, 0xC2}, PsOnDrawDone},
      {kPsSprite, {0xB2, 0x7C, 0xFE, 0x0C}, PsOnSprite},              // cmp.w #$FE0C,d1
      {kPsDot1, {0xB2, 0x79, 0x00, 0x04, 0x40, 0xB8}, PsOnDot1},      // cmp.w $440B8,d1
      {kPsDot2, {0x02, 0x40, 0x01, 0xFE}, PsOnDot2},                  // andi.w #$1FE,d0
      {kPsDot4, {0x02, 0x40, 0x01, 0xFE}, PsOnDot4},                  // andi.w #$1FE,d0
      {kPsDot4Any, {0x02, 0x40, 0x01, 0xFE}, PsOnDot4Any},            // andi.w #$1FE,d0
      {kPsShipDraw, {0x22, 0x3C, 0x00, 0x00, 0x00, 0x00}, PsOnShip},  // move.l #0,d1
  };
  if (g_ps_remaster_failed || Get16(*m, kPsDrawCall) == 0x4AFC) return;  // already in
  for (const H& h : kHooks)
    if (std::memcmp(m->Ram() + h.at, h.code.data(), h.code.size()) != 0) {
      LOG("photon storm: remaster hooks not installed ($%06X differs)", h.at);
      g_ps_remaster_failed = true;
      return;
    }
  for (const H& h : kHooks) {
    Put16(m, h.at, 0x4AFC);
    m->AddHook(h.at, [fn = h.fn](st::Machine& running) { fn(running); });
  }
  g_ps_capture = false;
  remaster::Stop();
  // Run-ahead: what the hooks change in C++ (the analog heading, drift, shots; the capture) comes back after it.
  struct Saved {
    bool capture, heading_on;
    uint32_t screen;
    double heading, fx, fy;
    PsShot shots[kPsMaxShots];
  };
  static Saved saved;
  remaster::SetAheadState(
      [] {
        saved.capture = g_ps_capture, saved.heading_on = g_ps_heading_on, saved.screen = g_ps_capture_screen;
        saved.heading = g_ps_heading, saved.fx = g_ps_fx, saved.fy = g_ps_fy;
        std::copy(std::begin(g_ps_shots), std::end(g_ps_shots), saved.shots);
      },
      [] {
        g_ps_capture = saved.capture, g_ps_heading_on = saved.heading_on, g_ps_capture_screen = saved.screen;
        g_ps_heading = saved.heading, g_ps_fx = saved.fx, g_ps_fy = saved.fy;
        std::copy(std::begin(saved.shots), std::end(saved.shots), g_ps_shots);
      });
  LOG("photon storm: remaster hooks installed");
}

// Installs the hooks once the program has unpacked itself (the instructions are where they should be).
void PsInstallHooks(st::Machine* m) {
  struct H {
    uint32_t at;
    uint8_t code[6];
    void (*fn)(st::Machine&);
  };
  static const H kHooks[] = {
      {kPsTurn, {0x08, 0x39, 0x00, 0x00, 0x00, 0x01}, PsOnTurn},       // btst #0,$17BBA
      {kPsMove, {0x30, 0x39, 0x00, 0x02, 0x79, 0x44}, PsOnMove},       // move.w $27944,d0
      {kPsShotNew, {0x30, 0x39, 0x00, 0x02, 0x79, 0x56}, PsOnShotNew},  // move.w $27956,d0
      {kPsShotsMove, {0x3A, 0x39, 0x00, 0x02, 0xD5, 0x3A}, PsOnShotsMove},  // move.w $2D53A,d5
      {kPsMarker, {0x30, 0x39, 0x00, 0x02, 0x79, 0x56}, PsOnMarker},   // move.w $27956,d0
  };
  // Only from play on: the unpacker copies from what it has already unpacked (an ILLEGAL put in early was
  // copied into other code).
  if (Get16(*m, kPsTurn) == 0x4AFC || Get16(*m, kPsState) != kPsPlay) return;
  for (const H& h : kHooks)
    if (std::memcmp(m->Ram() + h.at, h.code, 6) != 0) return;  // not unpacked yet
  for (const H& h : kHooks) {
    Put16(m, h.at, 0x4AFC);
    m->AddHook(h.at, [fn = h.fn](st::Machine& running) { fn(running); });
  }
  if (uint16_t(Get16(*m, kPsProjectSub)) == 0x9C45) {  // sub.w d5,d6
    Put16(m, kPsProjectSub, 0x4AFC);
    m->AddHook(kPsProjectSub, PsOnProjectSub);
  } else {
    LOG("photon storm: divide hook not installed ($%06X = $%04X)", kPsProjectSub, uint16_t(Get16(*m, kPsProjectSub)));
  }
  for (PsShot& s : g_ps_shots) s.on = false;
  g_ps_heading_on = false;
  PsInstallRemaster(m);
  LOG("photon storm: analog hooks installed (%s)", g_ps_analog ? "on" : "off");
}

// The direction the game makes of the pointer (x, y) (its routine $17BCA: 5 x 5 cells of 16, table $27958).
int PsCell(const st::Machine& m, int x, int y) {
  return int8_t(m.Ram()[kPsCellTable + (x >> 4) + (y >> 4) * 5]);
}

void PhotonUpdate(st::Machine* m, int mdx, int mdy, uint8_t mbuttons) {
  const pad::State& p = pad::Now();
  PsInstallHooks(m);
  // The remaster's hooks from the ARC logo on (the program has unpacked itself by then; the analog ones wait for play).
  if (PsDrawsEveryVbl(Get16(*m, kPsState))) PsInstallRemaster(m);
  remaster::DrawQueued(m, "photon storm");
  g_ps_analog = settings::Get("ps_analog", 1) != 0;
  int state = Get16(*m, kPsState);
  uint8_t buttons = mbuttons;
  if (g_ps_50fps && Get16(*m, kPsDivisor) > 0) {
    int want = PsDrawsEveryVbl(state) ? 1 : 2;
    if (Get16(*m, kPsDivisor) != want) Put16(m, kPsDivisor, want);
  }
  if (state == kPsTitle && pad::Pressed(SDL_GAMEPAD_BUTTON_NORTH)) {  // Y at the title: 360 twin stick / original
    g_ps_analog = !g_ps_analog;
    settings::Set("ps_analog", g_ps_analog ? 1 : 0);
    settings::Save();
    LOG("photon storm: %s controls", g_ps_analog ? "360 twin stick" : "original 16-direction");
  }
  if (pad::Pressed(SDL_GAMEPAD_BUTTON_START)) {
    if (state == kPsPlay) TapKey(m, kKeyRShift);
    else if (state == kPsPaused) TapKey(m, kKeySpace);
  }
  if (pad::Pressed(SDL_GAMEPAD_BUTTON_EAST) && state == kPsPaused) TapKey(m, kKeyQ);  // quit to the title
  int dx = 0, dy = 0;
  bool boost = false, single = false;
  g_ps_aim_on = g_ps_steer_on = g_ps_pad = false;
  if (state == kPsPlay && p.connected) {
    g_ps_pad = true;
    // The pointer's place from the middle (40, 40) of its 0..79 square is the ship's direction (16 of them,
    // $17BCA via the table $27958): the stick sets it directly; pushed far, the ship also flies (thrust).
    float x = p.lx, y = p.ly;
    if (p.Held(SDL_GAMEPAD_BUTTON_DPAD_LEFT)) x = -1;
    if (p.Held(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) x = 1;
    if (p.Held(SDL_GAMEPAD_BUTTON_DPAD_UP)) y = -1;
    if (p.Held(SDL_GAMEPAD_BUTTON_DPAD_DOWN)) y = 1;
    float mag = std::sqrt(x * x + y * y);
    if (mag > 1) x /= mag, y /= mag, mag = 1;
    if (mag < 0.25f) x = y = 0;
    float steer_mag = mag;
    if (mag >= 0.25f) g_ps_steer_on = true, g_ps_steer = std::atan2(double(y), double(x));
    // Twin stick (ps_analog): the right stick pushed = the aim, and fire. Its own hysteresis: on from 0.4,
    // off below 0.3.
    static bool right_on = false;
    float rmag = std::sqrt(p.rx * p.rx + p.ry * p.ry);
    right_on = g_ps_analog && rmag >= (right_on ? 0.3f : 0.4f);
    if (right_on) x = p.rx, y = p.ry, mag = rmag;
    int px = std::clamp(int(40 + x * 39.5f), 0, 79), py = std::clamp(int(40 + y * 39.5f), 0, 79);
    if (mag >= 0.25f) {
      // Exact angle; the pointer goes to the outer ring so the game's own direction is never "middle".
      g_ps_aim_on = true, g_ps_aim = std::atan2(double(y), double(x));
      px = std::clamp(int(std::lround(40 + x / mag * 39.5f)), 0, 79);
      py = std::clamp(int(std::lround(40 + y / mag * 39.5f)), 0, 79);
      g_ps_aim_cell = PsCell(*m, px, py);
    }
    Put16(m, kPsX, px);
    Put16(m, kPsY, py);
    if (p.Held(SDL_GAMEPAD_BUTTON_SOUTH) || right_on) buttons |= 2;                      // fire
    if (steer_mag > 0.7f || p.Held(SDL_GAMEPAD_BUTTON_WEST)) buttons |= 1;                // thrust
    // The play loop's Kbshift ($17BF4, copy $27C50): bit 2 Control = smart bomb ($17C56: count $1D126, one
    // at a time $183B8); bit 1 Left Shift held = boost ($17ED8: the move doubled while the gauge $1D12C
    // lasts, 1 step per 30 frames, back 1 per 120); bit 3 Alternate held while firing = one shot instead of
    // the three ($1845E). RT bomb, LT boost, LB / RB single shot.
    if (pad::RtPressed()) TapKey(m, kKeyLCtrl);
    boost = p.lt > 0.5f;
    single = p.Held(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) || p.Held(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
  } else {
    // Menus and the initials: the stick is the mouse, A / RT the left button, X / LT the right one.
    if (p.Held(SDL_GAMEPAD_BUTTON_SOUTH) || p.rt > 0.5f) buttons |= 2;
    if (p.Held(SDL_GAMEPAD_BUTTON_WEST) || p.lt > 0.5f) buttons |= 1;
    float x = std::fabs(p.lx) > 0.2f ? p.lx : 0, y = std::fabs(p.ly) > 0.2f ? p.ly : 0;
    g_ps_mx += x * std::fabs(x) * 3, g_ps_my += y * std::fabs(y) * 3;
    dx = int(g_ps_mx), dy = int(g_ps_my);
    g_ps_mx -= float(dx), g_ps_my -= float(dy);
  }
  if (boost != g_ps_boost) m->KeyEvent(kKeyLShift, boost), g_ps_boost = boost;
  if (single != g_ps_single) m->KeyEvent(kKeyAlt, single), g_ps_single = single;
  m->MouseInput(mdx + dx, mdy + dy, buttons);
}

}  // namespace

void Reset() { Start(g_game); }

void SetProgram(const std::string& program) {
  g_rmc_tt = program == "REVTT.TOS";
  g_rmc_aim = program == "REVSTE1M.TOS" ? &kRmcAimSte : g_rmc_tt ? &kRmcAimTt : nullptr;
  g_rmc_aim_on = false;
  remaster_rmc::Start(program);
  if (program == "REVSTE1M.TOS") kRmcHook = 0x3DD3E, kRmcPlay = 0x111DE, kRmcPause = 0x1056C;
  else if (program == "REVTT.TOS") kRmcHook = 0x3E0CE, kRmcPlay = 0x11206, kRmcPause = 0x10580;
  else kRmcHook = 0x42E18, kRmcPlay = 0x1118E, kRmcPause = 0x10526;
}

void StartDocs(const std::string& keys) {
  Start(Game::kDocs);
  g_docs_keys = keys;
}

void Start(Game g) {
  g_game = g;
  g_quit = false;
  g_taps.clear();
  g_delay = 0;
  g_mouse_x = g_mouse_y = 0;
  g_stick_dir = g_reverse_frames = 0;
  g_d2_sel = 1, g_d2_players = 0;
  g_d2_frame = 0, g_d2_space_at = g_d2_title_at = -1;
  g_d2_click_in = g_d2_click_frames = 0;
  g_d2_scores_ready = false, g_d2_scores.clear();
  g_host_paused = false;
  std::fill(std::begin(g_d2_held), std::end(g_d2_held), uint8_t(0));
  g_d2_pwm = 0;
  g_letter = 0, g_letter_repeat = 0;
  g_hc_x = g_hc_y = -1;
  g_ps_mx = g_ps_my = 0;
  g_ps_boost = g_ps_single = false;
  g_docs_frame = g_docs_repeat = 0;
  g_docs_keys.clear();
  g_hc_strobes_done = false;
  g_ps_remaster_failed = false;
  g_fire_held = false;
  g_alt = -1;
  g_thrust_frac = 0;
  g_cruise = false;
  LoadSettings();
  g_shield_held = false;
  remaster_andes::Start();
  remaster_d2::Start();
  std::fill(std::begin(g_keys), std::end(g_keys), false);
}

// The settings read at the start of a game; again when the settings panel changes one (some only matter at the
// start: the panel says so).
void LoadSettings() {
  g_ps_50fps = settings::Get("photonstorm_50fps", 1) != 0;
  g_hc_strobes = settings::Get("hc_strobes", 0) != 0;
  g_d2_50fps = settings::Get("d2_50fps", 1) != 0;
  g_andes_speed = float(settings::Get("andes_stick_speed", 3));
  g_andes_ease = std::clamp(float(settings::Get("andes_thrust_ease", 0.5)), 0.05f, 19.0f);
  g_andes_curve = float(settings::Get("andes_stick_curve", 1.5));
  // Off by default (owner: "left and right feel wrong" with it): the game's own thrust and glide.
  g_andes_direct = settings::Get("andes_direct_thrust", 0) != 0;
  g_andes_stock_alt = settings::Get("andes_altitude", 1) != 0;
  g_andes_arcade = settings::Get("andes_controls", 1) != 0;
  g_d2_arcade = settings::Get("d2_controls", 1) != 0;
  g_rmc_twin = std::clamp(int(settings::Get("rmc_twin_stick", 2)), 0, 2);
}

void Update(st::Machine* m, int mouse_dx, int mouse_dy, uint8_t mouse_buttons) {
  RunTaps(m);
  if (g_game == Game::kRmc) RmcUpdate(m, mouse_dx, mouse_dy, mouse_buttons);
  else if (g_game == Game::kAndes) AndesUpdate(m, mouse_dx, mouse_dy, mouse_buttons);
  else if (g_game == Game::kHardcore) HardcoreUpdate(m, mouse_dx, mouse_dy, mouse_buttons);
  else if (g_game == Game::kDocs) DocsUpdate(m, mouse_dx, mouse_dy, mouse_buttons);
  else if (g_game == Game::kPhotonStorm) PhotonUpdate(m, mouse_dx, mouse_dy, mouse_buttons);
  else Defender2Update(m, mouse_dx, mouse_dy, mouse_buttons);
}

bool HandleKey(const SDL_Event& e, st::Machine* m) {
  if (e.type != SDL_EVENT_KEY_DOWN && e.type != SDL_EVENT_KEY_UP) return false;
  bool down = e.type == SDL_EVENT_KEY_DOWN;
  if (e.key.scancode < SDL_SCANCODE_COUNT) g_keys[e.key.scancode] = down;
  if (e.key.repeat) return true;
  if (e.key.scancode == SDL_SCANCODE_ESCAPE && g_game == Game::kRmc && RmcPaused(*m)) {  // Esc = B
    if (down) RmcB(m);
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
  if (g_host_paused) return have_pad ? "PAUSED   Start: resume" : "PAUSED";
  if (g_game == Game::kDocs && have_pad && g_docs_frame < 400)
    return "Up/Down: scroll   LB/RB: page   B: back to the disks";
  if (g_game == Game::kDefender2 && have_pad && Get16(*m, kD2State) == kD2Title) {
    static char buf[64];
    std::snprintf(buf, sizeof(buf), "< %s >   D-pad: choose   A: play", kD2BoxNames[g_d2_sel]);
    return buf;
  }
  if (g_game == Game::kDefender2 && have_pad && Get16(*m, kD2State) == kD2Screen && Get32(*m, kD2Sub) == kD2SubPlayers)
    return g_d2_players ? "< TWO PLAYERS >   A: start" : "< ONE PLAYER >   A: start";
  if (g_game == Game::kDefender2 && have_pad && Get16(*m, kD2State) == kD2Screen && Get32(*m, kD2Sub) == kD2SubName)
    return "Left/Right: letter   A: fix it   X: delete";
  if (g_game == Game::kHardcore && Get32(*m, kHcHook) == kHcPaused)
    return have_pad ? "PAUSED   Start: resume" : "PAUSED   O: resume";
  if (g_game == Game::kPhotonStorm && Get16(*m, kPsState) == kPsTitle && have_pad)
    return g_ps_analog ? "A: 1 player   X: 2 players\nY: controls < 360 TWIN STICK >"
                       : "A: 1 player   X: 2 players\nY: controls < ORIGINAL 16 DIRECTIONS >";
  if (g_game == Game::kPhotonStorm && Get16(*m, kPsState) == kPsPaused)
    return have_pad ? "PAUSED   Start: resume   B: quit to the title" : nullptr;
  if (g_game == Game::kRmc && RmcPaused(*m))
    return have_pad ? "Start: resume   B: back to the menu" : "O: resume   Esc: back to the menu";
  if (g_game == Game::kAndes && AndesNameEntry(*m) && have_pad) {
    static char buf[64];
    std::snprintf(buf, sizeof(buf), "YOUR NAME:  [%c]   Up/Down: letter   A: enter it", kLetters[g_letter]);
    return buf;
  }
  if (g_game == Game::kAndes && Get16(*m, kAndesState) == kAndesPaused)
    return have_pad ? "PAUSED   Start: resume" : "Space: resume";
  return nullptr;
}

bool QuitRequested() { return g_quit; }

bool HostPaused() { return g_host_paused; }

bool FastCpu(const st::Machine& m) { return g_game == Game::kDefender2 && remaster_d2::Attract(m); }

void CalmStyle(int* hold, float* cap, float* release) {
  // Hardcore's are single white frames now and then: a short hold and a low cap (a faint, short pulse), not
  // the long glow that suits a strobe.
  if (g_game == Game::kHardcore) *hold = 4, *cap = 0.25f, *release = 0.2f;
  else *hold = 16, *cap = 0.4f, *release = 0.05f;
}

bool Calm(const st::Machine& m) {
  if (g_game == Game::kRmc) return RmcPlaying(m);
  // Not Defender 2: its rasters change colour 0 between lines (the smart bomb's white strobe needs a patch).
  return false;
}

}  // namespace stgames
