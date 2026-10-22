#include "game.h"

#include <cstring>

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

}  // namespace

void ApplyPatches(st::Machine& m, const Options& o) {
  if (!o.flashes) {
    // The VBL ($5398) plays the colour list at *$25EA into palette 0 (the background), one entry per frame:
    // white on every enemy kill ($1450 -> $25E6), fades, and strobes alternating every frame (red/dark blue at
    // $4AF2, multicolour at $25EE...). Drop the write "move.w d0,$ff8240.l" at $53DA: the lists still run (same
    // timing, one still cuts another short), the background stays black. The end of a list writes 0 as before.
    Apply(m, {"no screen flashes", 0x53DA, {0x33, 0xC0, 0x00, 0xFF, 0x82, 0x40}, {0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71}});
  }
}

}  // namespace game
