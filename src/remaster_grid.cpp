// Gridrunner: the remaster (remaster.h; REMASTER.md). Runtime addresses (the program unpacks itself in place).
// The same engine family as Andes Attack (the draw core is the same code).
//
// Play's frame ($144D4, from the main loop $4BA12): $144DA sets the draw mode $4D046 = 0 and draws into the screen
// $54DC2 (clips: groups $58044..$58046, lines $5804A..$5804C, set per part of the frame); $145E2 hands it over to be
// shown ($4D2E8); after the VBL wait, the restore pass (mode 2, $4BC18: last time's list redrawn from the
// background) and the three screens and lists rotate.
//   $4BCE4 the draw core: a0 = the sprite (word type 0..4 -> the table $4BCD0; 0 and 2 draw at even x; +2 width in
//     16-px groups, +4 height), d0 / d1 = top-left, a1 = the screen; $58054 a colour some types draw in. Reached through $4BB82 (writes the restore
//     entry, then calls it: return $4BBA8, the drawing code's return at sp + 12; with $4CF30 set it draws into the
//     background and every screen: the HUD, left alone) and $4BBFE (centres first, one word pushed: the drawing
//     code at sp + 18). Snakes: $1655E walks a chain and draws each segment through $4BBFE (a3 = the segment;
//     gridrunner.cpp's entry-lane hook may skip it first).
// Restoring something we didn't draw copies background over background: harmless.
#include "remaster_grid.h"

#include <cstring>
#include <vector>

#include "cpu.h"
#include "gridrunner.h"
#include "log.h"
#include "remaster.h"

namespace remaster_grid {
namespace {

constexpr uint32_t kBegin = 0x144DA, kEnd = 0x145E2, kScreen = 0x54DC2, kShow = 0x4D2E8;
constexpr uint32_t kCore = 0x4BCE4, kCoreGo = 0x4BCEA, kCoreTable = 0x4BCD0;
constexpr uint32_t kMode = 0x4D046, kAllScreens = 0x4CF30;
constexpr uint32_t kColour = 0x58054;  // the colour some sprite types draw in (the type-4 handler at $4C206; < 0: their own)
constexpr uint32_t kClipLeft = 0x58044, kClipRight = 0x58046, kClipTop = 0x5804A, kClipBottom = 0x5804C;
constexpr uint32_t kFromList = 0x4BBA8, kFromCentred = 0x4BC14;
constexpr uint32_t kHudFrom = 0x4CDBA, kHudTo = 0x4CE04;  // a HUD icon painted into the background and each screen

bool g_installed = false;
bool g_capture = false;  // this frame's drawing goes to remaster
bool g_in_pass = false;  // between play's draw pass start and end (other frame routines draw too: $4CE2A)
uint32_t g_screen = 0;

int Get16(const st::Machine& m, uint32_t a) { return int16_t(m.Ram()[a] << 8 | m.Ram()[a + 1]); }
uint32_t Get32(const st::Machine& m, uint32_t a) {
  const uint8_t* p = m.Ram() + (a & 0xFFFFFF);
  return uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3];
}
void Put16(st::Machine* m, uint32_t a, int v) { m->Ram()[a] = uint8_t(v >> 8), m->Ram()[a + 1] = uint8_t(v); }
void Put32(st::Machine* m, uint32_t a, uint32_t v) { Put16(m, a, int(v >> 16)), Put16(m, a + 2, int(v & 0xFFFF)); }
uint32_t Reg(int r) { return cpu::Get(cpu::Reg(r)); }
int RegW(int r) { return int16_t(Reg(r)); }

// States that keep the last play picture (fading it): 2 between play and the pause screen (and other breaks), 11.
// The pause screen (12) is a picture of its own.
bool Frozen(int state) { return state == 2 || state == grid::kPausing; }

// $144DA "move.w #0,$4D046": play's draw pass starts.
void OnBegin(st::Machine& m) {
  if (!remaster::Decoding(m)) {
    int state = grid::GameState(m);
    g_capture = remaster::Enabled() && state == grid::kPlay;
    g_screen = Get32(m, kScreen) & 0xFFFFFF;
    g_in_pass = g_capture;
    if (g_capture) remaster::BeginFrame(g_screen);
    else if (Frozen(state) && remaster::Enabled()) remaster::Hold();
    else remaster::Stop();
  }
  Put16(&m, kMode, 0);
  cpu::Set(cpu::PC, kBegin + 8);
}

// $145E2 "move.l $54DC2,$4D2E8" (the screen is to be shown): the pass is complete.
void OnEnd(st::Machine& m) {
  if (!remaster::Decoding(m) && g_capture) remaster::EndFrame(g_screen);
  if (!remaster::Decoding(m)) g_in_pass = false;
  Put32(&m, kShow, Get32(m, kScreen));
  cpu::Set(cpu::PC, kEnd + 10);
}

// $4BCE4 "movea.l #$4BCD0,a2".
void OnCore(st::Machine& m) {
  auto original = [] {
    cpu::Set(cpu::Reg(cpu::A0 + 2), kCoreTable);
    cpu::Set(cpu::PC, kCoreGo);
  };
  if (remaster::Decoding(m) || !g_in_pass || Get16(m, kMode) != 0 || Get16(m, kAllScreens) != 0 ||
      (Reg(cpu::A0 + 1) & 0xFFFFFF) != g_screen)
    return original();
  uint32_t a0 = Reg(cpu::A0) & 0xFFFFFF, sp = Reg(cpu::A7) & 0xFFFFFF, ret = Get32(m, sp) & 0xFFFFFF;
  if (ret >= kHudFrom && ret < kHudTo) return original();
  uint32_t caller = ret, object = 0;
  if (ret == kFromList) {
    caller = Get32(m, sp + 12) & 0xFFFFFF;
    if (caller == kFromCentred) caller = Get32(m, sp + 18) & 0xFFFFFF, object = Reg(cpu::A0 + 3);
  }
  // The image: the sprite and the colour word (level 4's rising text is drawn in colour 8 through it).
  uint64_t id = a0 | uint64_t(uint16_t(Get16(m, kColour))) << 32;
  int type = Get16(m, a0);
  if (!remaster::HaveImage(id)) {
    remaster::SandboxCall call;
    call.routine = kCore;
    for (int r = 0; r < 16; ++r) call.regs[r] = Reg(r);
    int h = Get16(m, a0 + 4);
    call.at_x = 128, call.at_y = h > 0 && h <= 180 ? (200 - h) / 2 : 80;
    call.regs[0] = uint32_t(call.at_x), call.regs[1] = uint32_t(call.at_y);
    call.pokes = {{kMode, 0}, {kClipTop, 0}, {kClipBottom, 199}, {kClipLeft, 0}, {kClipRight, 19},
                  {kColour, uint16_t(Get16(m, kColour))}};
    remaster::QueueImage(id, call);
    if (!m.ahead) return original();  // drawn by the game this once
  // In the run-ahead copy the item is kept anyway (its image is decoded right after the copy's run): what that copy
  // draws is never shown, so letting it draw there would lose the sprite for a frame (a flicker).
  }
  remaster::Item it;
  it.key = remaster::Key(caller, object);
  it.x = float(RegW(cpu::D0)), it.y = float(RegW(cpu::D0 + 1));
  it.image = id;
  it.snap = type == 0 || type == 2 ? 2 : 1;
  it.clip_top = int16_t(Get16(m, kClipTop)), it.clip_bottom = int16_t(Get16(m, kClipBottom));
  it.clip_left = int16_t(Get16(m, kClipLeft) * 16), it.clip_right = int16_t(Get16(m, kClipRight) * 16 + 15);
  remaster::Add(g_screen, it);
  remaster::Return(m);  // not drawn
}

void Install(st::Machine* m) {
  struct H {
    uint32_t at;
    std::vector<uint8_t> code;
    void (*fn)(st::Machine&);
  };
  static const H kHooks[] = {
      {kBegin, {0x33, 0xFC, 0x00, 0x00, 0x00, 0x04, 0xD0, 0x46}, OnBegin},                // move.w #0,$4D046
      {kEnd, {0x23, 0xF9, 0x00, 0x05, 0x4D, 0xC2, 0x00, 0x04, 0xD2, 0xE8}, OnEnd},        // move.l $54DC2,$4D2E8
      {kCore, {0x24, 0x7C, 0x00, 0x04, 0xBC, 0xD0}, OnCore},                              // movea.l #$4BCD0,a2
  };
  g_installed = true;
  for (const H& h : kHooks)
    if (std::memcmp(m->Ram() + h.at, h.code.data(), h.code.size()) != 0) {
      LOG("gridrunner: remaster hooks not installed ($%06X differs)", h.at);
      return;
    }
  for (const H& h : kHooks) {
    Put16(m, h.at, 0x4AFC);
    m->AddHook(h.at, [fn = h.fn](st::Machine& running) { fn(running); });
  }
  g_capture = false;
  remaster::Stop();
  struct Saved {
    bool capture, in_pass;
    uint32_t screen;
  };
  static Saved saved;
  remaster::SetAheadState([] { saved = {g_capture, g_in_pass, g_screen}; },
                          [] { g_capture = saved.capture, g_in_pass = saved.in_pass, g_screen = saved.screen; });
  LOG("gridrunner: remaster hooks installed");
}

}  // namespace

void Start() { g_installed = g_capture = g_in_pass = false; }

void Update(st::Machine* m) {
  int state = grid::GameState(*m);
  // From play on (game memory holds the packed program until the title has been seen).
  if (!g_installed && state == grid::kPlay) Install(m);
  if (!g_installed) return;
  remaster::DrawQueued(m, "gridrunner");
  if (Frozen(state) && remaster::Enabled()) remaster::Hold();
  // Left play some other way than through the draw pass: the ST picture.
  else if (g_capture && state != grid::kPlay) g_capture = false, remaster::Stop();
}

}  // namespace remaster_grid
