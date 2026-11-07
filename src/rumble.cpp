// Rumble (rumble.h). The counters were found from RAM dumps of scripted runs (values that only ever drop by one,
// at the deaths seen, and reset at game over): lives - Llamatron $5C0CF, Andes $12BB6, Photon Storm $1D124,
// Camels $19264 (STE; `subq.w #1` at $19268) / $1929C (TT, Tools/mapaddr.py), Hardcore $14839; smart bombs -
// Andes $12BB4, Photon Storm $1D126, Defender 2 $2BA44. Gridrunner: its state turning from play (1) to the
// ship-lost screen (4). Defender 2: the death countdown $2F2DE starting (300 frames, from 0).
#include "rumble.h"

#include <cstdlib>
#include <vector>

#include "log.h"
#include "settings.h"

namespace rumble {
namespace {

enum class Kind { kDeath, kBomb };
enum class When { kDropsByOne, kStartsFromZero, kPlayToLost };
struct Watch {
  uint32_t addr;
  int size;  // 1 or 2 bytes
  When when;
  Kind kind;
};

std::vector<Watch> g_watches;
std::vector<int> g_prev;
int g_frames = 0;
int g_touched = -100000;  // the frame the pad was last used (attract demos die too: only rumble for a player)

bool PadUsed(SDL_Gamepad* pad) {
  if (!pad) return false;
  for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b)
    if (SDL_GetGamepadButton(pad, SDL_GamepadButton(b))) return true;
  for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; ++a)
    if (std::abs(SDL_GetGamepadAxis(pad, SDL_GamepadAxis(a))) > 10000) return true;
  return false;
}

int Read(const st::Machine& m, const Watch& w) {
  const uint8_t* p = m.Ram() + w.addr;
  return w.size == 2 ? p[0] << 8 | p[1] : p[0];
}

}  // namespace

void Start(const std::string& id) {
  g_watches.clear();
  g_frames = 0;
  g_touched = -100000;
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
  for (size_t i = 0; i < g_watches.size(); ++i) {
    const Watch& w = g_watches[i];
    int v = Read(m, w), was = g_prev[i];
    g_prev[i] = v;
    // (Not in the first seconds: the programs set their RAM up then; packed data can sit there before.)
    if (was < 0 || g_frames < 100) continue;
    bool hit = (w.when == When::kDropsByOne && v == was - 1) || (w.when == When::kStartsFromZero && was == 0 && v > 0) ||
               (w.when == When::kPlayToLost && was == 1 && v == 4);
    if (!hit) continue;
    const bool idle = g_frames - g_touched > 750;  // nobody at the pad for 15 s: an attract demo
    LOG("rumble: %s at frame %d%s", w.kind == Kind::kDeath ? "life lost" : "smart bomb", g_frames, idle ? " (idle)" : "");
    if (!pad || idle || settings::Get("rumble", 1) == 0) continue;
    if (w.kind == Kind::kDeath) SDL_RumbleGamepad(pad, 0xD000, 0x7000, 550);
    else SDL_RumbleGamepad(pad, 0x9000, 0xB000, 260);
  }
}

}  // namespace rumble
