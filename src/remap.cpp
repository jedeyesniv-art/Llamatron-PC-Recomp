#include "remap.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "log.h"
#include "settings.h"

namespace remap {

uint32_t Function::DefaultMask() const {
  uint32_t m = 0;
  for (int d : defaults) m |= 1u << d;
  return m;
}

namespace {

constexpr int A = SDL_GAMEPAD_BUTTON_SOUTH, B = SDL_GAMEPAD_BUTTON_EAST, X = SDL_GAMEPAD_BUTTON_WEST,
              Y = SDL_GAMEPAD_BUTTON_NORTH, LB = SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,
              RB = SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, UP = SDL_GAMEPAD_BUTTON_DPAD_UP,
              DOWN = SDL_GAMEPAD_BUTTON_DPAD_DOWN, LEFT = SDL_GAMEPAD_BUTTON_DPAD_LEFT,
              RIGHT = SDL_GAMEPAD_BUTTON_DPAD_RIGHT, LT = kLt, RT = kRt;

// ---- The games' functions (from their how-to-play pages and their control code: input.cpp, gridrunner.cpp,
// stgames.cpp). A button is in at most one function of a list, listed under what it does in play (Gridrunner's
// d-pad up / down: the nosecone shot - owner 2026-11-07 moved them there when they showed as UP / DOWN). ----

const std::vector<Function> kLlamatron = {
    {"fire", "FIRE", {A}, "Fire; held, it locks the angle. At the title: starts a twin-stick game."},
    {"fire1", "ONE-STICK FIRE", {X}, "Fire on the left stick's joystick. At the title: starts a one-stick game."},
    {"fire2", "AIM STICK FIRE", {RT}, "Fire on the aiming joystick (the right stick's)."},
    {"aim", "ANALOG AIM / FIRE", {RB}, "At the title: analog aim on / off. In play: fire on the aiming joystick."},
    {"tentacle", "TENTACLE (PAUSED)", {Y}, "On the pause screen: the Ozric tentacle on / off."},
    {"quit", "QUIT (PAUSED)", {B}, "On the pause screen: quit to the title."},
    {"up", "UP", {UP}, "Moves, as the left stick."},
    {"down", "DOWN", {DOWN}, "Moves, as the left stick."},
    {"left", "LEFT", {LEFT}, "Moves, as the left stick."},
    {"right", "RIGHT", {RIGHT}, "Moves, as the left stick."},
};

const std::vector<Function> kGridrunner = {
    {"fire", "FIRE / CLICK", {A, RT}, "Fire (held). On the menus A picks the choice, RT clicks at the pointer."},
    {"nosecone", "NOSECONE BACK (HOLD)", {LT, X}, "The nosecone back to the ship while held. X at the title: game options."},
    {"shotup", "NOSECONE SHOT UP", {RB, UP}, "Turns the nosecone shot one step up a press. D-pad up: up on the menus too."},
    {"shotdown", "NOSECONE SHOT DOWN", {LB, DOWN},
     "Turns the nosecone shot one step down a press. LB at the title: more info. D-pad down: down on the menus too."},
    {"martyr", "MARTYRDOM", {Y}, "Voluntary martyrdom: lose a life, blow up what's vulnerable. Y at the title: secrets."},
    {"quit", "QUIT / DELETE", {B}, "Pause screen: quit to the title. Name entry: delete. Options: the quit row."},
    {"left", "LEFT", {LEFT}, "Moves the ship slowly. On the menus: left."},
    {"right", "RIGHT", {RIGHT}, "Moves the ship slowly. On the menus: right."},
};

const std::vector<Function> kAndesArcade = {
    {"fire", "FIRE", {A, LT}, "Fire (held). A types the letter of a high score name."},
    {"thrust", "THRUST (SQUEEZE)", {RT}, "Squeeze = speed. A button here: full thrust."},
    {"bomb", "SMART BOMB", {B}, "A smart bomb."},
    {"reverse", "REVERSE", {X, Y}, "Turn round."},
    {"shield", "SHIELD (HOLD)", {LB, RB}, "The shield while held (it drains)."},
    {"up", "UP", {UP}, "Climb (high score name: the letter)."},
    {"down", "DOWN", {DOWN}, "Dive (high score name: the letter)."},
    {"left", "LEFT", {LEFT}, "Face left."},
    {"right", "RIGHT", {RIGHT}, "Face right."},
};

const std::vector<Function> kAndesStick = {
    {"fire", "FIRE", {A, RT}, "Fire (held). A types the letter of a high score name."},
    {"thrust", "THRUST", {LT}, "Thrust the way you face."},
    {"bomb", "SMART BOMB", {Y}, "A smart bomb."},
    {"reverse", "REVERSE", {X}, "Turn round."},
    {"shield", "SHIELD (HOLD)", {LB, RB}, "The shield while held (it drains)."},
    {"up", "UP", {UP}, "Climb (high score name: the letter)."},
    {"down", "DOWN", {DOWN}, "Dive (high score name: the letter)."},
    {"left", "LEFT", {LEFT}, "Thrust left."},
    {"right", "RIGHT", {RIGHT}, "Thrust right."},
};

const std::vector<Function> kD2Arcade = {
    {"fire", "FIRE", {A, LT}, "Fire (held). On the menus A picks, LT is the right mouse button."},
    {"thrust", "THRUST (SQUEEZE)", {RT}, "Squeeze = speed. A button here: full thrust."},
    {"bomb", "SMART BOMB", {B}, "A smart bomb."},
    {"reverse", "REVERSE", {X, Y}, "Turn round. High score initials: X deletes a letter."},
    {"inviso", "INVISO / ENERGY", {LB}, "Inviso in Stargate (held), the energy weapon in Defender II."},
    {"ship", "SHIP (TAB)", {RB}, "Defender II's Tab key: the ship."},
    {"up", "UP", {UP}, "Climb. On the menus: up."},
    {"down", "DOWN", {DOWN}, "Dive. On the menus: down."},
    {"left", "LEFT", {LEFT}, "Face left. On the menus: left."},
    {"right", "RIGHT", {RIGHT}, "Face right. On the menus: right."},
};

const std::vector<Function> kD2Stick = {
    {"fire", "FIRE", {A, RT}, "Fire (held). On the menus A picks."},
    {"thrust", "THRUST", {LT}, "Thrust the way you face."},
    {"bomb", "SMART BOMB", {Y}, "A smart bomb."},
    {"reverse", "REVERSE", {X}, "Turn round. High score initials: X deletes a letter."},
    {"inviso", "INVISO / ENERGY", {LB}, "Inviso in Stargate (held), the energy weapon in Defender II."},
    {"ship", "SHIP (TAB)", {RB}, "Defender II's Tab key: the ship."},
    {"up", "UP", {UP}, "Climb. On the menus: up."},
    {"down", "DOWN", {DOWN}, "Dive. On the menus: down."},
    {"left", "LEFT", {LEFT}, "Thrust left. On the menus: left."},
    {"right", "RIGHT", {RIGHT}, "Thrust right. On the menus: right."},
};

const std::vector<Function> kRmc = {
    {"fire", "SPIT (HOLD)", {A, X, RT}, "Fire held: no jumping or sitting (aim with the left stick alone)."},
    {"menu", "MENU (PAUSED)", {B}, "On the pause screen: back to the menu."},
    {"up", "UP", {UP}, "Jump."},
    {"down", "DOWN", {DOWN}, "Sit."},
    {"left", "LEFT", {LEFT}, "Walk left."},
    {"right", "RIGHT", {RIGHT}, "Walk right."},
};

const std::vector<Function> kHardcore = {
    {"lasers", "SIDE LASERS", {A, RT}, "The side lasers on all four tracks. On the title: start."},
    {"puck", "PUCK INTELLIGENCE", {Y}, "The puck's intelligence on / off."},
    {"up", "UP", {UP}, "Moves the guns slowly."},
    {"down", "DOWN", {DOWN}, "Moves the guns slowly."},
    {"left", "LEFT", {LEFT}, "Moves the guns slowly."},
    {"right", "RIGHT", {RIGHT}, "Moves the guns slowly."},
};

const std::vector<Function> kPhotonStorm = {
    {"fire", "FIRE", {A}, "Fire where the ship points. On the menus: the left mouse button (title: 1 player)."},
    {"thrust", "THRUST", {X}, "Fly. On the menus: the right mouse button (title: 2 players)."},
    {"bomb", "SMART BOMB", {RT}, "A smart bomb. On the menus: the left mouse button."},
    {"boost", "BOOST (HOLD)", {LT}, "Faster while its gauge lasts. On the menus: the right mouse button."},
    {"single", "SINGLE SHOTS (HOLD)", {LB, RB}, "One shot at a time, not the spread."},
    {"mode", "360 / ORIGINAL (TITLE)", {Y}, "At the title: 360 twin stick or the original 16 directions."},
    {"quit", "QUIT (PAUSED)", {B}, "On the pause screen: quit to the title."},
    {"up", "UP", {UP}, "Points the ship, as the left stick."},
    {"down", "DOWN", {DOWN}, "Points the ship, as the left stick."},
    {"left", "LEFT", {LEFT}, "Points the ship, as the left stick."},
    {"right", "RIGHT", {RIGHT}, "Points the ship, as the left stick."},
};

const std::vector<Function> kGeneric = {
    {"fire", "JOYSTICK FIRE", {A, X}, "The joystick's fire button."},
    {"esc", "ESC", {B}, "The Esc key."},
    {"mleft", "MOUSE LEFT BUTTON", {RT}, "The mouse's left button."},
    {"mright", "MOUSE RIGHT BUTTON", {LT}, "The mouse's right button."},
    {"up", "UP", {UP}, "The joystick."},
    {"down", "DOWN", {DOWN}, "The joystick."},
    {"left", "LEFT", {LEFT}, "The joystick."},
    {"right", "RIGHT", {RIGHT}, "The joystick."},
};

const std::vector<Function> kNone;

std::string g_game;
input::Mode g_mode = input::Mode::kGeneric;
const std::vector<Function>* g_fns = &kNone;
std::string g_prefix;          // settings keys: <prefix>_controls_<function>
std::vector<uint32_t> g_sets;  // per function: the physical inputs attached
int g_logical[32];             // physical input -> logical (-1: nothing)
bool g_active = false;
bool g_test_through = false;
uint32_t g_event_held = 0;  // the logical buttons the made events have said are down

constexpr uint32_t kFixed = 1u << SDL_GAMEPAD_BUTTON_START | 1u << SDL_GAMEPAD_BUTTON_BACK | 1u << SDL_GAMEPAD_BUTTON_GUIDE;

std::string Sanitised(const std::string& id) {
  std::string k;
  for (char c : id) k += std::isalnum(uint8_t(c)) ? char(std::tolower(uint8_t(c))) : '_';
  return k;
}

// The list and settings prefix for a game (the control scheme picks Andes / Defender 2's).
const std::vector<Function>& ListFor(const std::string& id, input::Mode mode, std::string* prefix) {
  std::string own = id == "rmctt" ? "rmc" : Sanitised(id);  // both Camels builds: one set
  *prefix = own;
  if (id.empty() || mode == input::Mode::kTripATron || mode == input::Mode::kDocs) return kNone;
  if (mode == input::Mode::kGeneric) {
    *prefix = "disk_" + own;
    return kGeneric;
  }
  if (id == "llamatron") return kLlamatron;
  if (id == "gridrunner") return kGridrunner;
  if (id == "rmc" || id == "rmctt") return kRmc;
  if (id == "hardcore") return kHardcore;
  if (id == "photonstorm") return kPhotonStorm;
  if (id == "andes") {
    bool arcade = settings::Get("andes_controls", 1) != 0;
    *prefix = arcade ? "andes_arcade" : "andes_stick";
    return arcade ? kAndesArcade : kAndesStick;
  }
  if (id == "defender2") {
    bool arcade = settings::Get("d2_controls", 1) != 0;
    *prefix = arcade ? "defender2_arcade" : "defender2_stick";
    return arcade ? kD2Arcade : kD2Stick;
  }
  return kNone;
}

std::string Key(const std::string& prefix, const Function& f) { return prefix + "_controls_" + f.key; }

void Build() {
  g_sets.clear();
  for (const Function& f : *g_fns) {
    std::string k = Key(g_prefix, f);
    g_sets.push_back(settings::Has(k) ? uint32_t(settings::Get(k, 0)) & ~kFixed : f.DefaultMask());
  }
  uint32_t defaults = 0;  // a default of some function
  for (const Function& f : *g_fns) defaults |= f.DefaultMask();
  g_active = false;
  for (int p = 0; p < 32; ++p) {
    int to = defaults >> p & 1 ? -1 : p;  // listed somewhere but attached nowhere: nothing; never listed: itself
    for (size_t i = 0; i < g_fns->size(); ++i)
      if (g_sets[i] >> p & 1) {
        const Function& f = (*g_fns)[i];
        to = f.DefaultMask() >> p & 1 ? p : f.defaults[0];
      }
    if (kFixed >> p & 1) to = p;
    g_logical[p] = to;
    if (to != p) g_active = true;
  }
}

void Save() {
  for (size_t i = 0; i < g_fns->size(); ++i) settings::Set(Key(g_prefix, (*g_fns)[i]), double(g_sets[i]));
  settings::Save();
  Build();
}

bool IsTrigger(int i) { return i == kLt || i == kRt; }

}  // namespace

void SetGame(const std::string& id, input::Mode mode) {
  g_game = id, g_mode = mode;
  g_event_held = 0;
  Reload();
}

const std::string& Game() { return g_game; }

void Reload() {
  g_fns = &ListFor(g_game, g_mode, &g_prefix);
  Build();
  if (g_active) {
    std::string map;
    for (int p = 0; p < 32; ++p)
      if (g_logical[p] != p && Attachable(p))
        map += " " + InputName(p) + "->" + (g_logical[p] < 0 ? std::string("-") : InputName(g_logical[p]));
    LOG("controls (%s):%s", g_prefix.c_str(), map.c_str());
  }
}

const std::vector<Function>& Functions() { return *g_fns; }
uint32_t Attached(size_t fn) { return fn < g_sets.size() ? g_sets[fn] : 0; }

void Toggle(size_t fn, int input) {
  if (fn >= g_sets.size() || !Attachable(input)) return;
  uint32_t bit = 1u << input;
  if (g_sets[fn] & bit) {
    g_sets[fn] &= ~bit;
  } else {
    for (uint32_t& s : g_sets) s &= ~bit;
    g_sets[fn] |= bit;
  }
  LOG("controls: %s %s = %s", g_prefix.c_str(), (*g_fns)[fn].key, MaskNames(g_sets[fn]).c_str());
  Save();
}

void ResetFunction(size_t fn) {
  if (fn >= g_sets.size()) return;
  uint32_t d = (*g_fns)[fn].DefaultMask();
  for (uint32_t& s : g_sets) s &= ~d;
  g_sets[fn] = d;
  Save();
}

void ResetAll() {
  for (const Function& f : *g_fns) settings::Erase(Key(g_prefix, f));
  settings::Save();
  LOG("controls: %s back to the defaults", g_prefix.c_str());
  Build();
}

bool Changed() {
  for (size_t i = 0; i < g_sets.size(); ++i)
    if (g_sets[i] != (*g_fns)[i].DefaultMask()) return true;
  return false;
}

bool Changed(const std::string& id, input::Mode mode) {
  std::string prefix;
  for (const Function& f : ListFor(id, mode, &prefix)) {  // (Andes / Defender 2: the scheme selected)
    std::string k = Key(prefix, f);
    if (settings::Has(k) && uint32_t(settings::Get(k, 0)) != f.DefaultMask()) return true;
  }
  return false;
}

bool Attachable(int input) {
  if (input == kLt || input == kRt) return true;
  if (input < 0 || input >= SDL_GAMEPAD_BUTTON_COUNT || input >= kLt) return false;
  return !(kFixed >> input & 1);
}

std::string InputName(int input) {
  switch (input) {
    case A: return "A";
    case B: return "B";
    case X: return "X";
    case Y: return "Y";
    case LB: return "LB";
    case RB: return "RB";
    case kLt: return "LT";
    case kRt: return "RT";
    case SDL_GAMEPAD_BUTTON_LEFT_STICK: return "L3";
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return "R3";
    case UP: return "D-UP";
    case DOWN: return "D-DOWN";
    case LEFT: return "D-LEFT";
    case RIGHT: return "D-RIGHT";
    case SDL_GAMEPAD_BUTTON_START: return "START";
    case SDL_GAMEPAD_BUTTON_BACK: return "BACK";
    case SDL_GAMEPAD_BUTTON_GUIDE: return "GUIDE";
    case SDL_GAMEPAD_BUTTON_MISC1: return "MISC";
    case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1: return "P1";
    case SDL_GAMEPAD_BUTTON_LEFT_PADDLE1: return "P2";
    case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2: return "P3";
    case SDL_GAMEPAD_BUTTON_LEFT_PADDLE2: return "P4";
    case SDL_GAMEPAD_BUTTON_TOUCHPAD: return "PAD";
    default: return "B" + std::to_string(input);
  }
}

std::string MaskNames(uint32_t mask) {
  // The pad's order: face buttons, shoulders, triggers, d-pad, the rest.
  static const int kOrder[] = {A, B, X, Y, LB, RB, kLt, kRt, UP, DOWN, LEFT, RIGHT,
                               SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK};
  std::string s;
  uint32_t left = mask;
  for (int i : kOrder)
    if (left >> i & 1) s += (s.empty() ? "" : " ") + InputName(i), left &= ~(1u << i);
  for (int i = 0; i < 32; ++i)
    if (left >> i & 1) s += (s.empty() ? "" : " ") + InputName(i);
  return s;
}

void Apply(uint32_t* buttons, float* lt, float* rt) {
  if (!g_active) return;
  uint32_t in = *buttons, out = 0;
  float ilt = *lt, irt = *rt, olt = 0, ort = 0;
  for (int p = 0; p < 32; ++p) {
    int to = g_logical[p];
    if (to < 0) continue;
    float v = p == kLt ? ilt : p == kRt ? irt : float(in >> p & 1);
    if (to == kLt) olt = std::max(olt, v);
    else if (to == kRt) ort = std::max(ort, v);
    else if (IsTrigger(p) ? v > 0.5f : v != 0) out |= 1u << to;
  }
  *buttons = out, *lt = olt, *rt = ort;
}

bool Active() { return g_active; }

bool IsFirstPad(SDL_Gamepad* p) { return p && p == input::FirstPad(); }

namespace {
// Pad 1's logical state now.
void Logical(SDL_Gamepad* p, uint32_t* buttons, float* lt, float* rt) {
  uint32_t b = 0;
  for (int i = 0; i < SDL_GAMEPAD_BUTTON_COUNT && i < kLt; ++i)
    if (SDL_GetGamepadButton(p, SDL_GamepadButton(i))) b |= 1u << i;
  *buttons = b;
  *lt = SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) / 32767.0f;
  *rt = SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) / 32767.0f;
  Apply(buttons, lt, rt);
}
}  // namespace

bool Button(SDL_Gamepad* p, SDL_GamepadButton b) {
  if (!g_active || !IsFirstPad(p)) return SDL_GetGamepadButton(p, b);
  uint32_t buttons;
  float lt, rt;
  Logical(p, &buttons, &lt, &rt);
  return buttons >> b & 1;
}

float Axis(SDL_Gamepad* p, SDL_GamepadAxis a) {
  if (!g_active || !IsFirstPad(p) ||
      (a != SDL_GAMEPAD_AXIS_LEFT_TRIGGER && a != SDL_GAMEPAD_AXIS_RIGHT_TRIGGER))
    return SDL_GetGamepadAxis(p, a) / 32767.0f;
  uint32_t buttons;
  float lt, rt;
  Logical(p, &buttons, &lt, &rt);
  return a == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? lt : rt;
}

bool DropEvent(const SDL_Event& e) {
  if (!g_active || (e.type != SDL_EVENT_GAMEPAD_BUTTON_DOWN && e.type != SDL_EVENT_GAMEPAD_BUTTON_UP)) return false;
  SDL_Gamepad* first = input::FirstPad();
  if (!first || e.gbutton.which != SDL_GetGamepadID(first)) return false;
  return !(kFixed >> e.gbutton.button & 1);
}

std::vector<SDL_Event> Events(SDL_Gamepad* first) {
  std::vector<SDL_Event> out;
  if (!g_active || !first) {
    g_event_held = 0;
    return out;
  }
  uint32_t now;
  float lt, rt;
  Logical(first, &now, &lt, &rt);
  now &= ~kFixed;  // (their real events go through)
  uint32_t changed = now ^ g_event_held;
  for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT && b < kLt; ++b) {
    if (!(changed >> b & 1)) continue;
    SDL_Event e{};
    e.type = now >> b & 1 ? SDL_EVENT_GAMEPAD_BUTTON_DOWN : SDL_EVENT_GAMEPAD_BUTTON_UP;
    e.gbutton.which = SDL_GetGamepadID(first);
    e.gbutton.button = uint8_t(b);
    e.gbutton.down = e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    e.gbutton.timestamp = SDL_GetTicksNS();
    out.push_back(e);
  }
  g_event_held = now;
  return out;
}

void SyncEvents(SDL_Gamepad* first) {
  g_event_held = 0;
  if (!g_active || !first) return;
  float lt, rt;
  Logical(first, &g_event_held, &lt, &rt);
  g_event_held &= ~kFixed;
}

void SetTestThrough(bool on) { g_test_through = on; }
bool TestThrough() { return g_test_through; }

bool TranslateTest(SDL_Event* e) {
  if (!g_test_through || !g_active || e->gbutton.button >= 32) return true;
  int to = g_logical[e->gbutton.button];
  if (to < 0 || IsTrigger(to)) return false;
  e->gbutton.button = uint8_t(to);
  return true;
}

}  // namespace remap
