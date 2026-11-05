#include "settings_menu.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "log.h"
#include "settings.h"

namespace menu {
namespace {

enum class Kind { kToggle, kSlider, kChoice, kFullscreen, kResetCrt };

struct Item {
  const char* section;  // "general", "crt" or a game id
  const char* label;
  const char* key;
  Kind kind;
  double def = 0, lo = 0, hi = 1, step = 0.05;
  std::vector<const char*> choices = {};  // kChoice: the names of 0, 1, 2...
  const char* help = "";
  bool next_start = false;  // read when the game starts, not while it runs
};

struct Section {
  const char* id;
  const char* title;
};

const Section kSections[] = {
    {"general", "GENERAL"},       {"crt", "CRT LOOK"},       {"crtpp", "CRT++"},       {"llamatron", "LLAMATRON"},
    {"gridrunner", "SUPER GRIDRUNNER"}, {"photonstorm", "PHOTON STORM"}, {"andes", "ANDES ATTACK"},
    {"rmc", "MUTANT CAMELS"},
    {"defender2", "DEFENDER 2"},  {"hardcore", "HARDCORE"},  {"tripatron", "TRIP-A-TRON"},
};

// The defaults here must match the ones where each setting is read.
const std::vector<Item>& Items() {
  static const std::vector<Item> items = {
      {"general", "REMASTER", "remaster", Kind::kToggle, 1, 0, 1, 1, {},
       "Sprites drawn smooth by the PC at 4x, sliding between frames (F8). Off: the ST's own picture."},
      {"general", "RUN-AHEAD", "remaster_ahead", Kind::kChoice, 2, 0, 3, 1, {"OFF", "1 FRAME", "2 FRAMES", "3 FRAMES"},
       "Remaster only: runs the game ahead to cut the delay between the pad and the picture (F7). 2 is the sweet spot."},
      {"general", "VOLUME", "volume", Kind::kSlider, 1, 0, 1, 0.05, {},
       "Master volume for every game (0.5 is about half as loud)."},
      {"general", "EVEN OUT GAME VOLUMES", "volume_normalise", Kind::kToggle, 1, 0, 1, 1, {},
       "Every game played at the same loudness (the loud ones turned down). Off: each as loud as the original."},
      {"general", "CRT LOOK", "crt", Kind::kChoice, 1, 0, 2, 1, {"OFF", "CRT+", "CRT++"},
       "CRT+: scanlines, mask and glow, like a TV of the time. CRT++: psychedelic trails on top (F9). In a game: that game's own."},
      {"general", "FULLSCREEN", "", Kind::kFullscreen, 1, 0, 1, 1, {}, "Fullscreen or a window (F11, Alt+Enter)."},
      {"general", "EXCLUSIVE FULLSCREEN", "fullscreen_exclusive", Kind::kToggle, 0, 0, 1, 1, {},
       "Switches the display to a 50 / 100 Hz mode for even motion, at the cost of a blink when switching away.",
       true},

      {"crt", "SCANLINES", "crt_scanlines", Kind::kSlider, 0.45, 0, 1, 0.05, {}, "How dark the gaps between the lines are."},
      {"crt", "BEAM, DARK COLOURS", "crt_beam_min", Kind::kSlider, 0.35, 0, 1, 0.05, {}, "How thick the lines of dark colours are."},
      {"crt", "BEAM, BRIGHT COLOURS", "crt_beam_max", Kind::kSlider, 1.0, 0, 1, 0.05, {}, "How thick the lines of bright colours are."},
      {"crt", "SOFTNESS", "crt_blur", Kind::kSlider, 0.4, 0, 1, 0.05, {}, "Horizontal softness of the beam (0 = sharp pixels)."},
      {"crt", "PHOSPHOR MASK", "crt_mask", Kind::kSlider, 0.35, 0, 1, 0.05, {}, "The red, green and blue stripes of the tube."},
      {"crt", "MASK BLEED", "crt_mask_bleed", Kind::kSlider, 0.5, 0, 1, 0.05, {}, "How much the stripes blend together."},
      {"crt", "GLOW", "crt_halation", Kind::kSlider, 0.04, 0, 0.2, 0.01, {}, "The light bright things spill around them (halation)."},
      {"crt", "GLOW SIZE", "crt_halation_size", Kind::kSlider, 0.98, 0, 1, 0.02, {}, "A tight halo (0) or a long, soft tail (1)."},
      {"crt", "CURVATURE", "crt_curvature", Kind::kSlider, 0.02, 0, 0.15, 0.01, {}, "A curved tube (0 = flat)."},
      {"crt", "BRIGHTNESS", "crt_brightness", Kind::kSlider, 0.01, -0.3, 0.3, 0.01, {}, "Overall brightness."},
      {"crt", "CONTRAST", "crt_contrast", Kind::kSlider, 0.35, 0, 1, 0.05, {}, "Deeper blacks and brighter whites."},
      {"crt", "SATURATION", "crt_saturation", Kind::kSlider, 0.95, 0, 1.5, 0.05, {}, "How strong the colours are."},
      {"crt", "WARMTH", "crt_temperature", Kind::kSlider, 0.3, -1, 1, 0.1, {}, "Cool blue (-1) to warm (1)."},
      {"crt", "BLOOM", "crt_overflow", Kind::kSlider, 0.85, 0, 1, 0.05, {}, "Very bright colours spilling into white."},
      {"crt", "RESET THE CRT LOOK", "", Kind::kResetCrt, 0, 0, 1, 1, {}, "Press A: every CRT setting back to its default."},
      {"crtpp", "TRAILS", "crt_psy_trails", Kind::kSlider, 0.9, 0, 2, 0.05, {}, "CRT++: how strongly the trails show."},
      {"crtpp", "TRAIL LENGTH", "crt_psy_length", Kind::kSlider, 0.4, 0, 3, 0.05, {}, "Seconds for a trail to fade to half."},
      {"crtpp", "TRAIL ZOOM", "crt_psy_zoom", Kind::kSlider, 0.25, -2, 2, 0.05, {},
       "Trails stream out from the centre (in: below 0)."},
      {"crtpp", "TRAIL TWIST", "crt_psy_twist", Kind::kSlider, 0, -2, 2, 0.05, {}, "Trails swirl round the centre."},
      {"crtpp", "TRAIL COLOUR", "crt_psy_hue", Kind::kSlider, 0.66, 0, 1, 0.02, {},
       "The colour trails turn into: red 0, yellow .17, green .33, cyan .5, blue .66, magenta .83."},
      {"crtpp", "TRAIL TINT", "crt_psy_tint", Kind::kSlider, 0.3, 0, 1, 0.05, {}, "How quickly trails take that colour."},
      {"crtpp", "COLOUR CYCLE", "crt_psy_cycle", Kind::kSlider, 0, 0, 3, 0.05, {}, "Trails run through the rainbow as they age (turns a second)."},
      {"crtpp", "TRAIL THRESHOLD", "crt_psy_threshold", Kind::kSlider, 0.1, 0, 1, 0.05, {},
       "Only things brighter than this leave trails (higher: dark backgrounds don't)."},
      {"crtpp", "KEEP COLOURS", "crt_psy_keep", Kind::kSlider, 1, 0, 1, 0.05, {},
       "1: trails only where the picture is dark, things keep their own colour. 0: trails over everything."},
      {"crtpp", "TRAIL FEED", "crt_psy_gain", Kind::kSlider, 1, 0, 3, 0.1, {}, "How bright things are as they enter the trails."},
      {"crtpp", "GLOW BOOST", "crt_psy_glow", Kind::kSlider, 4, 1, 15, 0.5, {}, "The CRT glow, multiplied."},
      {"crtpp", "COLOUR BLEED", "crt_psy_bleed", Kind::kSlider, 0.5, 0, 3, 0.1, {}, "Colour smeared sideways, like a composite TV."},
      {"crtpp", "SATURATION BOOST", "crt_psy_saturation", Kind::kSlider, 1.3, 0.5, 2.5, 0.05, {}, "The CRT saturation, multiplied."},
      {"crtpp", "RESET CRT++", "", Kind::kResetCrt, 0, 0, 1, 1, {}, "Press A: every CRT++ setting back to its default."},

      {"llamatron", "ANALOG AIM", "analog_aim", Kind::kToggle, 0, 0, 1, 1, {},
       "Twin-stick shots in any direction (off: the original 8). Also RB on the title screen."},

      {"gridrunner", "SHIP SPEED", "grid_stick_speed", Kind::kSlider, 6.5, 2, 12, 0.5, {},
       "Pixels a frame with the stick pushed all the way."},
      {"gridrunner", "STICK CURVE", "grid_stick_curve", Kind::kSlider, 2, 1, 4, 0.25, {},
       "1 = straight; higher = finer control near the centre, the same top speed."},
      {"gridrunner", "STICK DEAD ZONE", "grid_stick_deadzone", Kind::kSlider, 0.15, 0, 0.5, 0.05, {},
       "How far the stick moves before the ship does."},
      {"gridrunner", "EASE IN", "grid_stick_smooth", Kind::kSlider, 2, 0, 6, 0.5, {},
       "Frames the ship takes to reach the stick's speed (0 = at once)."},
      {"gridrunner", "EASE OUT", "grid_stick_stop", Kind::kSlider, 1, 0, 6, 0.5, {},
       "Frames the ship takes to stop or turn back (0 = at once)."},
      {"gridrunner", "D-PAD SPEED", "grid_dpad_speed", Kind::kSlider, 1, 0.5, 4, 0.5, {}, "Pixels a frame on the d-pad."},
      {"gridrunner", "SWAP NOSECONE FLICK", "grid_fire_mode_swap", Kind::kToggle, 0, 0, 1, 1, {},
       "Which way a flick of the right stick turns the nosecone shot."},
      {"gridrunner", "50 FRAMES A SECOND", "grid_50fps", Kind::kToggle, 1, 0, 1, 1, {},
       "Draws every frame in play (the original: 25). The game's speed is the same."},
      {"gridrunner", "UNLOCK THE LEVEL YOU DIED ON", "grid_unlock_level_died", Kind::kToggle, 1, 0, 1, 1, {},
       "The level select starts where you died (the original: the level before, from level 6)."},

      {"photonstorm", "360 TWIN STICK", "ps_analog", Kind::kToggle, 1, 0, 1, 1, {},
       "Steer and aim in any direction, the right stick aims and fires (off: the original 16). Also Y on the title."},
      {"photonstorm", "50 FRAMES A SECOND", "photonstorm_50fps", Kind::kToggle, 1, 0, 1, 1, {},
       "Draws every frame in play (the original: 25). The game's speed is the same.", true},

      {"andes", "CONTROLS", "andes_controls", Kind::kChoice, 1, 0, 1, 1, {"STICK THRUST", "ARCADE"},
       "Arcade: left / right only turn round, RT thrust (squeeze = speed), X full thrust. Stick thrust: the push moves you."},
      {"andes", "ALTITUDE SPEED", "andes_stick_speed", Kind::kSlider, 3, 1, 8, 0.5, {},
       "Pixels a frame up or down with the stick pushed all the way."},
      {"andes", "ALTITUDE CURVE", "andes_stick_curve", Kind::kSlider, 1.5, 1, 3, 0.25, {},
       "1 = straight; higher = finer control near the centre."},
      {"andes", "DIRECT THRUST", "andes_direct_thrust", Kind::kToggle, 0, 0, 1, 1, {},
       "Stick thrust controls: speed follows how far the stick is pushed (off: the game's own thrust and glide).", true},
      {"andes", "DIRECT THRUST EASE", "andes_thrust_ease", Kind::kSlider, 0.5, 0.1, 3, 0.1, {},
       "With direct thrust: how quickly the speed follows the stick.", true},

      {"defender2", "CONTROLS", "d2_controls", Kind::kChoice, 1, 0, 1, 1, {"STICK THRUST", "ARCADE"},
       "Arcade: left / right only turn round, RT thrust (squeeze = speed), X full thrust. Stick thrust: the push moves you."},
      {"defender2", "60 HZ (ARCADE SPEED)", "defender2_60hz", Kind::kToggle, 1, 0, 1, 1, {},
       "The game's 60 Hz mode, as the arcade (its speeds adjusted). Off: 50 Hz.", true},
      {"defender2", "50 FRAMES A SECOND", "d2_50fps", Kind::kToggle, 1, 0, 1, 1, {},
       "Draws every frame in play (the original: 25). The game's speed is the same.", true},

      {"rmc", "TWIN STICK", "rmc_twin_stick", Kind::kChoice, 2, 0, 2, 1, {"OFF", "8 WAYS", "360"},
       "Right stick aims the spit (any angle with 360), left stick moves, jumps, sits. Off: the stick does both."},

      {"hardcore", "STROBES", "hc_strobes", Kind::kToggle, 0, 0, 1, 1, {},
       "The game's flashing strobes (its S key still switches them).", true},

      {"tripatron", "LIGHT SPEED", "trip_stick_speed", Kind::kSlider, 5, 1, 12, 0.5, {},
       "How fast the left stick moves the light source."},
      {"tripatron", "STICK CURVE", "trip_stick_curve", Kind::kSlider, 2, 1, 4, 0.25, {},
       "1 = straight; higher = finer control near the centre."},
      {"tripatron", "STICK DEAD ZONE", "trip_stick_deadzone", Kind::kSlider, 0.15, 0, 0.5, 0.05, {},
       "How far the stick moves before the light does."},
      {"tripatron", "SPIROGRAPH SIZE", "trip_spiro_size", Kind::kSlider, 0.9, 0.2, 1, 0.05, {},
       "How far the right stick reaches across the screen."},
      {"tripatron", "CALM FLASHES", "trip_calm_background", Kind::kToggle, 1, 0, 1, 1, {},
       "Background flashes and strobes softened into slow pulses."},
      {"tripatron", "REACT TO MUSIC", "trip_music", Kind::kChoice, 0, 0, 2, 1, {"OFF", "PULSE", "FULL"},
       "Follows whatever the PC is playing (L3 / F12). Full also changes palettes and effects on the beat."},
  };
  return items;
}

struct Row {
  int section = -1;  // a heading (index into kSections)
  int item = -1;     // or a setting
};

std::function<void()> g_apply;
SDL_Window* g_window = nullptr;
bool g_open = false;
std::string g_game, g_title;
std::vector<Row> g_rows;
int g_sel = 0;  // row
int g_top = 0;  // first row shown
// Held directions (pad d-pad / stick, keyboard handled by its own repeat): repeat timers.
int g_held_y = 0, g_held_x = 0;
double g_wait_y = 0, g_wait_x = 0;

// In a game, the look (crt) and the CRT++ sliders are the game's own: <id>_<key> once changed there, until then
// the shared key (what the Settings disk edits). Per key.
bool PerGame(const Item& it) {
  const std::string key = it.key;
  return !g_game.empty() && (key == "crt" || key.rfind("crt_psy_", 0) == 0);
}
std::string OwnKey(const Item& it) { return g_game + "_" + it.key; }

double Value(const Item& it) {
  if (it.kind == Kind::kFullscreen) return g_window && (SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN) ? 1 : 0;
  if (!*it.key) return 0;
  if (PerGame(it) && settings::Has(OwnKey(it))) return settings::Get(OwnKey(it), it.def);
  return settings::Get(it.key, it.def);
}

void SetValue(const Item& it, double v) {
  if (it.kind == Kind::kFullscreen) {
    if (g_window) SDL_SetWindowFullscreen(g_window, v != 0);
    return;
  }
  v = std::clamp(v, it.lo, it.hi);
  if (it.kind == Kind::kSlider) v = std::round(v / it.step) * it.step, v = std::round(v * 1000) / 1000;
  if (Value(it) == v && !(PerGame(it) && !settings::Has(OwnKey(it)))) return;
  const std::string key = PerGame(it) ? OwnKey(it) : std::string(it.key);
  settings::Set(key, v);
  settings::Save();
  LOG("settings: %s = %g", key.c_str(), v);
  if (g_apply) g_apply();
}

const Item* Selected() {
  if (g_sel < 0 || g_sel >= int(g_rows.size()) || g_rows[size_t(g_sel)].item < 0) return nullptr;
  return &Items()[size_t(g_rows[size_t(g_sel)].item)];
}

void Change(int dir) {
  const Item* it = Selected();
  if (!it) return;
  switch (it->kind) {
    case Kind::kToggle: case Kind::kFullscreen: SetValue(*it, Value(*it) != 0 ? 0 : 1); break;
    case Kind::kChoice: SetValue(*it, std::clamp(Value(*it) + dir, it->lo, it->hi)); break;
    case Kind::kSlider: SetValue(*it, Value(*it) + dir * it->step); break;
    case Kind::kResetCrt: break;
  }
}

void Press() {
  const Item* it = Selected();
  if (!it) return;
  if (it->kind == Kind::kResetCrt && !g_game.empty() && std::string(it->section) == "crtpp") {
    for (const Item& i : Items())  // the game's own CRT++ sliders go: the shared ones again
      if (PerGame(i) && std::string(i.key) != "crt") settings::Erase(OwnKey(i));
    settings::Save();
    LOG("settings: %s back to the shared CRT++ look", g_game.c_str());
    if (g_apply) g_apply();
    return;
  }
  if (it->kind == Kind::kResetCrt) {
    const bool pp = std::string(it->section) == "crtpp";
    settings::Set(pp ? "crt_psy_defaults" : "crt_defaults", 0);  // crt::LoadSettings puts its defaults back
    LOG("settings: the %s reset", pp ? "CRT++ look" : "CRT look");
    if (g_apply) g_apply();
    return;
  }
  if (it->kind == Kind::kChoice) {
    double v = Value(*it) + 1;
    SetValue(*it, v > it->hi ? it->lo : v);
    return;
  }
  Change(1);
}

void Default() {
  const Item* it = Selected();
  if (it && PerGame(*it)) {  // the game's own value goes: the shared one again
    settings::Erase(OwnKey(*it));
    settings::Save();
    if (g_apply) g_apply();
    return;
  }
  if (it && *it->key && it->kind != Kind::kFullscreen) SetValue(*it, it->def);
}

void Move(int dir) {
  for (int i = g_sel + dir; i >= 0 && i < int(g_rows.size()); i += dir)
    if (g_rows[size_t(i)].item >= 0) {
      g_sel = i;
      return;
    }
}

// The first setting of the next / previous section.
void JumpSection(int dir) {
  int i = g_sel;
  while (i >= 0 && g_rows[size_t(i)].section < 0) --i;  // this section's heading
  for (i += dir; i >= 0 && i < int(g_rows.size()); i += dir)
    if (g_rows[size_t(i)].section >= 0) break;
  if (i < 0 || i >= int(g_rows.size())) return;
  g_sel = i;
  g_top = i;  // its heading at the top (Render keeps it in range)
  Move(1);
}

}  // namespace

void SetApply(std::function<void()> apply) { g_apply = std::move(apply); }
void SetWindow(SDL_Window* window) { g_window = window; }

void Open(const std::string& game_id, const std::string& title) {
  g_game = game_id, g_title = title;
  g_rows.clear();
  auto add = [](int s) {
    g_rows.push_back({s, -1});
    for (size_t i = 0; i < Items().size(); ++i)
      if (std::string(kSections[s].id) == Items()[i].section) g_rows.push_back({-1, int(i)});
  };
  if (game_id.empty()) {  // the Settings disk: everything
    for (int s = 0; s < int(std::size(kSections)); ++s) add(s);
  } else {  // in a game: general, the game's own (short), then the CRT look
    const std::string own = game_id == "rmctt" ? "rmc" : game_id;  // both Camels builds: one section
    for (const char* id : {"general", own.c_str(), "crt", "crtpp"})
      for (int s = 0; s < int(std::size(kSections)); ++s)
        if (game_id != "general" && game_id != "crt" && game_id != "crtpp" && std::string(kSections[s].id) == id) add(s);
  }
  g_sel = g_top = 0;
  Move(1);
  g_held_x = g_held_y = 0;
  g_open = true;
  LOG("settings opened (%s)", game_id.empty() ? "all" : game_id.c_str());
}

void Close() {
  if (g_open) LOG("settings closed");
  g_open = false;
}

bool IsOpen() { return g_open; }

Result HandleEvent(const SDL_Event& e) {
  if (!g_open) return Result::kNone;
  if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
    switch (e.gbutton.button) {
      case SDL_GAMEPAD_BUTTON_SOUTH: Press(); break;
      case SDL_GAMEPAD_BUTTON_WEST: Default(); break;
      case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: JumpSection(-1); break;
      case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: JumpSection(1); break;
      case SDL_GAMEPAD_BUTTON_EAST: return Result::kQuit;
      case SDL_GAMEPAD_BUTTON_BACK: case SDL_GAMEPAD_BUTTON_START: return Result::kClose;
      default: break;  // the d-pad: Update (repeats while held)
    }
  } else if (e.type == SDL_EVENT_KEY_DOWN) {
    switch (e.key.scancode) {
      case SDL_SCANCODE_UP: Move(-1); break;
      case SDL_SCANCODE_DOWN: Move(1); break;
      case SDL_SCANCODE_LEFT: Change(-1); break;
      case SDL_SCANCODE_RIGHT: Change(1); break;
      case SDL_SCANCODE_PAGEUP: JumpSection(-1); break;
      case SDL_SCANCODE_PAGEDOWN: JumpSection(1); break;
      case SDL_SCANCODE_RETURN: case SDL_SCANCODE_SPACE: if (!e.key.repeat) Press(); break;
      case SDL_SCANCODE_DELETE: case SDL_SCANCODE_BACKSPACE: if (!e.key.repeat) Default(); break;
      case SDL_SCANCODE_ESCAPE: if (!e.key.repeat) return Result::kQuit; break;
      case SDL_SCANCODE_F1: if (!e.key.repeat) return Result::kClose; break;
      default: break;
    }
  } else if (e.type == SDL_EVENT_MOUSE_WHEEL && e.wheel.y != 0) {
    Move(e.wheel.y > 0 ? -1 : 1);
  }
  return Result::kNone;
}

void Update(SDL_Gamepad* pad, double dt) {
  if (!g_open) return;
  int x = 0, y = 0;
  if (pad) {
    float sx = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f;
    float sy = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f;
    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_UP) || sy < -0.5f) y = -1;
    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN) || sy > 0.5f) y = 1;
    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT) || sx < -0.5f) x = -1;
    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || sx > 0.5f) x = 1;
  }
  // A press acts at once; held, it repeats after a pause (faster for left / right: sliders).
  if (y != g_held_y) g_held_y = y, g_wait_y = 0.35, y ? Move(y) : void();
  else if (y && (g_wait_y -= dt) <= 0) g_wait_y = 0.08, Move(y);
  if (x != g_held_x) g_held_x = x, g_wait_x = 0.35, x ? Change(x) : void();
  else if (x && (g_wait_x -= dt) <= 0) g_wait_x = 0.05, Change(x);
}

void Render(SDL_Renderer* r) {
  if (!g_open || !r) return;
  int lw = 0, lh = 0;
  SDL_RendererLogicalPresentation mode = SDL_LOGICAL_PRESENTATION_DISABLED;
  SDL_GetRenderLogicalPresentation(r, &lw, &lh, &mode);
  SDL_SetRenderLogicalPresentation(r, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
  int iw = 0, ih = 0;
  SDL_GetRenderOutputSize(r, &iw, &ih);
  const float w = float(iw), h = float(ih);
  constexpr int kCols = 42, kLabelCols = 24, kBarCols = 10;
  float s = std::max(1.0f, std::floor(h / 300));
  while (s > 1 && (kCols + 4) * 8 * s > w) s -= 1;
  const float glyph = 8 * s, row_h = 12 * s, pad = 2 * glyph;
  const float pw = std::min(w, kCols * glyph + 2 * pad);
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(r, 0, 0, 0, 236);
  SDL_FRect panel{0, 0, pw, h};
  SDL_RenderFillRect(r, &panel);
  SDL_SetRenderDrawColor(r, 90, 90, 105, 255);
  SDL_RenderLine(r, pw - 1, 0, pw - 1, h);
  auto text = [&](const std::string& t, float x, float y, SDL_Color c, float scale = 1) {
    SDL_SetRenderScale(r, s * scale, s * scale);
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_RenderDebugText(r, x / (s * scale), y / (s * scale), t.c_str());
    SDL_SetRenderScale(r, 1, 1);
  };
  const SDL_Color kWhite{255, 255, 255, 255}, kDim{140, 140, 155, 255}, kLabel{205, 205, 215, 255},
      kAccent{255, 200, 90, 255}, kOn{120, 230, 140, 255};

  float y = pad;
  text("SETTINGS", pad, y, kWhite, 1.5f);
  if (!g_title.empty()) text(g_title, pad + 10 * glyph * 1.5f, y + 4 * s, kDim);
  y += 2.5f * row_h;

  // Help for the picked setting and the controls, at the bottom.
  std::vector<std::string> foot;
  if (const Item* it = Selected()) {
    std::string help = it->help;
    if (it->next_start) help += " Applies when the game next starts.";
    size_t at = 0;  // word wrap to the panel
    while (at < help.size()) {
      size_t n = std::min(help.size() - at, size_t(kCols));
      if (at + n < help.size()) {
        size_t sp = help.rfind(' ', at + n);
        if (sp != std::string::npos && sp > at) n = sp - at;
      }
      foot.push_back(help.substr(at, n));
      at += n;
      while (at < help.size() && help[at] == ' ') ++at;
    }
  }
  const char* keys1 = "UP/DOWN PICK   LEFT/RIGHT CHANGE   A TOGGLE";
  const char* keys2 = g_game.empty() ? "X DEFAULT   LB/RB SECTION   B BACK" : "X DEFAULT   LB/RB SECTION";
  const char* keys3 = g_game.empty() ? nullptr : "BACK RESUME   B QUIT TO THE DISKS";
  float foot_h = (float(foot.size()) + (keys3 ? 4.5f : 3.5f)) * row_h;
  float list_bottom = h - pad - foot_h;
  int visible = std::max(1, int((list_bottom - y) / row_h));
  if (g_sel < g_top + 1) g_top = std::max(0, g_sel - 1);  // keep the heading above the first row in view
  if (g_sel >= g_top + visible - 1) g_top = std::max(g_top, g_sel - visible + 2);  // a row of what's next
  g_top = std::clamp(g_top, 0, std::max(0, int(g_rows.size()) - visible));

  for (int i = g_top; i < int(g_rows.size()) && i < g_top + visible; ++i) {
    const Row& row = g_rows[size_t(i)];
    float ry = y + float(i - g_top) * row_h;
    if (row.section >= 0) {
      const bool own = !g_game.empty() && std::string(kSections[row.section].id) == "crtpp";
      const char* title = own ? "CRT++ FOR THIS GAME" : kSections[row.section].title;
      text(title, pad, ry + 2 * s, kAccent);
      SDL_SetRenderDrawColor(r, 255, 200, 90, 90);
      float lx = pad + (float(SDL_strlen(title)) + 1) * glyph;
      SDL_RenderLine(r, lx, ry + 6 * s, pw - pad, ry + 6 * s);
      continue;
    }
    const Item& it = Items()[size_t(row.item)];
    bool sel = i == g_sel;
    if (sel) {
      SDL_FRect bar{pad / 2, ry - 2 * s, pw - pad, row_h};
      SDL_SetRenderDrawColor(r, 255, 200, 90, 50);
      SDL_RenderFillRect(r, &bar);
    }
    const bool shared_reset = it.kind == Kind::kResetCrt && !g_game.empty() && std::string(it.section) == "crtpp";
    text(shared_reset ? "USE THE SHARED CRT++" : it.label, pad, ry, sel ? kWhite : kLabel);
    float vx = pad + kLabelCols * glyph;
    double v = Value(it);
    char buf[32];
    switch (it.kind) {
      case Kind::kToggle: case Kind::kFullscreen:
        text(v != 0 ? "ON" : "OFF", vx, ry, v != 0 ? kOn : kDim);
        break;
      case Kind::kChoice: {
        int k = std::clamp(int(std::lround(v)), 0, int(it.choices.size()) - 1);
        std::snprintf(buf, sizeof(buf), sel ? "< %s >" : "  %s", it.choices[size_t(k)]);
        text(buf, vx, ry, sel ? kWhite : kLabel);
        break;
      }
      case Kind::kSlider: {
        float bw = kBarCols * glyph, bh = 4 * s;
        SDL_FRect track{vx, ry + 2 * s, bw, bh};
        SDL_SetRenderDrawColor(r, 70, 70, 85, 255);
        SDL_RenderFillRect(r, &track);
        float f = float((v - it.lo) / (it.hi - it.lo));
        SDL_FRect fill{vx, ry + 2 * s, bw * std::clamp(f, 0.0f, 1.0f), bh};
        SDL_SetRenderDrawColor(r, sel ? 255 : 200, sel ? 200 : 170, sel ? 90 : 110, 255);
        SDL_RenderFillRect(r, &fill);
        if (it.def > it.lo && it.def < it.hi) {  // a tick at the default
          float dx = vx + bw * float((it.def - it.lo) / (it.hi - it.lo));
          SDL_SetRenderDrawColor(r, 255, 255, 255, 160);
          SDL_RenderLine(r, dx, ry, dx, ry + 7 * s);
        }
        std::snprintf(buf, sizeof(buf), "%.2f", v);
        std::string num = buf;  // trim trailing zeros: 6.50 -> 6.5, 2.00 -> 2
        if (num.find('.') != std::string::npos) {
          while (num.back() == '0') num.pop_back();
          if (num.back() == '.') num.pop_back();
        }
        text(num, vx + bw + glyph, ry, sel ? kWhite : kLabel);
        break;
      }
      case Kind::kResetCrt:
        break;
    }
    if (it.next_start) text("*", pw - pad - glyph, ry, kDim);
  }
  // Scroll marks.
  if (g_top > 0) text("^", pw - pad, y - row_h, kDim);
  if (g_top + visible < int(g_rows.size())) text("v", pw - pad, list_bottom, kDim);

  float fy = h - pad - foot_h + row_h;
  for (const std::string& l : foot) text(l, pad, fy, kLabel), fy += row_h;
  fy += row_h / 2;
  text(keys1, pad, fy, kDim), fy += row_h;
  text(keys2, pad, fy, kDim), fy += row_h;
  if (keys3) text(keys3, pad, fy, kAccent);
  SDL_SetRenderLogicalPresentation(r, lw, lh, mode);
}

}  // namespace menu
