#include "game.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "cpu.h"
#include "log.h"

namespace game {
namespace {

struct Patch {
  const char* name;
  uint32_t offset;  // in the text
  std::vector<uint8_t> original, replacement;
};

bool Apply(st::Machine& m, const Patch& p) {
  uint8_t* at = m.Ram() + m.TextBase() + p.offset;
  if (std::memcmp(at, p.original.data(), p.original.size()) != 0) {
    LOG("patch %s: unexpected code at $%X, not applied", p.name, p.offset);
    return false;
  }
  std::memcpy(at, p.replacement.data(), p.replacement.size());
  LOG("patch %s applied", p.name);
  return true;
}

uint16_t Get16(st::Machine& m, uint32_t a) { return uint16_t(m.Ram()[a] << 8 | m.Ram()[a + 1]); }
void Put16(st::Machine& m, uint32_t a, uint16_t v) { m.Ram()[a] = uint8_t(v >> 8), m.Ram()[a + 1] = uint8_t(v); }

// ---- Analog aiming (twin-stick mode) ----
// A shot ($54FD4, 16-byte slots, (a0) < 0 = free): +0 x, +2 y, +4 dx, +6 dy (pixels per frame: -3/0/3),
// +8 sprite (29 horizontal, 30 vertical, 31 "/", 32 "\"), +$C hot bullet lifetime. The movement routine ($1198,
// in the VBL) adds dx/dy. We keep an exact position and velocity per slot and set dx/dy before every move so the
// shot follows any angle. Speed 3 / max(|cos|, |sin|): the original's 3 straight and 3 per axis diagonally.
// Hot bullets ($1634) bounce off the walls by looking the shot up in the 8-direction table: then shots snap to
// the nearest of the 8 directions.
constexpr uint32_t kShots = 0x54FD4, kShotSlots = 64, kLlamaX = 0x4CFA4, kLlamaY = 0x4CFA6, kHot = 0x1634;
constexpr uint32_t kTwinStick = 0x8616;

struct Aim {
  bool on = false;
  float x = 0, y = 0;
} g_aim;

struct Shot {
  bool on = false;
  double fx, fy, vx, vy;
  int16_t x, y;  // where we last put it
} g_shots[kShotSlots];

void Octant(double vx, double vy, int16_t* dx, int16_t* dy, uint16_t* sprite) {
  int o = int(std::lround(std::atan2(vy, vx) / (3.14159265358979 / 4))) & 7;  // 0 = right, clockwise (y down)
  static const int kDx[8] = {3, 3, 0, -3, -3, -3, 0, 3}, kDy[8] = {0, 3, 3, 3, 0, -3, -3, -3};
  *dx = int16_t(kDx[o]), *dy = int16_t(kDy[o]);
  *sprite = *dx == 0 ? 30 : *dy == 0 ? 29 : (*dx > 0) == (*dy > 0) ? 32 : 31;
}

// $10A8 "move.w $4CFA6.l,$2(a0)": a new shot in slot a0 (dx/dy/sprite already set from the stick's direction).
void OnShotCreated(st::Machine& m) {
  uint32_t t = m.TextBase(), a0 = cpu::Get(cpu::A0) & 0xFFFFFF;
  Put16(m, a0 + 2, Get16(m, t + kLlamaY));
  cpu::Set(cpu::PC, t + 0x10B0);
  uint32_t slot = (a0 - (t + kShots)) / 16;
  if (a0 < t + kShots || slot >= kShotSlots) return;
  Shot& s = g_shots[slot];
  s.on = false;
  if (!g_aim.on || !Get16(m, t + kTwinStick) || Get16(m, t + kHot)) return;
  double len = std::hypot(g_aim.x, g_aim.y), c = g_aim.x / len, sn = g_aim.y / len;
  double speed = 3.0 / std::max(std::fabs(c), std::fabs(sn));
  s.on = true;
  s.fx = double(int16_t(Get16(m, t + kLlamaX))), s.fy = double(int16_t(Get16(m, t + kLlamaY)));
  s.vx = c * speed, s.vy = sn * speed;
  s.x = int16_t(s.fx), s.y = int16_t(s.fy);
  int16_t dx, dy;
  uint16_t sprite;
  Octant(s.vx, s.vy, &dx, &dy, &sprite);
  Put16(m, a0 + 8, sprite);
}

// $1198 "lea $54FD4.l,a0": the shot movement routine starts; set every analog shot's step for this frame.
void OnShotsMove(st::Machine& m) {
  uint32_t t = m.TextBase(), base = t + kShots;
  cpu::Set(cpu::A0, base);
  cpu::Set(cpu::PC, t + 0x119E);
  bool hot = Get16(m, t + kHot) != 0;
  for (uint32_t i = 0; i < kShotSlots; ++i) {
    Shot& s = g_shots[i];
    if (!s.on) continue;
    uint32_t a = base + i * 16;
    int16_t x = int16_t(Get16(m, a)), y = int16_t(Get16(m, a + 2));
    if (x < 0 || x != s.x || y != s.y) {  // freed, reused, or moved by something else
      s.on = false;
      continue;
    }
    if (hot) {  // back to the nearest of the 8 directions (the bounce code needs one of them)
      int16_t dx, dy;
      uint16_t sprite;
      Octant(s.vx, s.vy, &dx, &dy, &sprite);
      Put16(m, a + 4, uint16_t(dx)), Put16(m, a + 6, uint16_t(dy)), Put16(m, a + 8, sprite);
      s.on = false;
      continue;
    }
    s.fx += s.vx, s.fy += s.vy;
    int16_t nx = int16_t(std::lround(s.fx)), ny = int16_t(std::lround(s.fy));
    Put16(m, a + 4, uint16_t(nx - x)), Put16(m, a + 6, uint16_t(ny - y));
    s.x = nx, s.y = ny;
  }
}

// Replaces the instruction at `off` with ILLEGAL and hooks it, if its opcode and absolute operand (relocated)
// are as expected.
bool Hook(st::Machine& m, const char* name, uint32_t off, uint16_t opcode, uint32_t operand,
          void (*fn)(st::Machine&)) {
  uint32_t a = m.TextBase() + off;
  uint32_t got = uint32_t(Get16(m, a + 2)) << 16 | Get16(m, a + 4);
  if (Get16(m, a) != opcode || got != m.TextBase() + operand) {
    LOG("hook %s: unexpected code at $%X, not installed", name, off);
    return false;
  }
  Put16(m, a, 0x4AFC);
  m.AddHook(a, [&m, fn] { fn(m); });
  LOG("hook %s installed", name);
  return true;
}

}  // namespace

void SetAim(bool on, float x, float y) { g_aim = {on, x, y}; }

void ApplyPatches(st::Machine& m, const Options& o) {
  if (!o.flashes) {
    // The VBL ($5398) plays the colour list at *$25EA into palette 0 (the background), one entry per frame:
    // white on every enemy kill ($1450 -> $25E6), fades, and strobes alternating every frame (red/dark blue at
    // $4AF2, multicolour at $25EE...). Drop the write "move.w d0,$ff8240.l" at $53DA: the lists still run (same
    // timing, one still cuts another short), the background stays black. The end of a list writes 0 as before.
    Apply(m, {"no screen flashes", 0x53DA, {0x33, 0xC0, 0x00, 0xFF, 0x82, 0x40}, {0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71}});
  }
  if (o.analog_aim) {
    Hook(m, "analog aim: shot created", 0x10A8, 0x3179, kLlamaY, OnShotCreated);
    Hook(m, "analog aim: shots move", 0x1198, 0x41F9, kShots, OnShotsMove);
  }
}

}  // namespace game
