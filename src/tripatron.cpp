#include "tripatron.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include "log.h"
#include "music.h"
#include "settings.h"

namespace trip {
namespace {

// TRIP.PRG loaded at basepage $10000 (text $10100): its variables at fixed addresses.
constexpr uint32_t kStateVar = 0x108B0;  // 0 performing, 1 title, 2 Control Panel, ...
constexpr uint32_t kPointerX = 0x10692;  // the light source / pointer: 0..319
constexpr uint32_t kPointerY = 0x10694;  // 0..199

// ST scancodes.
constexpr uint8_t kSpace = 0x39, kEsc = 0x01, kF10 = 0x44;
// Effects: the letters A-Z, as (scancode, name).
struct Key {
  uint8_t code;
  const char* name;
};
const Key kEffects[26] = {{0x1E, "A"}, {0x30, "B"}, {0x2E, "C"}, {0x20, "D"}, {0x12, "E"}, {0x21, "F"}, {0x22, "G"},
                          {0x23, "H"}, {0x17, "I"}, {0x24, "J"}, {0x25, "K"}, {0x26, "L"}, {0x32, "M"}, {0x31, "N"},
                          {0x18, "O"}, {0x19, "P"}, {0x10, "Q"}, {0x13, "R"}, {0x1F, "S"}, {0x14, "T"}, {0x16, "U"},
                          {0x2F, "V"}, {0x11, "W"}, {0x2D, "X"}, {0x15, "Y"}, {0x2C, "Z"}};
// Symmetries: the cursor cluster (each gives a different one).
const Key kSymmetries[8] = {{0x48, "UP"},     {0x50, "DOWN"},  {0x4B, "LEFT"}, {0x4D, "RIGHT"},
                            {0x52, "INSERT"}, {0x47, "HOME"}, {0x62, "HELP"}, {0x61, "UNDO"}};
// Palettes: the numeric pad's digits.
const Key kPalettes[10] = {{0x6D, "1"}, {0x6E, "2"}, {0x6F, "3"}, {0x6A, "4"}, {0x6B, "5"},
                           {0x6C, "6"}, {0x67, "7"}, {0x68, "8"}, {0x69, "9"}, {0x70, "0"}};
// Waveforms (the starfield demos): the main keyboard's number keys.
const Key kWaves[10] = {{0x02, "1"}, {0x03, "2"}, {0x04, "3"}, {0x05, "4"}, {0x06, "5"},
                        {0x07, "6"}, {0x08, "7"}, {0x09, "8"}, {0x0A, "9"}, {0x0B, "0"}};

struct Tune {
  double speed = 5, curve = 2, deadzone = 0.15;  // left stick: pixels per frame at full deflection
  double spiro = 0.9;                             // right stick: fraction of the half-screen full deflection reaches
} g_tune;

struct Tap {
  uint8_t code;
  int frames;
};
std::vector<Tap> g_taps;
bool g_calm = true;  // trip_calm_background: the background's flashes toned down (owner 2026-11-01)
int g_effect = -1, g_symmetry = -1, g_palette = -1, g_wave = -1;  // -1: not chosen from the pad yet
bool g_latch = false;
uint32_t g_held = 0;                // pad buttons held last frame (edges)
bool g_lt_held = false;             // (performing: LT toggles the latch on its press)
float g_px = 0, g_py = 0;           // stick movement not sent yet (sub-pixel)
uint64_t g_hint_until = 0, g_quit_armed_ms = 0;
bool g_quit = false;
std::string g_hint;
TestPad g_test_pad;
bool g_test = false;
std::mt19937 g_rng(1992);
// Music.
int g_music_mode = 0;                   // 0 off, 1 pulse, 2 full (settings: trip_music)
bool g_music_key = false;               // F12 pressed (keyboard: input.cpp)
bool g_was_music = false, g_heard = false;
float g_cx = 160, g_cy = 100;           // the orbit's centre
double g_angle = 0, g_music_time = 0, g_last_symmetry = -100;
int g_pulse = 0, g_beats = 0;           // frames of generation left from the last beat; beats so far
uint64_t g_manual_until = 0;            // the player chose something: no automatic changes until then

uint16_t Get16(st::Machine& m, uint32_t a) { return uint16_t(m.Ram()[a] << 8 | m.Ram()[a + 1]); }
void Put16(st::Machine& m, uint32_t a, uint16_t v) { m.Ram()[a] = uint8_t(v >> 8), m.Ram()[a + 1] = uint8_t(v); }

void TapKey(st::Machine* m, uint8_t code) {
  m->KeyEvent(code, true);
  g_taps.push_back({code, 3});
}

void ShowState() {
  char buf[96];
  std::snprintf(buf, sizeof(buf), "EFFECT %s   SYMMETRY %s   PALETTE %s%s",
                g_effect >= 0 ? kEffects[g_effect].name : "-", g_symmetry >= 0 ? kSymmetries[g_symmetry].name : "-",
                g_palette >= 0 ? kPalettes[g_palette].name : "-", g_latch ? "   LATCHED" : "");
  g_hint = buf;
  g_hint_until = SDL_GetTicks() + 2500;
}

// Steps a choice (-1 = none yet: the first step goes to the first / last) and taps its key.
void Step(st::Machine* m, int* index, const Key* keys, int n, int dir) {
  *index = *index < 0 ? (dir > 0 ? 0 : n - 1) : (*index + dir + n) % n;
  TapKey(m, keys[*index].code);
}

void Curve(float x, float y, float* vx, float* vy) {
  float mag = std::sqrt(x * x + y * y);
  *vx = *vy = 0;
  if (mag <= g_tune.deadzone) return;
  double t = std::min(1.0, (mag - g_tune.deadzone) / (1.0 - g_tune.deadzone));
  double v = g_tune.speed * std::pow(t, g_tune.curve);
  *vx = float(x / mag * v), *vy = float(y / mag * v);
}

}  // namespace

int State(st::Machine& m) { return int16_t(Get16(m, kStateVar)); }

void LoadSettings() {
  g_tune.speed = settings::Get("trip_stick_speed", 5.0);
  g_tune.curve = settings::Get("trip_stick_curve", 2.0);
  g_tune.deadzone = std::clamp(settings::Get("trip_stick_deadzone", 0.15), 0.0, 0.9);
  g_tune.spiro = std::clamp(settings::Get("trip_spiro_size", 0.9), 0.1, 1.0);
  g_music_mode = std::clamp(int(settings::Get("trip_music", 0)), 0, 2);
  g_calm = settings::Get("trip_calm_background", 1) != 0;
}

void Reset() {
  g_taps.clear();
  g_effect = g_symmetry = g_palette = g_wave = -1;
  g_latch = g_lt_held = g_quit = g_test = false;
  g_held = 0;
  g_px = g_py = 0;
  g_hint_until = g_quit_armed_ms = 0;
  g_hint.clear();
  g_was_music = g_heard = g_music_key = false;
  g_pulse = g_beats = 0;
  g_manual_until = 0;
}

void SetTestPad(const TestPad& t) {
  g_test_pad = t;
  g_test = true;
}

bool QuitRequested() { return g_quit; }

// Music (Neon's analysis, music.h) while performing: the light source orbits a centre the player moves (left
// stick / mouse) - radius from the bass, speed from the mids, a shimmer from the treble; beats pulse pattern
// generation. Full also changes palette / symmetry / effect on musical accents (not for a while after the player
// chose one) and clears the screen when the music stops. The right stick, RT and the latch still play on top.
void Music(st::Machine* m, const music::Frame& f, int* dx, int* dy, uint8_t* buttons, bool right_stick) {
  constexpr double dt = 1.0 / 50;
  g_music_time += dt;
  // The centre: moved by the player instead of the source itself (it wraps like the source).
  g_cx = std::fmod(g_cx + float(*dx) + 320.0f, 320.0f), g_cy = std::fmod(g_cy + float(*dy) + 200.0f, 200.0f);
  *dx = *dy = 0;
  if (!right_stick) {
    g_angle += dt * (0.5 + 5.0 * f.mid);
    double r = 6 + 95 * std::pow(f.bass, 1.4) + 10 * f.treble * std::sin(g_music_time * 23);
    int x = int(std::lround(g_cx + r * 1.35 * std::cos(g_angle)));
    int y = int(std::lround(g_cy + r * 0.85 * std::sin(g_angle)));
    Put16(*m, kPointerX, uint16_t((x % 320 + 320) % 320));
    Put16(*m, kPointerY, uint16_t((y % 200 + 200) % 200));
  }
  // A beat holds the button for most of a beat at 120 bpm (0.24-0.44 s, longer for stronger beats): patterns
  // breathe in time; a new beat restarts it.
  if (f.beat) g_pulse = 12 + int(10 * f.beat_strength), ++g_beats;
  if (int(g_music_time * 50) % 250 == 0)  // every 5 s in the log
    LOG("music: %d beats so far, bass %.2f mid %.2f treble %.2f%s", g_beats, f.bass, f.mid, f.treble,
        f.silent ? " (silent)" : "");
  if (g_pulse > 0) --g_pulse, *buttons |= 2;
  if (g_music_mode < 2) return;
  bool free = SDL_GetTicks() >= g_manual_until;
  if (f.beat && free) {
    if (g_beats % 16 == 0) {
      Step(m, &g_palette, kPalettes, 10, 1 + int(g_rng() % 9));
      ShowState();
      LOG("music: palette %s", kPalettes[g_palette].name);
    } else if (g_beats % 32 == 8) {
      g_effect = int(g_rng() % 26);
      TapKey(m, kEffects[g_effect].code);
      ShowState();
      LOG("music: effect %s", kEffects[g_effect].name);
    } else if (f.beat_strength > 0.75f && g_music_time - g_last_symmetry > 6) {
      g_symmetry = int(g_rng() % 8);
      TapKey(m, kSymmetries[g_symmetry].code);
      g_last_symmetry = g_music_time;
      ShowState();
      LOG("music: symmetry %s", kSymmetries[g_symmetry].name);
    }
  }
  if (f.silent_for > 2.0 && g_heard) TapKey(m, kSpace), g_heard = false, LOG("music: silence - cleared");  // the track ended: a clean screen
  if (!f.silent) g_heard = true;
}

void CycleMusic() {
  g_music_mode = (g_music_mode + 1) % 3;
  settings::Set("trip_music", g_music_mode);
  settings::Save();
  bool ok = g_music_mode == 0 || music::StartLoopback();
  static const char* const kNames[3] = {"MUSIC OFF", "MUSIC: PULSE", "MUSIC: FULL"};
  g_hint = std::string(kNames[g_music_mode]) + (ok ? "" : " (CAN'T HEAR THE PC)");
  g_hint_until = SDL_GetTicks() + 2500;
  LOG("Trip-A-Tron music mode %d", g_music_mode);
}

void SetMusicMode(int mode) {
  g_music_mode = std::clamp(mode, 0, 2);
  if (g_music_mode) music::StartLoopback();
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
  uint8_t buttons = mouse_buttons;
  bool test = g_test;
  g_test = false;
  bool performing = State(*m) == 0;
  // The palettes' background flashes (4: red / green, 5: white, 6: a constant strobe) as soft pulses.
  m->SetCalmBackground(performing && g_calm);
  bool have_pad = pad || test;
  auto axis = [&](SDL_GamepadAxis a) {
    if (test) {
      switch (a) {
        case SDL_GAMEPAD_AXIS_LEFTX: return g_test_pad.lx;
        case SDL_GAMEPAD_AXIS_LEFTY: return g_test_pad.ly;
        case SDL_GAMEPAD_AXIS_RIGHTX: return g_test_pad.rx;
        case SDL_GAMEPAD_AXIS_RIGHTY: return g_test_pad.ry;
        case SDL_GAMEPAD_AXIS_LEFT_TRIGGER: return g_test_pad.lt;
        case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: return g_test_pad.rt;
        default: return 0.0f;
      }
    }
    return pad ? SDL_GetGamepadAxis(pad, a) / 32767.0f : 0.0f;
  };
  uint32_t held = 0;
  if (have_pad)
    for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT && b < 32; ++b)
      if (test ? (g_test_pad.buttons >> b & 1) : SDL_GetGamepadButton(pad, SDL_GamepadButton(b))) held |= 1u << b;
  uint32_t pressed = held & ~g_held;
  g_held = held;
  auto press = [&](SDL_GamepadButton b) { return (pressed >> b & 1) != 0; };
  auto down = [&](SDL_GamepadButton b) { return (held >> b & 1) != 0; };
  bool rt = axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 0.5f, lt = axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 0.5f;
  bool lt_pressed = lt && !g_lt_held;
  g_lt_held = lt;
  auto chose = [&]() { g_manual_until = SDL_GetTicks() + 15000; };

  if (press(SDL_GAMEPAD_BUTTON_EAST)) {  // B twice: back to the launcher
    if (SDL_GetTicks() < g_quit_armed_ms) g_quit = true;
    else g_quit_armed_ms = SDL_GetTicks() + 3000;
  }
  if (press(SDL_GAMEPAD_BUTTON_NORTH)) TapKey(m, kEsc);  // the Control Panel (and out of edit screens)
  if (press(SDL_GAMEPAD_BUTTON_LEFT_STICK) || g_music_key) CycleMusic();
  g_music_key = false;

  // Left stick: relative movement (both modes).
  float vx, vy;
  Curve(axis(SDL_GAMEPAD_AXIS_LEFTX), axis(SDL_GAMEPAD_AXIS_LEFTY), &vx, &vy);
  bool right_stick = false;
  if (performing) {
    // Right stick: the spirograph - the stick's position is the light source's position on the screen.
    float rx = axis(SDL_GAMEPAD_AXIS_RIGHTX), ry = axis(SDL_GAMEPAD_AXIS_RIGHTY);
    if (rx * rx + ry * ry > 0.08f * 0.08f) {
      int x = std::clamp(int(std::lround(160 + rx * 160 * g_tune.spiro)), 0, 319);
      int y = std::clamp(int(std::lround(100 + ry * 100 * g_tune.spiro)), 0, 199);
      Put16(*m, kPointerX, uint16_t(x));
      Put16(*m, kPointerY, uint16_t(y));
      g_px = g_py = 0;
      right_stick = true;
    }
    if (lt_pressed) g_latch = !g_latch, ShowState();
    if (rt || g_latch) buttons |= 2;
    if (press(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) Step(m, &g_effect, kEffects, 26, 1), ShowState(), chose();
    if (press(SDL_GAMEPAD_BUTTON_DPAD_LEFT)) Step(m, &g_effect, kEffects, 26, -1), ShowState(), chose();
    if (press(SDL_GAMEPAD_BUTTON_DPAD_DOWN)) Step(m, &g_symmetry, kSymmetries, 8, 1), ShowState(), chose();
    if (press(SDL_GAMEPAD_BUTTON_DPAD_UP)) Step(m, &g_symmetry, kSymmetries, 8, -1), ShowState(), chose();
    if (press(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) Step(m, &g_palette, kPalettes, 10, 1), ShowState(), chose();
    if (press(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)) Step(m, &g_palette, kPalettes, 10, -1), ShowState(), chose();
    if (press(SDL_GAMEPAD_BUTTON_SOUTH)) {  // surprise me
      g_effect = int(g_rng() % 26), g_symmetry = int(g_rng() % 8), g_palette = int(g_rng() % 10);
      TapKey(m, kEffects[g_effect].code), TapKey(m, kSymmetries[g_symmetry].code), TapKey(m, kPalettes[g_palette].code);
      ShowState();
      chose();
    }
    if (press(SDL_GAMEPAD_BUTTON_WEST)) {
      TapKey(m, kSpace);
      g_hint = "PATTERNS STOPPED", g_hint_until = SDL_GetTicks() + 1500;
    }
    if (press(SDL_GAMEPAD_BUTTON_START)) TapKey(m, kF10);
    if (press(SDL_GAMEPAD_BUTTON_BACK)) {
      Step(m, &g_wave, kWaves, 10, 1);
      g_hint = std::string("WAVEFORM ") + kWaves[g_wave].name, g_hint_until = SDL_GetTicks() + 1500;
    }
  } else {
    // A mouse: both sticks move the pointer; A / RT left button, X / LT right button.
    float wx, wy;
    Curve(axis(SDL_GAMEPAD_AXIS_RIGHTX), axis(SDL_GAMEPAD_AXIS_RIGHTY), &wx, &wy);
    vx += wx, vy += wy;
    if (down(SDL_GAMEPAD_BUTTON_SOUTH) || rt) buttons |= 2;
    if (down(SDL_GAMEPAD_BUTTON_WEST) || lt) buttons |= 1;
    g_latch = false;
  }
  g_px += vx, g_py += vy;
  int dx = int(g_px), dy = int(g_py);
  g_px -= float(dx), g_py -= float(dy);
  dx += mouse_dx, dy += mouse_dy;
  if (g_music_mode && !music::Active()) music::StartLoopback();  // (once: it remembers a failure)
  bool music_on = g_music_mode && music::Active() && performing;
  if (music_on) {
    music::Frame f = music::Analyse(1.0 / 50);
    if (!g_was_music) g_cx = float(Get16(*m, kPointerX)), g_cy = float(Get16(*m, kPointerY));
    Music(m, f, &dx, &dy, &buttons, right_stick);
  }
  g_was_music = music_on;
  m->MouseInput(dx, dy, buttons);
}

void MusicKey() { g_music_key = true; }

int MusicMode() { return g_music_mode; }

const char* Hint(st::Machine* m, bool have_pad) {
  if (SDL_GetTicks() < g_quit_armed_ms) return "Press B again to leave Trip-A-Tron";
  if (SDL_GetTicks() < g_hint_until) return g_hint.c_str();
  return nullptr;
}

}  // namespace trip
