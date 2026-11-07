#include "tripatron.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
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
// On the Control Panel (state 2): an alert box is up / a side panel (Sym, Cube Face) replaces the colour
// presets; which side panel (1 = a Sym panel, 0 = Cube Face). Found by RAM diffs, 2026-11-07.
constexpr uint32_t kAlertUp = 0x10A78, kSidePanel = 0x1036E, kSymPanel = 0x16AFE;
// KML's system variables (TRIP.PRG's name table at text $2F2C3, addresses right after it at $2F3BA): the
// starfield's speed in depth and spin; $23AEC = the starfield running (0 = none; found by a RAM diff).
// The plot routine's enable mask (bit 0 pre-symmetry ... bit 5 post-symmetry: KML's FXENABLE / FXSTATE).
constexpr uint32_t kFxMask = 0x15B30;
// The KML bank in memory: the programs (event n = the n-th, each ended by $FFFF; ROUTINE / JUMP_TO count from 1),
// the keys' events (a word per ST scancode; $FFFE / $FFFF = none) and the command names (text $2ED74: name, a
// space, the argument count, $FF). Numbers are 14-bit signed; $8000 | n = system variable n, $4000 | n = the
// letter variable, $C000 | n = a binary constant.
constexpr uint32_t kKmlPrograms = 0x3C4DC, kKmlKeys = 0x3E632, kKmlCommands = 0x3EE74;
constexpr uint32_t kStarSvz = 0x23AF2, kStarAngle = 0x23B04, kStarfieldOn = 0x23AEC;  // (ABS_ANGLE 0..255)

// ST scancodes.
constexpr uint8_t kSpace = 0x39, kEsc = 0x01, kF1 = 0x3B, kF10 = 0x44, kDelete = 0x53;
struct Key {
  uint8_t code;
  const char* name;
};
// Effects: the three letter rows of the keyboard, as the manual lays out demo 1 (Q row decay, A row line,
// Z row expandor effects; demo 2: the A row starfields, / = starfields off). Each row is a "family".
const Key kRowQ[] = {{0x10, "Q"}, {0x11, "W"}, {0x12, "E"}, {0x13, "R"}, {0x14, "T"}, {0x15, "Y"},
                     {0x16, "U"}, {0x17, "I"}, {0x18, "O"}, {0x19, "P"}, {0x1A, "["}, {0x1B, "]"}};
const Key kRowA[] = {{0x1E, "A"}, {0x1F, "S"}, {0x20, "D"}, {0x21, "F"}, {0x22, "G"}, {0x23, "H"},
                     {0x24, "J"}, {0x25, "K"}, {0x26, "L"}, {0x27, ";"}, {0x28, "'"}, {0x2B, "#"}};
const Key kRowZ[] = {{0x60, "\\"}, {0x2C, "Z"}, {0x2D, "X"}, {0x2E, "C"}, {0x2F, "V"}, {0x30, "B"},
                     {0x31, "N"},  {0x32, "M"}, {0x33, ","}, {0x34, "."}, {0x35, "/"}};
struct Family {
  const Key* keys;
  int n;
  const char* name;
};
const Family kFamilies[3] = {{kRowQ, 12, "Q ROW"}, {kRowA, 12, "A ROW"}, {kRowZ, 11, "Z ROW"}};
constexpr int kEffectCount = 12 + 12 + 11;
const Key& EffectKey(int i) { return i < 12 ? kRowQ[i] : i < 24 ? kRowA[i - 12] : kRowZ[i - 24]; }
int EffectFamily(int i) { return i < 12 ? 0 : i < 24 ? 1 : 2; }
int FamilyStart(int f) { return f == 0 ? 0 : f == 1 ? 12 : 24; }
// Symmetries: the cursor cluster, in wheel order from the top clockwise, placed as on the ST's keyboard
// (Insert and Clr/Home either side of up, Help / Undo beyond them).
const Key kSymmetries[8] = {{0x48, "UP"},   {0x47, "HOME"}, {0x4D, "RIGHT"}, {0x61, "UNDO"},
                            {0x50, "DOWN"}, {0x62, "HELP"}, {0x4B, "LEFT"},  {0x52, "INS"}};
// Palettes: the numeric pad's digits.
const Key kPalettes[10] = {{0x6D, "1"}, {0x6E, "2"}, {0x6F, "3"}, {0x6A, "4"}, {0x6B, "5"},
                           {0x6C, "6"}, {0x67, "7"}, {0x68, "8"}, {0x69, "9"}, {0x70, "0"}};
// The main keyboard's number row 1 ... 0 - = (the manual: demo 1 a random palette, demo 2 the lasers).
const Key kNumbers[12] = {{0x02, "1"}, {0x03, "2"}, {0x04, "3"}, {0x05, "4"}, {0x06, "5"}, {0x07, "6"},
                          {0x08, "7"}, {0x09, "8"}, {0x0A, "9"}, {0x0B, "0"}, {0x0C, "-"}, {0x0D, "="}};
// The plot routine's six stages (the panel's bottom row, F1-F6), with their pad buttons on the Y layer.
const char* const kStages[6] = {"PRE-SYM", "REMAP", "CUBE", "SHEAR", "RESIZE", "POST-SYM"};
const char* const kStageButtons[6] = {"LB", "A", "X", "B", "START", "RB"};

// B on an edit screen: how that screen is left (the manual's "to leave" for each), by state ($108B0).
// x < 0: Esc (the keyboard-driven screens); otherwise a left click on its exit button at (x, y).
struct Exit {
  int state, x, y;
  const char* name;
};
const Exit kExits[] = {
    {5, 122, 196, "RESIZE EDIT"},        {7, 303, 60, "WAVEFORM BUILD"},     {9, 87, 177, "OSCILLATOR FX"},
    {10, -1, 0, "KML EDITOR"},           {11, 257, 166, "EVENT ASSIGN"},     {12, 273, 85, "DISK ACCESS"},
    {13, 196, 89, "VIDEO SEQUENCER"},    {14, 298, 168, "EVENT SEQUENCER"},  {15, -1, 0, "STARFIELD EDIT"},
    {16, 276, 191, "PATTERN EDIT"},      {17, 36, 186, "COLOUR COOKER"},     {18, 250, 189, "MIDI"},
    {19, 281, 166, "GLOBAL PARAMETERS"}, {20, 73, 194, "SILLYSCOPE"},
};

// The Control Panel as a grid for the d-pad: 6 icon columns + 3 colour preset columns, 3 rows; what the left
// (A) and right (X) buttons do on each (the manual's "naming of parts").
const int kGridX[9] = {18, 52, 85, 118, 150, 185, 238, 270, 303};
const int kGridY[3] = {137, 163, 187};
struct Icon {
  const char* name;
  const char* a;
  const char* x;
};
const Icon kIcons[3][6] = {
    {{"DISK ACCESS", "OPEN", "OPEN"},
     {"MIDI", "OPEN", "OPEN"},
     {"EVENT SEQUENCER", "OPEN", "OPEN"},
     {"GLOBAL PARAMETERS", "OPEN", "OPEN"},
     {"SILLYSCOPE", "OPEN", "OPEN"},
     {"ABOUT", "VERSION", "VERSION"}},
    {{"WAVEFORMS", "OSCILLATOR FX", "BUILD WAVES"},
     {"PATTERNS", "EDIT", "EDIT"},
     {"COLOUR COOKER", "OPEN", "OPEN"},
     {"STARFIELD EDIT", "OPEN", "OPEN"},
     {"KML", "KEY ASSIGN", "PROGRAM EDIT"},
     {"VIDEO SEQUENCER", "OPEN", "OPEN"}},
    {{"PRE-SYMMETRY", "ON/OFF", "SET SYMMETRY"},
     {"REMAP", "ON/OFF", "PICK MAP"},
     {"CUBIC REMAP", "ON/OFF", "CUBE FACES"},
     {"SHEAR", "ON/OFF", "-"},
     {"RESIZE", "ON/OFF", "EDIT BOXES"},
     {"POST-SYMMETRY", "ON/OFF", "SET SYMMETRY"}},
};

struct Tune {
  double speed = 5, curve = 2, deadzone = 0.15;  // left stick: pixels per frame at full deflection
  double spiro = 0.9;                             // right stick: fraction of the half-screen full deflection reaches
  double flow = 0;                                // left stick inertia, 0 (none) .. 1 (long glides)
  bool pressure = true;                           // RT's squeeze sets how often patterns start
  bool gyro = false;                              // the pad's gyro moves the light / pointer too
  double gyro_speed = 1;
  bool rumble = true;                             // music mode: a light rumble on beats
} g_tune;

constexpr double kPi = 3.14159265358979323846;
constexpr int kHold = 11;  // frames: a shoulder / Y held this long is a hold (a wheel, the Y layer), not a tap

struct Tap {
  uint8_t code;
  int frames;  // > 0: held for this many frames; Later: < 0 = frames until it is pressed
};
std::vector<Tap> g_taps, g_later;
bool g_calm = true;  // trip_calm_background: the background's flashes toned down (owner 2026-11-01)
int g_effect = -1, g_symmetry = -1, g_palette = -1, g_number = -1;  // -1: not chosen from the pad yet
bool g_latch = false;
uint32_t g_held = 0;                // pad buttons held last frame (edges)
float g_px = 0, g_py = 0;           // stick movement not sent yet (sub-pixel)
float g_fvx = 0, g_fvy = 0;         // the light's velocity with flow (inertia)
float g_paint = 0;                  // RT pressure: pattern starts owed (a start each time it passes 1)
uint64_t g_hint_until = 0;
std::string g_hint;
bool g_resync = false;   // the settings panel closed: what's held now makes no presses
bool g_greeted = false;  // the first hint shown
// Hold-to-choose: LB = effect wheel (a family), RB = symmetry wheel; Y = the plot layer. Frames held (-1 = up).
enum class Wheel { kNone, kEffect, kSymmetry };
Wheel g_wheel = Wheel::kNone;
int g_lb_frames = -1, g_rb_frames = -1, g_y_frames = -1;
bool g_y_used = false;
int g_family = 0, g_wheel_sel = -1;
float g_wheel_x = 0, g_wheel_y = 0;  // the right stick, for the overlay's pointer
bool g_pre = false;                   // the symmetry wheel sets the pre-symmetry (Y held)
int g_cell = -1;                      // the Control Panel's icon picked with the d-pad (row * 9 + column)
int g_dpad_frames = 0;                // tool screens: d-pad held (nudge repeat)
SDL_Gamepad* g_gyro_pad = nullptr;    // the pad whose gyro is switched on
// Flying a starfield: the right stick and RT offset its own speed / spin, which come back when let go.
bool g_flying = false;
int16_t g_base_svz = 0;                  // the starfield's own speed (as its KML left it)
float g_fly_svz = 0, g_fly_spin = 0;      // our speed offset and spin (angle units a frame), eased
float g_spin_acc = 0;                     // spin not applied yet (fractions of an angle unit)
// A click on an exit button (B): the pointer goes there, the left button is pressed for a few frames. Screens
// ignore clicks while they slide in / load (the Disk screen: ~2 s), so it is repeated every 30 frames until the
// state changes (8 tries).
struct Click {
  bool active = false;
  int x = 0, y = 0, frame = 0, state = 0, tries = 0;  // frame < 0: waiting before the next try
} g_click;
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

bool g_log_keys = false;  // scripted runs (a test pad seen): every key sent is logged

void TapKey(st::Machine* m, uint8_t code) {
  if (g_log_keys) LOG("trip key $%02X", code);
  m->KeyEvent(code, true);
  g_taps.push_back({code, 3});
}
// A key a few frames from now (after another key: Delete, then the symmetry it applies to).
void TapLater(uint8_t code, int frames) { g_later.push_back({code, -frames}); }

void SetHint(const std::string& text, int ms) {
  g_hint = text;
  g_hint_until = SDL_GetTicks() + uint64_t(ms);
}

void StartClick(int x, int y, int state) { g_click = {true, x, y, 0, state, 0}; }

// B everywhere but performing: back towards the Control Panel - an edit screen by its own exit button (or
// Esc), an alert by its button, a side panel by OK / GROOVY; on the bare panel, put the panel away.
void Back(st::Machine* m, int state) {
  if (state == 4 || (state == 2 && Get16(*m, kSidePanel))) {  // a side panel (4: still sliding in)
    if (Get16(*m, kSymPanel)) StartClick(308, 131, -3);  // OK
    else StartClick(299, 194, -3);                        // GROOVY
    return SetHint("BACK TO THE CONTROL PANEL", 1200);
  }
  if (state == 2) {
    if (Get16(*m, kAlertUp)) {
      StartClick(136, 47, -2);  // every alert's button row (a two-button alert: the left one, the "no")
      return SetHint("ALERT CLOSED", 1200);
    }
    TapKey(m, kEsc);  // the panel itself: away, back to performing
    return;
  }
  for (const Exit& e : kExits) {
    if (e.state != state) continue;
    if (e.x < 0) TapKey(m, kEsc);
    else StartClick(e.x, e.y, state);
    LOG("Trip-A-Tron: B leaves %s (state %d)", e.name, state);
    return SetHint(std::string("LEAVING ") + e.name, 1200);
  }
  if (state != 1 && state != 3) {
    LOG("Trip-A-Tron: B in an unknown state %d - Esc", state);
    TapKey(m, kEsc);
  }
}

std::string g_effect_text;           // what the current effect key does (the loaded KML)
std::vector<std::string> g_row_text;  // the effect wheel's row: each key's description ("" = nothing)
int g_fx_mask = -1;                   // the plot routine's stages on (bits), for the Y layer's picture
bool g_no_gyro_told = false;          // "this pad has no gyro" shown once

void ShowState() {
  char buf[160];
  std::snprintf(buf, sizeof(buf), "%s%s%s   SYMMETRY %s   PALETTE %s%s", g_effect >= 0 ? EffectKey(g_effect).name : "-",
                g_effect_text.empty() ? "" : ": ", g_effect_text.c_str(),
                g_symmetry >= 0 ? kSymmetries[g_symmetry].name : "-", g_palette >= 0 ? kPalettes[g_palette].name : "-",
                g_latch ? "   LATCHED" : "");
  SetHint(buf, 2500);
}

// ---- What each key does: its KML program, described in a few words ----
struct Kml {
  std::vector<std::string> names;  // command names by number
  std::vector<int> argc;
  std::vector<std::vector<uint16_t>> programs;  // by event
};

Kml ReadKml(st::Machine& m) {
  Kml k;
  const uint8_t* ram = m.Ram();
  for (uint32_t a = kKmlCommands; k.names.size() < 200 && ram[a] >= 'A' && ram[a] <= 'Z';) {
    std::string name;
    while (ram[a] != 0xFF && name.size() < 24) name += char(ram[a++]);
    ++a;
    if (name.size() < 2) break;
    k.argc.push_back(uint8_t(name.back()));
    name.pop_back();
    while (!name.empty() && name.back() == ' ') name.pop_back();
    k.names.push_back(name);
  }
  uint32_t a = kKmlPrograms;
  for (int ev = 0; ev < 128 && a < kKmlKeys; ++ev) {
    std::vector<uint16_t> prog;
    for (uint16_t w; (w = Get16(m, a)) != 0xFFFF && a < kKmlKeys; a += 2) prog.push_back(w);
    a += 2;
    k.programs.push_back(std::move(prog));
  }
  return k;
}

int KeyEvent(st::Machine& m, uint8_t code) {
  uint16_t ev = Get16(m, kKmlKeys + uint32_t(code) * 2);
  return ev >= 128 ? -1 : int(ev);
}

int Number(uint16_t w) {  // a 14-bit constant
  int v = w & 0x3FFF;
  return v & 0x2000 ? v - 0x4000 : v;
}

std::string Describe(const Kml& k, int ev, int depth = 0) {
  if (ev < 0 || ev >= int(k.programs.size()) || k.programs[ev].empty()) return "";
  const std::vector<uint16_t>& p = k.programs[ev];
  std::vector<int> atypes;
  int pattern = -1, patterns = 0, palette = -2, fx = -1, set_binary = -1, laser = 0, scope_off = 0, cursor = -1;
  bool loop = false, stars = false, stars_off = false, sym = false, strobe = false, random = false, steer = false,
       button = false, cls = false, macro = false, killall = false, sym_pre = false, fx_toggle = false;
  bool warp = false, drift = false, spin = false, waves = false;  // how a starfield moves
  int target = -1;
  for (size_t i = 0; i < p.size();) {
    int c = p[i];
    if (c >= int(k.names.size())) break;
    const std::string& n = k.names[c];
    int argc = k.argc[c];
    auto arg = [&](int j) -> uint16_t { return i + 1 + j < p.size() ? p[i + 1 + j] : 0; };
    auto is_var = [&](int j, int var) { return (arg(j) & 0xC000) == 0x8000 && (arg(j) & 0x3FFF) == var; };
    for (int j = 0; j < argc; ++j)
      if ((arg(j) & 0xC000) == 0x8000) {
        int v = arg(j) & 0x3FFF;
        if (v == 0 || v == 1) steer = true;  // CX / CY
        if (v == 2) button = true;           // BUTTONS
      }
    if (n == "SET" && (arg(1) & 0xC000) == 0x8000) {  // a starfield's motion: SVX 3, SVY 4, SVZ 5, ANG 6
      int v = arg(1) & 0x3FFF;
      bool moving = (arg(0) & 0xC000) != 0 || Number(arg(0)) != 0;
      if (moving && v == 5) warp = true;
      if (moving && (v == 3 || v == 4)) drift = true;
      if (moving && v == 6) spin = true;
    }
    if (n == "SINE" || n == "COSINE" || n == "USER_WAVE") waves = true;
    if (n == "ATYPE" && (arg(0) & 0xC000) == 0) {
      int t = Number(arg(0));
      if (std::find(atypes.begin(), atypes.end(), t) == atypes.end()) atypes.push_back(t);
    } else if (n == "SET" && is_var(1, 21)) {  // PATTERN
      ++patterns;
      if ((arg(0) & 0xC000) == 0) pattern = Number(arg(0));
      else pattern = -2;
    } else if (n == "SET" && (arg(0) & 0xC000) == 0xC000) {
      set_binary = arg(0) & 0xFF;
    } else if (n == "SET" && (arg(1) & 0xC000) == 0x8000 && (arg(1) & 0x3FFF) >= 24 && (arg(1) & 0x3FFF) < 50) {
      if ((arg(1) & 0x3FFF) == 24 + ('S' - 'A') && Number(arg(0)) == 1) sym_pre = true;  // SET 1,GS
    } else if (n == "LOOP" || n == "FOREVER") {
      loop = true;
    } else if (n == "STARFIELD") {
      if ((arg(0) & 0xC000) == 0 && Number(arg(0)) == 0) stars_off = true;
      else stars = true;
    } else if (n == "SCOPE") {
      for (int j = 0; j < 2; ++j)
        if ((arg(j) & 0xC000) == 0 && Number(arg(j)) == 0) scope_off |= 1 << j;
    } else if (n == "LASER") {
      laser |= 1 << std::clamp(Number(arg(0)) - 1, 0, 1);
    } else if (n == "SYM") {
      sym = true;
    } else if (n == "STROBE") {
      strobe = true;
    } else if (n == "PALETTE") {
      palette = (arg(0) & 0xC000) == 0 ? Number(arg(0)) : -1;
    } else if (n == "RND") {
      random = true;
    } else if (n == "FXENABLE") {
      fx = (arg(0) & 0xC000) == 0xC000 ? (arg(0) & 0x3F) : -1;
    } else if (n == "FXSTATE") {
      fx_toggle = true;
    } else if (n == "CURSOR") {
      cursor = Number(arg(1));
    } else if (n == "CLS") {
      cls = true;
    } else if (n == "MACRO_LOAD") {
      macro = true;
    } else if (n == "KILLALL") {
      killall = true;
    } else if ((n == "JUMP_TO" || n == "ROUTINE") && (arg(0) & 0xC000) == 0) {
      target = Number(arg(0)) - 1;
    }
    i += 1 + size_t(argc);
  }
  static const char* const kModes[3] = {"DECAY", "LINE", "EXPANDOR"};
  if (macro) return "LOAD THE OTHER DEMO";
  if (stars) {
    std::string d;
    for (auto [on, word] : {std::pair{warp, "WARP"}, {drift, "DRIFT"}, {spin, "SPIN"}, {waves, "WAVES"}, {random, "RANDOM"}})
      if (on) d += (d.empty() ? "" : "+") + std::string(word);
    if (steer) d += d.empty() ? "LIGHT STEERS" : ", LIGHT STEERS";
    if (button) d += d.empty() ? "BUTTONS TOO" : ", BUTTONS TOO";
    return "STARFIELD" + (d.empty() ? "" : ": " + d);
  }
  if (laser) return std::string("LASER ") + (laser == 3 ? "1+2" : laser == 1 ? "1" : "2") + (steer ? ": LIGHT STEERS" : "");
  if (stars_off && scope_off == 3) return "STARS AND LASERS OFF";
  if (stars_off) return "STARFIELD OFF";
  if (scope_off == 1) return "LASER 1 OFF";
  if (scope_off == 2) return "LASER 2 OFF";
  if (scope_off == 3) return "LASERS OFF";
  if (!atypes.empty()) {
    std::string mode = atypes.size() > 1 ? "MIXED" : atypes[0] >= 0 && atypes[0] < 3 ? kModes[atypes[0]] : "PATTERN";
    if (loop) return mode + " ANIMATED";
    if (pattern >= 0 && patterns == 1) return mode + " SHAPE " + std::to_string(pattern);
    return mode + " MODE";
  }
  if (cursor >= 0) return cursor ? "2ND CURSOR ON" : std::string("2ND CURSOR OFF") + (cls ? " + CLEAR" : "");
  if (fx_toggle && depth == 0) return "PLOT ROUTINE SWITCH";
  if (fx >= 0) return "PLOT: POST-SYM ONLY";
  if (sym) return "SYMMETRY";
  if (sym_pre) return "NEXT SYMMETRY: PRE";
  if (strobe) return "STROBE";
  if (palette >= -1 && random) return "RANDOM PALETTE";
  if (palette >= 0) return "PALETTE " + std::to_string(palette);
  if (killall) return "STOP ALL";
  if (cls) return "CLEAR SCREEN";
  if (target >= 0 && depth < 2) {
    // A small program that sets something up and jumps: name it by where it goes (the bit it sets, for FX).
    const Kml& kk = k;
    if (target < int(kk.programs.size())) {
      std::string t = Describe(kk, target, depth + 1);
      if (t == "PLOT ROUTINE SWITCH" && set_binary > 0) {
        for (int b = 0; b < 6; ++b)
          if (set_binary == 1 << b) return std::string("SWITCH ") + kStages[b];
      }
      if (!t.empty()) return t;
      if (fx_toggle) return "PLOT ROUTINE SWITCH";
    }
  }
  if (fx_toggle) return "PLOT ROUTINE SWITCH";
  return k.names.empty() || p[0] >= k.names.size() ? "?" : k.names[p[0]];
}

// The key's description in the loaded KML ("" = the key has no program).
std::string KeyDescription(st::Machine& m, const Kml& k, uint8_t code) { return Describe(k, KeyEvent(m, code)); }

// Steps a choice (-1 = none yet: the first step goes to the first / last) and taps its key.
void Step(st::Machine* m, int* index, const Key* keys, int n, int dir) {
  *index = *index < 0 ? (dir > 0 ? 0 : n - 1) : (*index + dir + n) % n;
  TapKey(m, keys[*index].code);
}
void StepEffect(st::Machine* m, int dir) {
  Kml k = ReadKml(*m);
  for (int tries = 0; tries < kEffectCount; ++tries) {  // the next key that does something in the loaded KML
    g_effect = g_effect < 0 ? (dir > 0 ? 0 : kEffectCount - 1) : (g_effect + dir + kEffectCount) % kEffectCount;
    if (!(g_effect_text = KeyDescription(*m, k, EffectKey(g_effect).code)).empty()) break;
  }
  TapKey(m, EffectKey(g_effect).code);
}
// A symmetry key; pre = the pre-symmetry (the manual: Delete makes the next symmetry key set it).
void ChooseSymmetry(st::Machine* m, int i, bool pre) {
  g_symmetry = i;
  if (pre) TapKey(m, kDelete), TapLater(kSymmetries[i].code, 5);
  else TapKey(m, kSymmetries[i].code);
}

void Curve(float x, float y, float* vx, float* vy, double speed) {
  float mag = std::sqrt(x * x + y * y);
  *vx = *vy = 0;
  if (mag <= g_tune.deadzone) return;
  double t = std::min(1.0, (mag - g_tune.deadzone) / (1.0 - g_tune.deadzone));
  double v = speed * std::pow(t, g_tune.curve);
  *vx = float(x / mag * v), *vy = float(y / mag * v);
}

// The wheel segment the right stick points at (n segments, the first at the top, clockwise); -1 = centred.
int WheelSegment(float x, float y, int n) {
  if (x * x + y * y < 0.5f * 0.5f) return -1;
  double a = std::atan2(double(x), double(-y));  // 0 = up, clockwise
  if (a < 0) a += 2 * kPi;
  return int(std::lround(a / (2 * kPi / n))) % n;
}

// The panel's grid cell nearest the pointer.
int NearestCell(int px, int py, int columns) {
  int best = 0, best_d = 1 << 30;
  for (int r = 0; r < 3; ++r)
    for (int c = 0; c < columns; ++c) {
      int d = (kGridX[c] - px) * (kGridX[c] - px) + (kGridY[r] - py) * (kGridY[r] - py);
      if (d < best_d) best_d = d, best = r * 9 + c;
    }
  return best;
}

void DescribeCell(int cell) {
  int r = cell / 9, c = cell % 9;
  std::string text;
  if (c < 6) {
    const Icon& i = kIcons[r][c];
    text = std::string(i.name) + "   A: " + i.a + (std::string(i.x) == "-" ? "" : std::string("   X: ") + i.x);
  } else {
    static const int kNumber[3][3] = {{7, 8, 9}, {4, 5, 6}, {1, 2, 3}};
    text = "COLOUR CHANNEL " + std::to_string(kNumber[r][c - 6]) + "   A: ON/OFF   X: EDIT COLOURS";
  }
  SetHint(text, 4000);
}

}  // namespace

int State(st::Machine& m) { return int16_t(Get16(m, kStateVar)); }

void LoadSettings() {
  g_tune.speed = settings::Get("trip_stick_speed", 5.0);
  g_tune.curve = settings::Get("trip_stick_curve", 2.0);
  g_tune.deadzone = std::clamp(settings::Get("trip_stick_deadzone", 0.15), 0.0, 0.9);
  g_tune.spiro = std::clamp(settings::Get("trip_spiro_size", 0.9), 0.1, 1.0);
  g_tune.flow = std::clamp(settings::Get("trip_flow", 0.0), 0.0, 1.0);
  g_tune.pressure = settings::Get("trip_pressure", 1) != 0;
  g_tune.gyro = settings::Get("trip_gyro", 0) != 0;
  g_tune.gyro_speed = std::clamp(settings::Get("trip_gyro_speed", 1.0), 0.1, 4.0);
  g_tune.rumble = settings::Get("trip_rumble", 1) != 0;
  g_music_mode = std::clamp(int(settings::Get("trip_music", 0)), 0, 2);
  g_calm = settings::Get("trip_calm_background", 1) != 0;
}

void Reset() {
  g_taps.clear(), g_later.clear();
  g_effect = g_symmetry = g_palette = g_number = -1;
  g_latch = g_test = g_resync = g_greeted = false;
  g_click = {};
  g_held = 0;
  g_px = g_py = g_fvx = g_fvy = g_paint = 0;
  g_hint_until = 0;
  g_hint.clear();
  g_wheel = Wheel::kNone, g_wheel_sel = -1;
  g_lb_frames = g_rb_frames = g_y_frames = -1;
  g_y_used = g_pre = false;
  g_cell = -1, g_dpad_frames = 0;
  g_gyro_pad = nullptr;
  g_flying = false;
  g_effect_text.clear(), g_row_text.clear();
  g_no_gyro_told = false;
  g_was_music = g_heard = g_music_key = false;
  g_pulse = g_beats = 0;
  g_manual_until = 0;
}

void SetTestPad(const TestPad& t) {
  g_test_pad = t;
  g_test = true;
  g_log_keys = true;
}

void Resync() { g_resync = true; }

// Music (Neon's analysis, music.h) while performing: the light source orbits a centre the player moves (left
// stick / mouse) - radius from the bass, speed from the mids, a shimmer from the treble; beats pulse pattern
// generation (and a light rumble). Full also changes palette / symmetry / effect on musical accents (not for a
// while after the player chose one) and clears the screen when the music stops. The right stick, RT and the
// latch still play on top.
void Music(st::Machine* m, SDL_Gamepad* pad, const music::Frame& f, int* dx, int* dy, uint8_t* buttons,
           bool right_stick) {
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
  if (f.beat) {
    g_pulse = 12 + int(10 * f.beat_strength), ++g_beats;
    if (g_tune.rumble && pad)
      SDL_RumbleGamepad(pad, uint16_t(9000 + 16000 * f.beat_strength), uint16_t(3000 + 6000 * f.beat_strength), 70);
  }
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
      Kml k = ReadKml(*m);
      for (int tries = 0; tries < 40; ++tries) {
        g_effect = int(g_rng() % kEffectCount);
        if (!(g_effect_text = KeyDescription(*m, k, EffectKey(g_effect).code)).empty()) break;
      }
      TapKey(m, EffectKey(g_effect).code);
      ShowState();
      LOG("music: effect %s", EffectKey(g_effect).name);
    } else if (f.beat_strength > 0.75f && g_music_time - g_last_symmetry > 6) {
      ChooseSymmetry(m, int(g_rng() % 8), false);
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
  SetHint(std::string(kNames[g_music_mode]) + (ok ? "" : " (CAN'T HEAR THE PC)"), 2500);
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
  for (size_t i = 0; i < g_later.size();) {  // keys due now
    if (++g_later[i].frames < 0) {
      ++i;
      continue;
    }
    TapKey(m, g_later[i].code);
    g_later.erase(g_later.begin() + long(i));
  }
  uint8_t buttons = mouse_buttons;
  bool test = g_test;
  g_test = false;
  const int state = State(*m);
  const bool performing = state == 0;
  g_fx_mask = m->Ram()[kFxMask] & 0x3F;
  if (g_log_keys && performing) {  // scripted runs: every effect key's description, when the loaded KML changes
    static uint32_t last_sum = 0;
    uint32_t sum = 0;
    for (uint32_t a = kKmlPrograms; a < kKmlKeys + 256; a += 2) sum = sum * 31 + Get16(*m, a);
    if (sum != last_sum) {
      last_sum = sum;
      Kml k = ReadKml(*m);
      for (int i = 0; i < kEffectCount; ++i)
        if (std::string d = KeyDescription(*m, k, EffectKey(i).code); !d.empty()) LOG("kml key %s: %s", EffectKey(i).name, d.c_str());
      for (const Key& n : kNumbers)
        if (std::string d = KeyDescription(*m, k, n.code); !d.empty()) LOG("kml number %s: %s", n.name, d.c_str());
    }
  }
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
  if (g_resync) g_held = held, g_lb_frames = g_rb_frames = g_y_frames = -1, g_wheel = Wheel::kNone, g_resync = false;
  uint32_t pressed = held & ~g_held, released = g_held & ~held;
  g_held = held;
  auto press = [&](SDL_GamepadButton b) { return (pressed >> b & 1) != 0; };
  auto release = [&](SDL_GamepadButton b) { return (released >> b & 1) != 0; };
  auto down = [&](SDL_GamepadButton b) { return (held >> b & 1) != 0; };
  const float rt_axis = axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER), lt_axis = axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
  const bool rt = rt_axis > 0.5f, lt = lt_axis > 0.5f;
  const float rx = axis(SDL_GAMEPAD_AXIS_RIGHTX), ry = axis(SDL_GAMEPAD_AXIS_RIGHTY);
  auto chose = [&]() { g_manual_until = SDL_GetTicks() + 15000; };

  if (!g_greeted && have_pad && state == 1) {
    g_greeted = true;
    SetHint("BACK: SETTINGS / LEAVE    B: BACK TO THE PANEL", 6000);
  }
  if (press(SDL_GAMEPAD_BUTTON_LEFT_STICK) || g_music_key) CycleMusic();
  g_music_key = false;

  // Gyro (Steam Deck, DualShock / DualSense, Switch pads): turning the pad moves the light / pointer.
  float gx = 0, gy = 0;
  if (g_tune.gyro && pad) {
    if (!SDL_GamepadHasSensor(pad, SDL_SENSOR_GYRO) && !g_no_gyro_told) {
      g_no_gyro_told = true;
      LOG("Trip-A-Tron: %s has no gyro", SDL_GetGamepadName(pad));
      SetHint("THIS PAD HAS NO GYRO HERE (STEAM INPUT? TURN IT OFF FOR THIS GAME)", 6000);
    }
    if (g_gyro_pad != pad && SDL_GamepadHasSensor(pad, SDL_SENSOR_GYRO)) {
      SDL_SetGamepadSensorEnabled(pad, SDL_SENSOR_GYRO, true);
      g_gyro_pad = pad;
      LOG("Trip-A-Tron: gyro on (%s)", SDL_GetGamepadName(pad));
    }
    float g[3] = {};
    if (g_gyro_pad == pad && SDL_GetGamepadSensorData(pad, SDL_SENSOR_GYRO, g, 3)) {
      const float k = float(300.0 * g_tune.gyro_speed / 50.0);  // pixels per radian, per frame
      gx = -g[1] * k, gy = -g[0] * k;                           // yaw -> x, pitch -> y
    }
  } else if (g_gyro_pad && !g_tune.gyro) {
    SDL_SetGamepadSensorEnabled(g_gyro_pad, SDL_SENSOR_GYRO, false);
    g_gyro_pad = nullptr;
  }

  float vx, vy;
  bool right_stick = false;
  if (performing) {
    g_cell = -1;
    // Y: a tap = the Control Panel; held = the plot layer (its buttons below instead of their own).
    if (press(SDL_GAMEPAD_BUTTON_NORTH)) g_y_frames = 0, g_y_used = false;
    if (g_y_frames >= 0 && down(SDL_GAMEPAD_BUTTON_NORTH)) ++g_y_frames;
    if (release(SDL_GAMEPAD_BUTTON_NORTH)) {
      if (!g_y_used && g_y_frames < kHold && g_wheel == Wheel::kNone) TapKey(m, kEsc);
      g_y_frames = -1;
    }
    const bool layer = g_y_frames >= 0;
    if (layer && g_wheel == Wheel::kNone) {
      static const SDL_GamepadButton kStageButton[6] = {
          SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_WEST,
          SDL_GAMEPAD_BUTTON_EAST,          SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER};
      for (int i = 0; i < 6; ++i)
        if (press(kStageButton[i])) {
          TapKey(m, uint8_t(kF1 + i)), g_y_used = true, chose();
          SetHint(std::string(kStages[i]) + " SWITCHED", 1500);
        }
      if (press(SDL_GAMEPAD_BUTTON_DPAD_DOWN) || press(SDL_GAMEPAD_BUTTON_DPAD_UP)) {
        int dir = press(SDL_GAMEPAD_BUTTON_DPAD_DOWN) ? 1 : -1;
        ChooseSymmetry(m, g_symmetry < 0 ? (dir > 0 ? 0 : 7) : (g_symmetry + dir + 8) % 8, true);
        g_y_used = true, chose();
        SetHint(std::string("PRE-SYMMETRY ") + kSymmetries[g_symmetry].name, 1500);
      }
    }

    // LB / RB: a tap = the previous / next palette; held = the effect / symmetry wheel (right stick picks,
    // release chooses). Not while Y holds the layer (there they switch stages).
    auto shoulder = [&](SDL_GamepadButton b, int* frames, Wheel wheel) {
      if (press(b) && !layer) *frames = 0;
      if (*frames < 0) return;
      if (down(b)) {
        ++*frames;
        if (g_wheel == Wheel::kNone && (*frames >= kHold || rx * rx + ry * ry > 0.5f * 0.5f)) {
          g_wheel = wheel, g_wheel_sel = -1;
          if (wheel == Wheel::kEffect) g_family = g_effect >= 0 ? EffectFamily(g_effect) : g_family;
        }
        return;
      }
      // Released.
      if (g_wheel == wheel) {
        if (g_wheel_sel >= 0) {
          if (wheel == Wheel::kEffect) {
            g_effect = FamilyStart(g_family) + g_wheel_sel;
            TapKey(m, EffectKey(g_effect).code);
            g_effect_text = KeyDescription(*m, ReadKml(*m), EffectKey(g_effect).code);
          } else {
            ChooseSymmetry(m, g_wheel_sel, g_pre);
          }
          ShowState(), chose();
        }
        g_wheel = Wheel::kNone;
      } else if (g_wheel == Wheel::kNone && *frames < kHold) {
        Step(m, &g_palette, kPalettes, 10, b == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER ? 1 : -1);
        ShowState(), chose();
      }
      *frames = -1;
    };
    shoulder(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, &g_lb_frames, Wheel::kEffect);
    shoulder(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, &g_rb_frames, Wheel::kSymmetry);
    if (g_wheel != Wheel::kNone) {
      g_wheel_x = rx, g_wheel_y = ry;
      if (g_wheel == Wheel::kEffect) {
        if (press(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) g_family = (g_family + 1) % 3, g_wheel_sel = -1;
        if (press(SDL_GAMEPAD_BUTTON_DPAD_LEFT)) g_family = (g_family + 2) % 3, g_wheel_sel = -1;
        Kml k = ReadKml(*m);
        g_row_text.clear();
        for (int i = 0; i < kFamilies[g_family].n; ++i) g_row_text.push_back(KeyDescription(*m, k, kFamilies[g_family].keys[i].code));
      }
      g_pre = g_wheel == Wheel::kSymmetry && down(SDL_GAMEPAD_BUTTON_NORTH);
      if (g_pre) g_y_used = true;  // (Y joined a symmetry wheel: no Control Panel on its release)
      int n = g_wheel == Wheel::kEffect ? kFamilies[g_family].n : 8;
      int s = WheelSegment(rx, ry, n);
      if (s >= 0) g_wheel_sel = s;
    }

    // Left stick: the light, relative, with optional flow (inertia: it eases to the stick's speed).
    Curve(axis(SDL_GAMEPAD_AXIS_LEFTX), axis(SDL_GAMEPAD_AXIS_LEFTY), &vx, &vy, g_tune.speed);
    if (g_tune.flow > 0) {
      float k = float(1.0 / (1.0 + g_tune.flow * 14.0));
      g_fvx += (vx - g_fvx) * k, g_fvy += (vy - g_fvy) * k;
      vx = g_fvx, vy = g_fvy;
    } else {
      g_fvx = vx, g_fvy = vy;
    }
    vx += gx, vy += gy;
    // A starfield running (patterns can't be drawn meanwhile - the manual): the right stick flies it - up /
    // down = faster / slower (past the middle: backwards), left / right = spin; RT = a boost. Let go and the
    // starfield's own speed and spin come back (eased); its KML may change them meanwhile (taken as the new own).
    const bool stars = Get16(*m, kStarfieldOn) != 0;
    if (stars && g_wheel == Wheel::kNone) {
      int16_t svz = int16_t(Get16(*m, kStarSvz));
      if (!g_flying) g_base_svz = svz, g_fly_svz = g_fly_spin = g_spin_acc = 0, g_flying = true;
      // What we wrote last frame, unless its KML changed it since: then that is its own value now.
      if (svz != int16_t(std::lround(g_base_svz + g_fly_svz))) g_base_svz = int16_t(svz - std::lround(g_fly_svz));
      const float dead = 0.12f;
      float sy = std::fabs(ry) > dead ? -ry : 0, sx = std::fabs(rx) > dead ? rx : 0;
      const float own = float(g_base_svz == 0 ? -60 : g_base_svz);  // (a still field: forward = towards you)
      float want_svz = own * (sy >= 0 ? 2.5f * sy : 2.0f * sy) + own * 2.0f * std::max(0.0f, rt_axis - 0.08f);
      float want_spin = 2.5f * sx * std::fabs(sx);  // angle units (256 a turn) a frame: ~half a turn a second
      g_fly_svz += (want_svz - g_fly_svz) * 0.15f, g_fly_spin += (want_spin - g_fly_spin) * 0.15f;
      Put16(*m, kStarSvz, uint16_t(int16_t(std::clamp<long>(std::lround(g_base_svz + g_fly_svz), -500, 500))));
      g_spin_acc += g_fly_spin;  // the spin turns the field's own angle on (its KML's turning kept)
      if (int turn = int(g_spin_acc)) {
        g_spin_acc -= float(turn);
        Put16(*m, kStarAngle, uint16_t((Get16(*m, kStarAngle) + turn) & 255));
      }
      if (sx != 0 || sy != 0) right_stick = true;  // (no spirograph: the stick is flying)
    } else if (g_flying) {
      g_flying = false;
    }
    // Right stick: the spirograph - the stick's position is the light source's position on the screen.
    if (!stars && g_wheel == Wheel::kNone && rx * rx + ry * ry > 0.08f * 0.08f) {
      int x = std::clamp(int(std::lround(160 + rx * 160 * g_tune.spiro)), 0, 319);
      int y = std::clamp(int(std::lround(100 + ry * 100 * g_tune.spiro)), 0, 199);
      Put16(*m, kPointerX, uint16_t(x));
      Put16(*m, kPointerY, uint16_t(y));
      g_px = g_py = 0;
      right_stick = true;
    }
    // RT: paint; its squeeze sets the density - a light touch starts a pattern every few frames, a full one every
    // frame (the Global Parameters' ON / OFF times, played live). R3 latches painting on.
    if (press(SDL_GAMEPAD_BUTTON_RIGHT_STICK)) {
      g_latch = !g_latch;
      SetHint(g_latch ? "PAINTING LATCHED ON (R3)" : "LATCH OFF", 1500);
    }
    if (g_latch) {
      buttons |= 2;
    } else if (rt_axis > 0.08f) {
      float p = std::min(1.0f, (rt_axis - 0.08f) / 0.84f);
      float rate = !g_tune.pressure || p >= 1.0f ? 1.0f : 0.12f + 0.88f * p * p;
      g_paint += rate;
      if (g_paint >= 1) g_paint -= 1, buttons |= 2;
    } else {
      g_paint = 0.99f;  // (the first touch paints at once)
    }
    // LT: the right mouse button (Global Parameters "mouse button 2": VECTOR = a dead-straight line at the
    // speed of the moment, CENTRE = back to the middle, KML #128 = that program).
    if (lt_axis > 0.3f) buttons |= 1;

    if (!layer && g_wheel == Wheel::kNone) {
      if (press(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) StepEffect(m, 1), ShowState(), chose();
      if (press(SDL_GAMEPAD_BUTTON_DPAD_LEFT)) StepEffect(m, -1), ShowState(), chose();
      if (press(SDL_GAMEPAD_BUTTON_START)) SetHint("LOADING THE OTHER DEMO (STARFIELDS AND LASERS / PATTERNS)", 4000);
      if (press(SDL_GAMEPAD_BUTTON_DPAD_DOWN)) {
        ChooseSymmetry(m, g_symmetry < 0 ? 0 : (g_symmetry + 1) % 8, false), ShowState(), chose();
      }
      if (press(SDL_GAMEPAD_BUTTON_DPAD_UP)) {
        ChooseSymmetry(m, g_symmetry < 0 ? 7 : (g_symmetry + 7) % 8, false), ShowState(), chose();
      }
      if (press(SDL_GAMEPAD_BUTTON_SOUTH)) {  // surprise me (an effect key that does something)
        Kml k = ReadKml(*m);
        for (int tries = 0; tries < 40; ++tries) {
          g_effect = int(g_rng() % kEffectCount);
          if (!(g_effect_text = KeyDescription(*m, k, EffectKey(g_effect).code)).empty()) break;
        }
        g_palette = int(g_rng() % 10);
        TapKey(m, EffectKey(g_effect).code), TapKey(m, kPalettes[g_palette].code);
        ChooseSymmetry(m, int(g_rng() % 8), false);
        ShowState(), chose();
      }
      if (press(SDL_GAMEPAD_BUTTON_WEST)) TapKey(m, kSpace), SetHint("PATTERNS STOPPED", 1500);
      if (press(SDL_GAMEPAD_BUTTON_START)) TapKey(m, kF10);
      if (press(SDL_GAMEPAD_BUTTON_EAST)) {  // the number row in turn (on Back until 2026-11-07)
        Step(m, &g_number, kNumbers, 12, 1);
        std::string d = KeyDescription(*m, ReadKml(*m), kNumbers[g_number].code);
        SetHint(std::string("NUMBER KEY ") + kNumbers[g_number].name + (d.empty() ? "" : ": " + d), 2000);
      }
    }
  } else {
    g_wheel = Wheel::kNone, g_lb_frames = g_rb_frames = g_y_frames = -1, g_latch = false;
    if (press(SDL_GAMEPAD_BUTTON_NORTH)) TapKey(m, kEsc);  // the Control Panel up / down
    if (press(SDL_GAMEPAD_BUTTON_EAST) && !g_click.active) Back(m, state);
    // A mouse: both sticks move the pointer (LB held: slowly, for small buttons); A / RT left button, X / LT
    // right button (the icons do different things with each).
    const double speed = g_tune.speed * (down(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) ? 0.3 : 1.0);
    float wx, wy;
    Curve(axis(SDL_GAMEPAD_AXIS_LEFTX), axis(SDL_GAMEPAD_AXIS_LEFTY), &vx, &vy, speed);
    Curve(rx, ry, &wx, &wy, speed);
    vx += wx + gx, vy += wy + gy;
    if (down(SDL_GAMEPAD_BUTTON_SOUTH) || rt) buttons |= 2;
    if (down(SDL_GAMEPAD_BUTTON_WEST) || lt) buttons |= 1;
    const bool bare_panel = state == 2 && !Get16(*m, kAlertUp);
    const bool dpad_up = press(SDL_GAMEPAD_BUTTON_DPAD_UP), dpad_down = press(SDL_GAMEPAD_BUTTON_DPAD_DOWN),
               dpad_left = press(SDL_GAMEPAD_BUTTON_DPAD_LEFT), dpad_right = press(SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
    if (bare_panel) {
      // The d-pad jumps between the icons (the colour presets too, unless a side panel covers them).
      const int columns = Get16(*m, kSidePanel) ? 6 : 9;
      if (vx * vx + vy * vy > 0.01f) g_cell = -1;  // (the stick took over)
      if (dpad_up || dpad_down || dpad_left || dpad_right) {
        if (g_cell < 0 || g_cell % 9 >= columns) {
          g_cell = NearestCell(Get16(*m, kPointerX), Get16(*m, kPointerY), columns);
        } else {
          int r = g_cell / 9, c = g_cell % 9;
          r = (r + (dpad_down ? 1 : dpad_up ? 2 : 0)) % 3;
          c = (c + (dpad_right ? 1 : dpad_left ? columns - 1 : 0)) % columns;
          g_cell = r * 9 + c;
        }
        Put16(*m, kPointerX, uint16_t(kGridX[g_cell % 9])), Put16(*m, kPointerY, uint16_t(kGridY[g_cell / 9]));
        g_px = g_py = 0;
        DescribeCell(g_cell);
      }
    } else if (state != 1 && state != 3) {
      // A tool screen: the d-pad nudges the pointer a pixel at a time (repeating while held).
      g_cell = -1;
      int nx = (down(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) ? 1 : 0) - (down(SDL_GAMEPAD_BUTTON_DPAD_LEFT) ? 1 : 0);
      int ny = (down(SDL_GAMEPAD_BUTTON_DPAD_DOWN) ? 1 : 0) - (down(SDL_GAMEPAD_BUTTON_DPAD_UP) ? 1 : 0);
      if (nx || ny) {
        bool step = g_dpad_frames == 0 || (g_dpad_frames >= 15 && g_dpad_frames % 3 == 0);
        ++g_dpad_frames;
        if (step) vx += float(nx), vy += float(ny);
      } else {
        g_dpad_frames = 0;
      }
    }
    if (g_click.active) {  // B's click: the pointer held on the exit button, the left button pressed
      // Still where it was clicked (an alert / side panel: its flag still up)? Try again a little later.
      bool still = g_click.state == -2   ? Get16(*m, kAlertUp) && state == 2
                   : g_click.state == -3 ? Get16(*m, kSidePanel) != 0
                                         : state == g_click.state;
      if (g_click.frame == 0 && g_click.tries > 0 && !still) g_click.active = false;
      if (g_click.active) {
        if (g_click.frame >= 0) {
          Put16(*m, kPointerX, uint16_t(g_click.x)), Put16(*m, kPointerY, uint16_t(g_click.y));
          vx = vy = 0, g_px = g_py = 0, mouse_dx = mouse_dy = 0;
          buttons = g_click.frame >= 2 && g_click.frame < 6 ? 2 : 0;
        }
        if (++g_click.frame > 8) {
          if (++g_click.tries >= 8) g_click.active = false;
          else g_click.frame = -22;  // (with the 8 above: every 30 frames)
        }
      }
    }
  }
  if (performing) g_click.active = false;
  g_px += vx, g_py += vy;
  int dx = int(g_px), dy = int(g_py);
  g_px -= float(dx), g_py -= float(dy);
  dx += mouse_dx, dy += mouse_dy;
  if (g_music_mode && !music::Active()) music::StartLoopback();  // (once: it remembers a failure)
  bool music_on = g_music_mode && music::Active() && performing;
  if (music_on) {
    music::Frame f = music::Analyse(1.0 / 50);
    if (!g_was_music) g_cx = float(Get16(*m, kPointerX)), g_cy = float(Get16(*m, kPointerY));
    Music(m, pad, f, &dx, &dy, &buttons, right_stick);
  }
  g_was_music = music_on;
  m->MouseInput(dx, dy, buttons);
}

void MusicKey() { g_music_key = true; }

int MusicMode() { return g_music_mode; }

const char* Hint(st::Machine* m, bool have_pad) {
  if (g_wheel != Wheel::kNone || (g_y_frames >= kHold && State(*m) == 0)) return nullptr;  // (the overlay)
  if (SDL_GetTicks() < g_hint_until) return g_hint.c_str();
  return nullptr;
}

// The wheels and the Y layer, drawn over the picture in the ST's 320-wide logical space.
void DrawOverlay(SDL_Renderer* r, float view_h) {
  auto box = [&](float x, float y, float w, float h, uint8_t cr, uint8_t cg, uint8_t cb, uint8_t ca) {
    SDL_FRect f{x, y, w, h};
    SDL_SetRenderDrawColor(r, cr, cg, cb, ca);
    SDL_RenderFillRect(r, &f);
  };
  auto text = [&](float x, float y, const std::string& s, bool dark = false) {
    SDL_SetRenderDrawColor(r, dark ? 0 : 255, dark ? 0 : 255, dark ? 0 : 255, 255);
    SDL_RenderDebugText(r, x, y, s.c_str());
  };
  auto centred = [&](float cx, float y, const std::string& s, bool dark = false) {
    text(cx - float(s.size()) * 4, y, s, dark);
  };
  SDL_BlendMode old;
  SDL_GetRenderDrawBlendMode(r, &old);
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
  const float cx = 160, cy = view_h / 2;
  if (g_wheel != Wheel::kNone) {
    const bool effect = g_wheel == Wheel::kEffect;
    const int n = effect ? kFamilies[g_family].n : 8;
    const float radius = effect ? 58 : 62;
    box(cx - 92, cy - 86, 184, 172, 0, 0, 0, 170);
    std::string title = effect ? std::string("< ") + kFamilies[g_family].name + " >"
                               : (g_pre ? "PRE-SYMMETRY" : "SYMMETRY  (Y: PRE)");
    centred(cx, cy - 82, title);
    // The stick's direction.
    if (g_wheel_x * g_wheel_x + g_wheel_y * g_wheel_y > 0.04f) {
      SDL_SetRenderDrawColor(r, 255, 220, 0, 255);
      SDL_RenderLine(r, cx, cy, cx + g_wheel_x * (radius - 14), cy + g_wheel_y * (radius - 14));
    }
    for (int i = 0; i < n; ++i) {
      double a = 2 * kPi * i / n;
      float x = cx + float(std::sin(a)) * radius, y = cy - float(std::cos(a)) * radius;
      const char* label = effect ? kFamilies[g_family].keys[i].name : kSymmetries[i].name;
      float w = float(std::strlen(label)) * 8;
      bool sel = i == g_wheel_sel;
      bool live = !effect || (i < int(g_row_text.size()) && !g_row_text[i].empty());
      box(x - w / 2 - 3, y - 5, w + 6, 10, sel ? 255 : live ? 40 : 20, sel ? 220 : live ? 40 : 20, sel ? 0 : live ? 60 : 20,
          230);
      if (live || sel) {
        centred(x, y - 4, label, sel);
      } else {
        SDL_SetRenderDrawColor(r, 90, 90, 90, 255);
        SDL_RenderDebugText(r, x - w / 2, y - 4, label);
      }
    }
    // What the chosen key does (the loaded KML), in the middle, in the half-size font (4 px a letter).
    if (effect && g_wheel_sel >= 0 && g_wheel_sel < int(g_row_text.size())) {
      std::string d = g_row_text[g_wheel_sel].empty() ? "(NOTHING ON THIS KEY)" : g_row_text[g_wheel_sel];
      SDL_SetRenderScale(r, 0.5f, 0.5f);
      size_t cut = d.size() > 22 ? d.rfind(' ', 22) : std::string::npos;
      if (cut == std::string::npos || cut == 0) {
        centred(2 * cx, 2 * cy - 4, d);
      } else {
        centred(2 * cx, 2 * cy - 10, d.substr(0, cut));
        centred(2 * cx, 2 * cy + 2, d.substr(cut + 1));
      }
      SDL_SetRenderScale(r, 1, 1);
    }
    centred(cx, cy + 74, effect ? "D-PAD: ROW   RELEASE: CHOOSE" : "RELEASE: CHOOSE");
  } else if (g_y_frames >= kHold) {
    // The plot routine, left to right as on the panel's bottom row.
    const float top = view_h - 58;
    box(4, top, 312, 54, 0, 0, 0, 190);
    centred(cx, top + 3, "PLOT ROUTINE (Y HELD)");
    for (int i = 0; i < 6; ++i) {
      float x = 8 + float(i) * 51.5f;
      bool on = g_fx_mask >= 0 && (g_fx_mask >> i & 1);
      box(x, top + 15, 48, 22, on ? 20 : 40, on ? 120 : 40, on ? 40 : 70, 230);  // on: green, as the panel's borders
      centred(x + 24, top + 17, kStageButtons[i]);
      SDL_SetRenderScale(r, 0.5f, 0.5f);  // (the names in a half-size font: 4 px a letter)
      centred(2 * (x + 24), 2 * (top + 28), std::string(kStages[i]) + (on ? " ON" : " OFF"));
      SDL_SetRenderScale(r, 1, 1);
    }
    centred(cx, top + 42, "D-PAD UP/DOWN: PRE-SYMMETRY");
  }
  SDL_SetRenderDrawBlendMode(r, old);
}

}  // namespace trip
