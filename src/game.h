// What we know about Llamatron's state in memory (text offsets; see CLAUDE.md "Controls").
#pragma once

#include <cstdint>

#include "machine.h"

namespace game {

inline uint32_t Long(st::Machine& m, uint32_t off) {
  const uint8_t* p = m.Ram() + m.TextBase() + off;
  return uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3];
}

// The title screen is up: its frame handler ($551C, the option selector) is the VBL hook ($52FC2).
inline bool AtTitle(st::Machine& m) { return Long(m, 0x52FC2) == m.TextBase() + 0x551C; }

// Title mode (bits 0-1 of $4CF9A): 0 = 1 PLAYER, 1 = PLAYER + DROID, 3 = TEAM.
inline int Mode(st::Machine& m) { return m.Ram()[m.TextBase() + 0x4CF9A] & 3; }  // btst.b: the high byte

// Fire on joystick 0 starts the twin-stick ("Robotron style") game in these modes; the title ignores it in
// PLAYER + DROID.
inline bool TwinStickAllowed(st::Machine& m) { return Mode(m) != 1; }

// The game in progress (or starting) is the twin-stick one: $8616, set when the title is left.
inline bool TwinStick(st::Machine& m) { return m.Ram()[m.TextBase() + 0x8616 + 1] != 0; }

}  // namespace game
