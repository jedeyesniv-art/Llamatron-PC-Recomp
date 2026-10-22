// What we know about Llamatron's state in memory (text offsets; see CLAUDE.md "Controls").
#pragma once

#include <cstdint>
#include <vector>

#include "machine.h"

namespace game {

inline uint32_t Long(st::Machine& m, uint32_t off) {
  const uint8_t* p = m.Ram() + m.TextBase() + off;
  return uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3];
}

// The title screen is up: its frame handler ($551C, the option selector) is the VBL hook ($52FC2).
inline bool AtTitle(st::Machine& m) { return Long(m, 0x52FC2) == m.TextBase() + 0x551C; }

// Playing: the gameplay VBL handler ($1EDA, all the game logic) is the VBL hook. Not during the intros, the
// instructions scroller, wave intros/transitions or the pause screens (some of those are timed by CPU speed).
inline bool Playing(st::Machine& m) { return Long(m, 0x52FC2) == m.TextBase() + 0x1EDA; }

// The game's pause screen (P): its VBL handler is $1D32 (set by $1F22).
inline bool Paused(st::Machine& m) { return Long(m, 0x52FC2) == m.TextBase() + 0x1D32; }

// Title mode (bits 0-1 of $4CF9A): 0 = 1 PLAYER, 1 = PLAYER + DROID, 3 = TEAM.
inline int Mode(st::Machine& m) { return m.Ram()[m.TextBase() + 0x4CF9A] & 3; }  // btst.b: the high byte

// Fire on joystick 0 starts the twin-stick ("Robotron style") game in these modes; the title ignores it in
// PLAYER + DROID.
inline bool TwinStickAllowed(st::Machine& m) { return Mode(m) != 1; }

// The game in progress (or starting) is the twin-stick one: $8616, set when the title is left.
inline bool TwinStick(st::Machine& m) { return m.Ram()[m.TextBase() + 0x8616 + 1] != 0; }

// Changes to the game's code, applied after loading (each checks the original bytes first).
struct Options {
  bool flashes = false;  // the background flashes and strobes (white on every kill...); original: on
};
void ApplyPatches(st::Machine& m, const Options& o);

}  // namespace game
