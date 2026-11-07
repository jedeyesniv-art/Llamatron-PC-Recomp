// Rumble (rumble.h). Everything is read from each game's RAM (or, for Photon Storm's kills, the score digits on its
// screen) after every frame; found by RAM searches of scripted runs (--dump-every, 2026-11-07):
// - lives (a long, heavy pulse when one is lost): Llamatron $5C0CF, Andes $12BB6, Photon Storm $1D124, Camels
//   $19264 (STE; `subq.w #1` at $19268) / $1929C (TT), Hardcore $14839; Gridrunner's state turning from play (1) to
//   the ship-lost screen (4); Defender 2's death countdown $2F2DE starting from 0;
// - smart bombs (short, sharp): Andes $12BB4, Photon Storm $1D126, Defender 2 $2BA44;
// - kills (a short knock): the score going up - Andes's digits at $12B9A, Defender 2's at $4B09A (a byte a digit),
//   Photon Storm's score digits on screen (lines 18..37, x 0..63 of the screen shown; its RAM score wasn't found);
//   Llamatron: the kill flash being started (the colour list *$25EA set to $25E6, text offsets - the flash itself is
//   patched out, game.cpp), stronger;
// - thrust (a continuous low hum rising with speed): Andes's speed index $14BC4 (0..19), Defender 2's velocity
//   $1B4B4 (16.16, top 9 px a frame); Photon Storm's boost (the play loop's Kbshift copy $27C50 bit 1, gauge $1D12C);
// - Hardcore's puck ($1B714 / $1B718, 16.16; the lead ring of its trail): a hum in the motor on its side, stronger
//   as it speeds up;
// - Camels' power-ups: a pickup starts a palette flash ($3D5D0 = $3D636 / $3D736, level $3D5D4 jumping to 7; TT
//   $3D960 = $3D9C6 / $3DAC6, $3D964): a tiny blip.
#include "rumble.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "log.h"
#include "settings.h"

namespace rumble {
namespace {

enum class Kind { kDeath, kBomb, kKill, kBigKill, kBlip };
enum class When { kDropsByOne, kStartsFromZero, kPlayToLost };
struct Watch {
  uint32_t addr;
  int size;  // 1 or 2 bytes
  When when;
  Kind kind;
};

std::string g_id;
std::vector<Watch> g_watches;
std::vector<int> g_prev;
int g_frames = 0;
int g_touched = -100000;  // the frame the pad was last used (attract demos die too: only rumble for a player)
std::vector<uint8_t> g_digits;  // the score digits last frame (kills)
uint32_t g_hud[3] = {1, 2, 3};  // Photon Storm: the score area's last pictures (screens alternate)
uint32_t g_flash_prev = 0;      // Llamatron: the colour list pointer last frame
int g_level_prev = 0;           // Camels: the pickup flash's level last frame
double g_puck_x = -1, g_puck_y = -1;
int g_kills = 0;  // (counted for the log)
int g_death_at = -100000;  // the frame of the last life lost (Photon Storm redraws its score panel then)

struct Pulse {
  float low, high;
  int frames;
};
std::vector<Pulse> g_pulses;
uint16_t g_sent_low = 0, g_sent_high = 0;
int g_sent_at = -1000;

bool PadUsed(SDL_Gamepad* pad) {
  if (!pad) return false;
  for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b)
    if (SDL_GetGamepadButton(pad, SDL_GamepadButton(b))) return true;
  for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; ++a)
    if (std::abs(SDL_GetGamepadAxis(pad, SDL_GamepadAxis(a))) > 10000) return true;
  return false;
}

int Read(const st::Machine& m, uint32_t addr, int size) {
  const uint8_t* p = m.Ram() + addr;
  return size == 2 ? p[0] << 8 | p[1] : p[0];
}
uint32_t Read32(const st::Machine& m, uint32_t a) {
  const uint8_t* p = m.Ram() + (a & 0xFFFFFF);
  return uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3];
}

void Event(Kind kind, bool idle) {
  static const char* kNames[] = {"life lost", "smart bomb", "kill", "kill (strong)", "pickup"};
  if (kind == Kind::kKill || kind == Kind::kBigKill) {
    if (++g_kills % 10 == 1) LOG("rumble: kills so far %d (frame %d)%s", g_kills, g_frames, idle ? " (idle)" : "");
  } else {
    LOG("rumble: %s at frame %d%s", kNames[int(kind)], g_frames, idle ? " (idle)" : "");
  }
  if (kind == Kind::kDeath) g_death_at = g_frames;
  if (idle) return;
  switch (kind) {
    case Kind::kDeath: g_pulses.push_back({0.82f, 0.45f, 28}); break;
    case Kind::kBomb: g_pulses.push_back({0.55f, 0.70f, 13}); break;
    case Kind::kKill: g_pulses.push_back({0.22f, 0.30f, 4}); break;
    case Kind::kBigKill: g_pulses.push_back({0.55f, 0.65f, 6}); break;
    case Kind::kBlip: g_pulses.push_back({0.0f, 0.22f, 2}); break;
  }
}

// The score went up (a block of digits, a byte each, read as a number: compared byte by byte).
bool DigitsUp(const st::Machine& m, uint32_t addr, int n) {
  std::vector<uint8_t> now(m.Ram() + addr, m.Ram() + addr + n);
  bool up = !g_digits.empty() && g_digits.size() == now.size() &&
            std::lexicographical_compare(g_digits.begin(), g_digits.end(), now.begin(), now.end());
  g_digits = std::move(now);
  return up;
}

// Photon Storm: the score digits on the screen shown (planar, 160 bytes a line) - a picture not seen in the last
// frames (two screens alternate) = the score changed.
bool HudChanged(const st::Machine& m) {
  uint32_t base = m.FrameBase() & 0xFFFFFF, h = 2166136261u;
  for (int y = 18; y <= 37; ++y)
    for (int b = 0; b < 32; ++b) h = (h ^ m.Ram()[(base + uint32_t(y) * 160 + uint32_t(b)) & 0xFFFFFF]) * 16777619u;
  bool seen = h == g_hud[0] || h == g_hud[1] || h == g_hud[2];
  g_hud[2] = g_hud[1], g_hud[1] = g_hud[0], g_hud[0] = h;
  return !seen;
}

}  // namespace

void Start(const std::string& id) {
  g_id = id;
  g_watches.clear();
  g_frames = 0;
  g_touched = -100000;
  g_digits.clear();
  g_hud[0] = 1, g_hud[1] = 2, g_hud[2] = 3;
  g_flash_prev = 0, g_level_prev = 0, g_kills = 0, g_death_at = -100000;
  g_puck_x = g_puck_y = -1;
  g_pulses.clear();
  g_sent_low = g_sent_high = 0;
  using K = Kind;
  using W = When;
  if (id == "llamatron") g_watches = {{0x5C0CF, 1, W::kDropsByOne, K::kDeath}};
  if (id == "gridrunner") g_watches = {{0x4B632, 2, W::kPlayToLost, K::kDeath}};
  if (id == "andes") g_watches = {{0x12BB6, 2, W::kDropsByOne, K::kDeath}, {0x12BB4, 2, W::kDropsByOne, K::kBomb}};
  if (id == "photonstorm")
    g_watches = {{0x1D124, 2, W::kDropsByOne, K::kDeath}, {0x1D126, 2, W::kDropsByOne, K::kBomb}};
  if (id == "rmc") g_watches = {{0x19264, 2, W::kDropsByOne, K::kDeath}};
  if (id == "rmctt") g_watches = {{0x1929C, 2, W::kDropsByOne, K::kDeath}};
  if (id == "hardcore") g_watches = {{0x14839, 1, W::kDropsByOne, K::kDeath}};
  if (id == "defender2")
    g_watches = {{0x2F2DE, 2, W::kStartsFromZero, K::kDeath}, {0x2BA44, 2, W::kDropsByOne, K::kBomb}};
  g_prev.assign(g_watches.size(), -1);
}

void Frame(const st::Machine& m, SDL_Gamepad* pad) {
  ++g_frames;
  if (PadUsed(pad)) g_touched = g_frames;
  const bool idle = g_frames - g_touched > 750;  // nobody at the pad for 15 s: an attract demo
  const bool settled = g_frames >= 100;  // (the programs set their RAM up first; packed data can sit there before)

  for (size_t i = 0; i < g_watches.size(); ++i) {
    const Watch& w = g_watches[i];
    int v = Read(m, w.addr, w.size), was = g_prev[i];
    g_prev[i] = v;
    if (was < 0 || !settled) continue;
    bool hit = (w.when == When::kDropsByOne && v == was - 1) || (w.when == When::kStartsFromZero && was == 0 && v > 0) ||
               (w.when == When::kPlayToLost && was == 1 && v == 4);
    if (hit) Event(w.kind, idle);
  }

  // Kills, pickups and the continuous hums (per game; play only).
  float hum_low = 0, hum_high = 0;
  if (g_id == "andes") {
    const bool play = Read(m, 0x12EEE, 2) == 1;
    if (DigitsUp(m, 0x12B96, 12) && play && settled) Event(Kind::kKill, idle);
    if (play) {
      float speed = std::clamp(Read(m, 0x14BC4, 2) / 19.0f, 0.0f, 1.0f);
      if (speed > 0) hum_low = 0.05f + 0.13f * speed;  // lowest to low, with speed
    }
  } else if (g_id == "defender2") {
    const bool play = Read(m, 0x4B086, 2) == 0 && Read(m, 0x2F2DE, 2) == 0;
    if (DigitsUp(m, 0x4B098, 10) && play && settled) Event(Kind::kKill, idle);
    if (play) {
      float v = std::clamp(std::fabs(float(int32_t(Read32(m, 0x1B4B4))) / 65536.0f) / 9.0f, 0.0f, 1.0f);
      if (v > 0.03f) hum_low = 0.05f + 0.13f * v;
    }
  } else if (g_id == "photonstorm") {
    const bool play = Read(m, 0x44846, 2) == 1;
    const bool hud = HudChanged(m);  // (not just after a death: the panel is redrawn)
    if (hud && play && settled && g_frames - g_death_at > 150) Event(Kind::kKill, idle);
    if (play && (m.Ram()[0x27C51] & 2) && Read(m, 0x1D12C, 2) > 0) hum_low = 0.06f;  // boost: very low
  } else if (g_id == "llamatron") {
    const uint32_t list = Read32(m, m.TextBase() + 0x25EA), kill = m.TextBase() + 0x25E6;
    if (list == kill && g_flash_prev != kill && settled) Event(Kind::kBigKill, idle);
    g_flash_prev = list;
  } else if (g_id == "hardcore") {
    if (Read32(m, 0x338BA) == 0x10C82) {  // play
      double x = int32_t(Read32(m, 0x1B714)) / 65536.0, y = int32_t(Read32(m, 0x1B718)) / 65536.0;
      if (g_puck_x >= 0) {
        double speed = std::hypot(x - g_puck_x, y - g_puck_y);
        if (speed < 20) {  // (not a jump: a new puck)
          float amp = float(std::clamp(0.02 + 0.025 * speed, 0.0, 0.14));
          float side = float(std::clamp((x - 21) / (257 - 21), 0.0, 1.0));  // 0 left edge .. 1 right edge
          hum_low = amp * (1.4f - side), hum_high = amp * (0.4f + side);
        }
      }
      g_puck_x = x, g_puck_y = y;
    } else {
      g_puck_x = g_puck_y = -1;
    }
  } else if (g_id == "rmc" || g_id == "rmctt") {
    const bool tt = g_id == "rmctt";
    const bool play = Read32(m, tt ? 0x3E0CE : 0x3DD3E) == (tt ? 0x11206u : 0x111DEu);
    const uint32_t pal = Read32(m, tt ? 0x3D960 : 0x3D5D0);
    const int level = Read(m, tt ? 0x3D964 : 0x3D5D4, 2);
    const bool pickup_pal = tt ? pal == 0x3D9C6 || pal == 0x3DAC6 : pal == 0x3D636 || pal == 0x3D736;
    if (play && settled && pickup_pal && level > g_level_prev) Event(Kind::kBlip, idle);
    g_level_prev = level;
  }
  {  // (the log: the strongest hum every 10 s, for checking scripted runs)
    static float most_low = 0, most_high = 0;
    most_low = std::max(most_low, hum_low), most_high = std::max(most_high, hum_high);
    if (g_frames % 500 == 0 && (most_low > 0 || most_high > 0)) {
      LOG("rumble: hum up to %.2f / %.2f%s", most_low, most_high, idle ? " (idle)" : "");
      most_low = most_high = 0;
    }
  }
  if (idle) hum_low = hum_high = 0;

  // Out: the strongest of the hum and the pulses, per motor.
  float low = hum_low, high = hum_high;
  for (Pulse& p : g_pulses) low = std::max(low, p.low), high = std::max(high, p.high), --p.frames;
  g_pulses.erase(std::remove_if(g_pulses.begin(), g_pulses.end(), [](const Pulse& p) { return p.frames <= 0; }),
                 g_pulses.end());
  if (!pad || settings::Get("rumble", 1) == 0) return;
  const uint16_t ul = uint16_t(std::clamp(low, 0.0f, 1.0f) * 65535), uh = uint16_t(std::clamp(high, 0.0f, 1.0f) * 65535);
  if (ul != g_sent_low || uh != g_sent_high || ((ul || uh) && g_frames - g_sent_at >= 5)) {
    SDL_RumbleGamepad(pad, ul, uh, ul || uh ? 150 : 0);
    g_sent_low = ul, g_sent_high = uh, g_sent_at = g_frames;
  }
}

}  // namespace rumble
