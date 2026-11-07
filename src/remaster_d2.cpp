// Defender 2: the remaster (remaster.h; REMASTER.md). Runtime addresses (DEFENDR2.EXE). The Andes / Gridrunner
// engine again.
//
// The main loop ($4CC86) writes the restore list's end marker, then "jsr (a3)" at $4CC9A with a3 = *$4CC46 (the
// screen's frame routine; play: $1AA6A), which draws into the screen $6244C and ends by putting it on display
// ($1B344) - so the pass is complete at $4CC9C. Then it waits a VBL, restores the other screen $62450 from its list
// ($4B77A: clears or copies the background $4CFCE: skipping a draw is harmless) and swaps.
//   $4B8B0 the draw core: a0 = the sprite (word type 0..8 -> the table $4B88C; 0, 2, 5 and 8 draw at even x;
//     +2 16-px groups - 1, +4 height, +6 width), d0 / d1 = top-left, a1 = the screen; $6450A a colour some types
//     draw in; $4CFD2 the mode (0 draw, else restore from the background); clips: groups $644FA..$644FC, lines
//     $64500..$64502. Reached through $4B6BA (writes the restore entry: return $4B6E2, the drawing code at sp + 12;
//     with $4CEBC set it draws into every screen and the background: the HUD, left alone) and the centring
//     wrapper $4B74C (its call at $4B772: the drawing code at sp + 18).
// Outside play (2026-11-06) the same main loop draws the attract screens with their own frame routines, through the
// same core: $1FA38 the title (state 2: its three preview boxes), $117F0 player select and $11C70 the high scores
// (8). Captured too, drawn every VBL. Not the version screen ($1EB24) nor the logo ($1E92A: pass-timed).
#include "remaster_d2.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "cpu.h"
#include "log.h"
#include "remaster.h"

namespace remaster_d2 {
namespace {

constexpr uint32_t kState = 0x4B086;  // 0 play
constexpr uint32_t kFramePtr = 0x4CC46, kPlayFrame = 0x1AA6A;
constexpr uint32_t kLoopCall = 0x4CC9A, kLoopAfter = 0x4CC9C, kAfterSkip = 0x4CD0C, kAfterGo = 0x4CCA6;
constexpr uint32_t kAfterFlag = 0x1C548;
constexpr uint32_t kScreen = 0x6244C;
constexpr uint32_t kCore = 0x4B8B0, kCoreGo = 0x4B8B6, kCoreTable = 0x4B88C;
constexpr uint32_t kMode = 0x4CFD2, kAllScreens = 0x4CEBC, kColour = 0x6450A;
constexpr uint32_t kClipLeft = 0x644FA, kClipRight = 0x644FC, kClipTop = 0x64500, kClipBottom = 0x64502;
constexpr uint32_t kFromList = 0x4B6E2, kFromCentred = 0x4B776;
constexpr uint32_t kSprite = 0x1DF2A, kSpriteWide = 0x1DCB2;  // the Camels' "type 3" routine (any x, centred)
constexpr uint32_t kDot1 = 0x4A8A2, kDot2 = 0x4A952, kCell = 0x19716, kCellNext = 0x197B4;
constexpr uint32_t kStarsA = 0x13690, kStarsB = 0x1ABFC, kTerrain = 0x1AE42, kSpan = 0x1C76C, kBand = 0x1AF50;
constexpr uint32_t kRows = 0x64594, kScroll = 0x2BA32;
constexpr uint32_t kMono = 0x1E14C, kPlanePixel = 0x198F4;
constexpr uint32_t kParticle = 0x1C5CA, kPlanet = 0x1AD5C, kPlanetImage = 0x3D318, kPlanetDone = 0x1AD72;

bool g_installed = false;
bool g_capture = false;  // this frame's drawing goes to remaster
bool g_in_pass = false;  // inside play's frame routine
uint32_t g_screen = 0;
std::vector<std::pair<int, std::vector<uint8_t>>> g_rows;  // the lasers' rows this pass: row, 320 colours (OnSpan)
// The planet drawn this pass (OnPlanet): its image and top-left, for the lasers over it (FlushSpans).
uint64_t g_planet_image = 0;
constexpr uint8_t kLaserBit = 0x20;  // in g_rows: a laser's pixel (OnSpan; the other OR-ed pixels stay OR-ed)
int g_planet_x = 0, g_planet_y = 0;
const st::Machine* g_m = nullptr;  // the machine of the pass (a run-ahead copy too): AddPlaneDot, FlushSpans

int Get16(const st::Machine& m, uint32_t a) { return int16_t(m.Ram()[a] << 8 | m.Ram()[a + 1]); }
uint32_t Get32(const st::Machine& m, uint32_t a) {
  const uint8_t* p = m.Ram() + (a & 0xFFFFFF);
  return uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3];
}
void Put16(st::Machine* m, uint32_t a, int v) { m->Ram()[a] = uint8_t(v >> 8), m->Ram()[a + 1] = uint8_t(v); }
uint32_t Reg(int r) { return cpu::Get(cpu::Reg(r)); }
int RegW(int r) { return int16_t(Reg(r)); }

// Every capture but the lasers' goes through here: laser rows gathered so far are handed over first, so they keep
// their place in the drawing order (the planet and the ground are drawn over them).
void FlushSpans();
void Put(const remaster::Item& it) {
  FlushSpans();
  remaster::Add(g_screen, it);
}

bool Playing(const st::Machine& m) { return Get16(m, kState) == 0 && Get32(m, kFramePtr) == kPlayFrame; }
// (Not the logo $1E92A, state 6: its lightning grows per drawn frame - at 60 a second the strikes came at other
// moments and the screen ended sooner.)
constexpr uint32_t kAttractFrames[] = {0x1FA38, 0x117F0, 0x11C70};

}  // namespace

bool DrawsEveryVbl(const st::Machine& m) { return Playing(m) || Attract(m); }
bool Captured(const st::Machine& m) { return DrawsEveryVbl(m); }

// Audit 2026-11-07: also the end-of-wave screen (state 4, frame $12848: ATTACK LEVEL n COMPLETED, the bonus
// humanoids, a wireframe pyramid) and the fade when the ship is lost (state 5 with play's frame). Both are partly
// timed by their drawing: every VBL on the faster CPU they end 4 / 3 frames (of 340 / 38) sooner than the
// original, capture on or off alike; captured at the original pace they'd change with the capture instead.
bool Attract(const st::Machine& m) {
  int state = Get16(m, kState);
  uint32_t frame = Get32(m, kFramePtr);
  if (state == 4) return frame == 0x12848;
  if (state == 5) return frame == kPlayFrame;
  if (state != 2 && state != 8) return false;
  for (uint32_t f : kAttractFrames)
    if (frame == f) return true;
  return false;
}

namespace {

// $4CC9A "jsr (a3)": a frame is drawn.
void OnLoopCall(st::Machine& m) {
  g_m = &m;
  if (!remaster::Decoding(m)) {
    g_capture = remaster::Enabled() && Captured(m);
    g_screen = Get32(m, kScreen) & 0xFFFFFF;
    g_in_pass = g_capture;
    if (g_capture) remaster::BeginFrame(g_screen);
    else remaster::Stop();
    g_rows.clear();
    g_planet_image = 0;
  }
  uint32_t sp = Reg(cpu::A7) - 4;
  cpu::Set(cpu::Reg(cpu::A7), sp);
  Put16(&m, sp & 0xFFFFFF, int(kLoopAfter >> 16));
  Put16(&m, (sp + 2) & 0xFFFFFF, int(kLoopAfter & 0xFFFF));
  cpu::Set(cpu::PC, Reg(cpu::A0 + 3));
}

// $4CC9C "tst.w $1C548; beq": the frame is complete (and on display).
void OnLoopAfter(st::Machine& m) {
  if (!remaster::Decoding(m)) {
    if (g_capture) FlushSpans(), remaster::EndFrame(g_screen);
    g_in_pass = false;
  }
  cpu::Set(cpu::PC, Get16(m, kAfterFlag) == 0 ? kAfterSkip : kAfterGo);
}

// $4B8B0 "movea.l #$4B88C,a2".
void OnCore(st::Machine& m) {
  auto original = [] {
    cpu::Set(cpu::Reg(cpu::A0 + 2), kCoreTable);
    cpu::Set(cpu::PC, kCoreGo);
  };
  if (remaster::Decoding(m) || !g_in_pass || Get16(m, kMode) != 0 || Get16(m, kAllScreens) != 0 ||
      (Reg(cpu::A0 + 1) & 0xFFFFFF) != g_screen)
    return original();
  uint32_t a0 = Reg(cpu::A0) & 0xFFFFFF, sp = Reg(cpu::A7) & 0xFFFFFF, ret = Get32(m, sp) & 0xFFFFFF;
  uint32_t caller = ret;
  if (ret == kFromList) {
    caller = Get32(m, sp + 12) & 0xFFFFFF;
    if (caller == kFromCentred) caller = Get32(m, sp + 18) & 0xFFFFFF;
  }
  uint32_t object = 0;
  uint64_t id = a0 | uint64_t(uint16_t(Get16(m, kColour))) << 32;
  int type = Get16(m, a0);
  if (!remaster::HaveImage(id)) {
    remaster::SandboxCall call;
    call.routine = kCore;
    for (int r = 0; r < 16; ++r) call.regs[r] = Reg(r);
    uint32_t h = type == 5 || type == 8 ? (Get32(m, a0 + 4) & 0xFFFFFF) - 2 : a0;
    int ht = Get16(m, h + 4);
    call.at_x = 128, call.at_y = ht > 0 && ht <= 180 ? (200 - ht) / 2 : 80;
    call.regs[0] = uint32_t(call.at_x), call.regs[1] = uint32_t(call.at_y);
    call.pokes = {{kMode, 0}, {kAllScreens, 0}, {kClipTop, 0}, {kClipBottom, 199}, {kClipLeft, 0},
                  {kClipRight, 19}, {kColour, uint16_t(Get16(m, kColour))}};
    remaster::QueueImage(id, call);
    // (Kept at first sight too: main decodes the image right after the frame - remaster::DrawQueued - before it is shown.)
  }
  remaster::Item it;
  it.key = remaster::Key(caller, object);
  it.x = float(RegW(cpu::D0)), it.y = float(RegW(cpu::D0 + 1));
  it.image = id;
  it.snap = type == 0 || type == 2 || type == 5 || type == 8 ? 2 : 1;
  it.clip_top = int16_t(Get16(m, kClipTop)), it.clip_bottom = int16_t(Get16(m, kClipBottom));
  it.clip_left = int16_t(Get16(m, kClipLeft) * 16), it.clip_right = int16_t(Get16(m, kClipRight) * 16 + 15);
  Put(it);
  remaster::Return(m);  // not drawn
}

// $1DF2A "tst.w 2(a0); bne $1DCB2": the enemies, the planet... - a0 the sprite (+2 groups - 1, +4 height, +6
// width), centred on d0 / d1, any x, clipped to lines 28..199 and the screen's columns.
void OnSprite(st::Machine& m) {
  uint32_t a0 = Reg(cpu::A0) & 0xFFFFFF;
  auto original = [&] { cpu::Set(cpu::PC, Get16(m, a0 + 2) != 0 ? kSpriteWide : kSprite + 8); };
  if (remaster::Decoding(m) || !g_in_pass || (Reg(cpu::A0 + 1) & 0xFFFFFF) != g_screen) return original();
  uint32_t caller = Get32(m, Reg(cpu::A7)) & 0xFFFFFF;
  uint64_t id = a0 | uint64_t(1) << 62;
  if (!remaster::HaveImage(id)) {
    remaster::SandboxCall call;
    call.routine = kSprite;
    for (int r = 0; r < 16; ++r) call.regs[r] = Reg(r);
    call.at_x = 160, call.at_y = 110;
    call.regs[0] = uint32_t(call.at_x), call.regs[1] = uint32_t(call.at_y);
    remaster::QueueImage(id, call);
    // (Kept at first sight too: main decodes the image right after the frame - remaster::DrawQueued - before it is shown.)
  }
  remaster::Item it;
  it.key = remaster::Key(caller, Reg(cpu::A0 + 6));
  it.x = float(RegW(cpu::D0)), it.y = float(RegW(cpu::D0 + 1));
  it.image = id;
  it.clip_top = 28, it.clip_bottom = 199, it.clip_left = 0, it.clip_right = 319;
  Put(it);
  remaster::Return(m);  // not drawn
}

// ---- Dots, lines and the ground (all drawn in play's frame routine, into the screen being drawn) ----

bool Capturing(const st::Machine& m) {
  g_m = &m;
  return !remaster::Decoding(m) && g_in_pass && (Reg(cpu::A0 + 1) & 0xFFFFFF) == g_screen;
}
uint32_t Caller(const st::Machine& m) { return Get32(m, Reg(cpu::A7)) & 0xFFFFFF; }

void AddDot(uint64_t key, int x, int y, int colour, int w, int snap = 1) {
  remaster::Item it;
  it.kind = remaster::kDot;
  it.key = key;
  it.x = float(x), it.y = float(y);
  it.colour = uint8_t(colour & 15);
  it.w = uint16_t(w), it.h = 1, it.snap = uint8_t(snap);
  it.clip_top = 0, it.clip_bottom = 199;
  Put(it);
}

// A pixel OR-ed into one plane at a screen address (mask: one bit): stars, particles. Its colour: the plane's bit
// OR-ed into what is under it now (IndexAt), as the game does.
void AddPlaneDot(uint64_t key, uint32_t addr, uint16_t mask) {
  uint32_t off = (addr & 0xFFFFFF) - g_screen;
  if (off >= 32000 || mask == 0) return;
  int bit = 15;
  while (!(mask & (1u << bit))) --bit;
  int x = int(off % 160 / 8) * 16 + (15 - bit), plane = int(off % 8 / 2), y = int(off / 160);
  AddDot(key, x, y, remaster::IndexAt(*g_m, g_screen, x, y) | 1 << plane, 1);
}

// $4A8A2 "cmp.w #$1C,d1; bpl": a pixel at d0 / d1 in colour d2 (sparks, explosions), lines 28..198.
void OnDot1(st::Machine& m) {
  int x = RegW(cpu::D0), y = RegW(cpu::D0 + 1);
  if (y < 28) return remaster::Return(m);
  if (!Capturing(m)) return cpu::Set(cpu::PC, kDot1 + 10);
  if (y < 199 && x >= 0 && x < 320) AddDot(remaster::Key(Caller(m), Reg(cpu::A0 + 6)), x, y, int(Reg(cpu::D0 + 2)), 1);
  remaster::Return(m);
}

// $4A952 "tst.w d0; bmi rts": a 2 x 1 dot (x rounded down to even) in colour d2, lines 28..199.
void OnDot2(st::Machine& m) {
  int x = RegW(cpu::D0), y = RegW(cpu::D0 + 1);
  if (x < 0) return remaster::Return(m);
  if (!Capturing(m)) return cpu::Set(cpu::PC, kDot2 + 4);
  if (x < 320 && y >= 28 && y < 200) AddDot(remaster::Key(Caller(m), Reg(cpu::A0 + 6)), x, y, int(Reg(cpu::D0 + 2)), 2, 2);
  remaster::Return(m);
}

// $19716 "movem.w d0-d6,-(a7)": one cell of $196D4's 3 x 3 grid of 2 x 1 dots (an enemy materialising: a3; the
// cells spread out and close in), colour d2 (0 too: black). The loop goes on at $197B4 (it pops these).
void OnCell(st::Machine& m) {
  uint32_t sp = Reg(cpu::A7) - 14;
  cpu::Set(cpu::Reg(cpu::A7), sp);
  for (int r = 0; r < 7; ++r) Put16(&m, (sp + r * 2) & 0xFFFFFF, int(Reg(cpu::D0 + r) & 0xFFFF));
  if (!Capturing(m)) return cpu::Set(cpu::PC, kCell + 4);
  int x = RegW(cpu::D0), y = RegW(cpu::D0 + 1);
  if (x >= 0 && x < 320 && y >= 28 && y < 200)
    AddDot(remaster::Key(kCell, Reg(cpu::A0 + 3)), x, y, int(Reg(cpu::D0 + 2)), 2, 2);
  cpu::Set(cpu::PC, kCellNext);
}

// Stars and the terrain outline: the restore list's pointer is written as always, the OR into the screen skipped.
// $13690 "move.l a3,(a6)" (then "or.w d1,(a3)"): stars from the table $31718 (plane 0).
void OnStarsA(st::Machine& m) {
  uint32_t a3 = Reg(cpu::A0 + 3), a6 = Reg(cpu::A0 + 6) & 0xFFFFFF;
  Put16(&m, a6, int(a3 >> 16)), Put16(&m, a6 + 2, int(a3 & 0xFFFF));
  if (!Capturing(m)) return cpu::Set(cpu::PC, kStarsA + 2);
  AddPlaneDot(remaster::Key(kStarsA, Reg(cpu::A0 + 4)), a3, uint16_t(Reg(cpu::D0 + 1)));
  cpu::Set(cpu::PC, kStarsA + 4);
}

// $1ABFC "move.l a5,(a0)+" (then "or.w d2,(a5)"): stars from the table $212CC (plane 3), a3 the star.
void OnStarsB(st::Machine& m) {
  uint32_t a5 = Reg(cpu::A0 + 5), a0 = Reg(cpu::A0);
  Put16(&m, a0 & 0xFFFFFF, int(a5 >> 16)), Put16(&m, (a0 + 2) & 0xFFFFFF, int(a5 & 0xFFFF));
  cpu::Set(cpu::Reg(cpu::A0), a0 + 4);
  if (!Capturing(m)) return cpu::Set(cpu::PC, kStarsB + 2);
  AddPlaneDot(remaster::Key(kStarsB, Reg(cpu::A0 + 3)), a5, uint16_t(Reg(cpu::D0 + 2)));
  cpu::Set(cpu::PC, kStarsB + 4);
}

// $1C5CA "move.l a5,(a6)" (then "or.w d1,(a5)"): particles from a list ($1C54A: a0 the particle, $20 bytes each;
// its plane from +8).
void OnParticle(st::Machine& m) {
  uint32_t a5 = Reg(cpu::A0 + 5), a6 = Reg(cpu::A0 + 6) & 0xFFFFFF;
  Put16(&m, a6, int(a5 >> 16)), Put16(&m, a6 + 2, int(a5 & 0xFFFF));
  if (!Capturing(m)) return cpu::Set(cpu::PC, kParticle + 2);
  AddPlaneDot(remaster::Key(kParticle, Reg(cpu::A0)), a5, uint16_t(Reg(cpu::D0 + 1)));
  cpu::Set(cpu::PC, kParticle + 4);
}

// $1AD5C "lea $3D318,a3": the planet, 32 px wide at x 256 (it doesn't scroll), drawn over the lasers and dots as
// whole words: a cleared row, then d0 + 1 rows from $3D318 (a0 at the second row). Captured in its place.
void OnPlanet(st::Machine& m) {
  if (!Capturing(m)) {
    cpu::Set(cpu::Reg(cpu::A0 + 3), kPlanetImage);
    return cpu::Set(cpu::PC, kPlanet + 6);
  }
  uint32_t off = (Reg(cpu::A0) & 0xFFFFFF) - g_screen;
  int rows = (Reg(cpu::D0) & 0xFFFF) + 1;
  if (off < 32000 && off >= 160 && rows <= 24) {
    std::vector<uint8_t> data(size_t(rows + 1) * 16);  // row 0: the cleared one
    std::memcpy(&data[16], m.Ram() + kPlanetImage, size_t(rows) * 16);
    uint64_t id = 1469598103934665603ull;
    for (uint8_t b : data) id = (id ^ b) * 1099511628211ull;
    id = (id & ((uint64_t(1) << 60) - 1)) | uint64_t(3) << 60;
    if (!remaster::HaveImage(id)) {
      remaster::Image im = remaster::DecodePlanes(data.data(), 2, rows + 1);
      im.opaque = true;
      remaster::SetImage(id, std::move(im));
    }
    remaster::Item it;
    it.key = remaster::Key(kPlanet, 0);
    it.x = float(off % 160 / 8 * 16), it.y = float(off / 160 - 1);
    it.image = id;
    it.clip_top = 0, it.clip_bottom = 199;
    Put(it);
    g_planet_image = id, g_planet_x = int(it.x), g_planet_y = int(it.y);
  }
  cpu::Set(cpu::PC, kPlanetDone);  // not drawn
}

// $1AE42 "move.l a4,(a5)+" (then "or.w d1,(a4)"): the terrain outline, 2 x 1 dots in plane 0 every 2 px along
// line 187 + its height table at the scroll $2BA32: matched in world coordinates (x + scroll), so it slides.
void OnTerrain(st::Machine& m) {
  uint32_t a4 = Reg(cpu::A0 + 4), a5 = Reg(cpu::A0 + 5);
  Put16(&m, a5 & 0xFFFFFF, int(a4 >> 16)), Put16(&m, (a5 + 2) & 0xFFFFFF, int(a4 & 0xFFFF));
  cpu::Set(cpu::Reg(cpu::A0 + 5), a5 + 4);
  if (!Capturing(m)) return cpu::Set(cpu::PC, kTerrain + 2);
  uint32_t off = (a4 & 0xFFFFFF) - g_screen;
  uint16_t mask = uint16_t(Reg(cpu::D0 + 1));
  if (off < 32000 && mask) {
    int bit = 15;
    while (!(mask & (1u << bit))) --bit;
    int x = int(off % 160 / 8) * 16 + (15 - bit), y = int(off / 160);
    remaster::Item it;
    it.kind = remaster::kDot;
    it.key = remaster::Key(kTerrain, 0);
    it.x = float(x), it.y = float(y);
    it.colour = 1, it.w = 2, it.h = 1;
    it.world = true, it.wx = float(x + (Get16(m, kScroll) & ~1)), it.wy = float(y);
    Put(it);
  }
  cpu::Set(cpu::PC, kTerrain + 4);
}

// $1C76C "cmp.w #$1C,d3; blt rts": a line along row d3 from x d0 to d1 (both kept within the screen; d0 = d1:
// nothing) OR-ed into plane d2: the lasers. A laser is several of these on one row in different planes, so they
// are OR-ed together here (g_rows) and handed over as runs of one colour at the end of the pass (FlushSpans).

void OnSpan(st::Machine& m) {
  int y = RegW(cpu::D0 + 3);
  if (y < 28) return remaster::Return(m);
  if (!Capturing(m)) return cpu::Set(cpu::PC, kSpan + 6);
  int x0 = std::clamp(RegW(cpu::D0), 0, 319), x1 = std::clamp(RegW(cpu::D0 + 1), 0, 319);
  if (y <= 199 && x0 != x1) {
    if (x0 > x1) std::swap(x0, x1);
    std::vector<uint8_t>* row = nullptr;
    for (auto& r : g_rows)
      if (r.first == y) row = &r.second;
    if (!row) g_rows.push_back({y, std::vector<uint8_t>(320)}), row = &g_rows.back().second;
    uint8_t bit = uint8_t(1 << (Reg(cpu::D0 + 2) & 3)) | kLaserBit;
    for (int x = x0; x <= x1; ++x) (*row)[size_t(x)] |= bit;
  }
  remaster::Return(m);
}

// A pixel OR-ed into the planes `bits` (with the lasers' rows: handed over by FlushSpans in their place).
void OrPixel(int x, int y, uint8_t bits) {
  if (x < 0 || x > 319 || y < 0 || y > 199) return;
  for (auto& r : g_rows)
    if (r.first == y) return void(r.second[size_t(x)] |= bits);
  g_rows.push_back({y, std::vector<uint8_t>(320)});
  g_rows.back().second[size_t(x)] |= bits;
}

// $1E14C "tst.w d0; bmi rts": a 16-px wide one-plane shape (a0: +2 rows - 1, +4 the rows' words) OR-ed into
// plane d2 with its left edge at d0 (0..315), top at d1 (28..193): the small things from $19BCA.
void OnMono(st::Machine& m) {
  int x = RegW(cpu::D0), y = RegW(cpu::D0 + 1);
  if (x < 0) return remaster::Return(m);
  if (!Capturing(m)) return cpu::Set(cpu::PC, kMono + 6);
  if (x < 0x13C && y >= 28 && y < 0xC2) {
    uint32_t a0 = Reg(cpu::A0) & 0xFFFFFF;
    int rows = Get16(m, a0 + 2) + 1;
    uint8_t bit = uint8_t(1 << (Reg(cpu::D0 + 2) & 3));
    for (int r = 0; r < rows && r < 64; ++r) {
      int w = Get16(m, a0 + 4 + r * 2) & 0xFFFF;
      for (int i = 0; i < 16; ++i)
        if (w & (0x8000 >> i)) OrPixel(x + i, y + r, bit);
    }
  }
  remaster::Return(m);
}

// $198F4 "tst.w d0; bmi rts": a pixel OR-ed into plane d2 at d0 / d1 (lines 28..199).
void OnPlanePixel(st::Machine& m) {
  int x = RegW(cpu::D0), y = RegW(cpu::D0 + 1);
  if (x < 0) return remaster::Return(m);
  if (!Capturing(m)) return cpu::Set(cpu::PC, kPlanePixel + 6);
  if (x < 320 && y >= 28 && y < 200) OrPixel(x, y, uint8_t(1 << (Reg(cpu::D0 + 2) & 3)));
  remaster::Return(m);
}

// Over the planet's own pixels, not OR-ed: its greys already have the lasers' plane bits, so in the original a
// shot crossing the moon vanishes as if behind it (owner 2026-11-07: they should go over it - a remaster liberty).
bool OnPlanetPixel(int x, int y) {
  if (!g_planet_image) return false;
  const remaster::Image* im = remaster::FindImage(g_planet_image);
  int px = x - g_planet_x - (im ? im->ox : 0), py = y - g_planet_y - (im ? im->oy : 0);
  if (!im || px < 0 || py < 0 || px >= im->w || py >= im->h) return false;
  uint8_t c = im->pixels[size_t(py) * size_t(im->w) + size_t(px)];
  return c != 0 && c != remaster::kBlack;
}

void FlushSpans() {
  if (g_rows.empty()) return;
  auto rows = std::move(g_rows);
  g_rows.clear();
  for (auto& [y, row] : rows) {  // OR-ed into what is under them (IndexAt), as the game does
    for (int x = 0; x < 320; ++x)
      if (uint8_t v = row[size_t(x)]) {
        bool own = (v & kLaserBit) && OnPlanetPixel(x, y);
        row[size_t(x)] = uint8_t((v & 15) | (own ? 0 : remaster::IndexAt(*g_m, g_screen, x, y)) | 16);
      }
    for (int x = 0; x < 320;) {
      int c = row[size_t(x)], e = x + 1;
      while (e < 320 && row[size_t(e)] == c) ++e;
      if (c) {  // (bit 4: drawn; colour 0 can't result from an OR of a plane bit anyway)
        remaster::Item it;
        it.kind = remaster::kDot;
        it.key = remaster::Key(kSpan, uint32_t(y) + 1);
        it.x = float(x), it.y = float(y);
        it.colour = uint8_t(c & 15);
        it.w = uint16_t(e - x), it.h = 1;
        it.clip_top = 0, it.clip_bottom = 199;
        remaster::Add(g_screen, it);
      }
      x = e;
    }
  }
}

// $1AF50 "lea $64594,a3": a ground band from the list *$1369E - d0 its top line, d1 its speed divisor, a0 ->
// (pattern, rows - 1). Each row is a 32-px pattern pre-shifted in 16 steps of 2 px ($100 bytes a row), the step
// ((scroll & $FFF) / d1) & 30 (pixels left), copied across the screen as whole words.
void OnBand(st::Machine& m) {
  if (!Capturing(m)) {
    cpu::Set(cpu::Reg(cpu::A0 + 3), kRows);
    return cpu::Set(cpu::PC, kBand + 6);
  }
  int top = RegW(cpu::D0), div = std::max(1, RegW(cpu::D0 + 1) & 0xFFFF);
  uint32_t desc = Reg(cpu::A0) & 0xFFFFFF, src = Get32(m, desc) & 0xFFFFFF;
  int rows = Get16(m, desc + 4) + 1;
  if (rows > 0 && rows <= 40 && src + uint32_t(rows) * 0x100 <= st::kRamSize) {
    uint64_t id = 1469598103934665603ull;
    for (int r = 0; r < rows; ++r)
      for (int i = 0; i < 16; ++i) id = (id ^ m.Ram()[src + r * 0x100 + i]) * 1099511628211ull;
    id = (id & ((uint64_t(1) << 60) - 1)) | uint64_t(1) << 61;
    if (!remaster::HaveImage(id)) {
      std::vector<uint8_t> data(size_t(rows) * 22 * 8);
      for (int r = 0; r < rows; ++r)
        for (int g = 0; g < 22; ++g)
          std::memcpy(&data[(size_t(r) * 22 + size_t(g)) * 8], m.Ram() + src + r * 0x100 + (g & 1) * 8, 8);
      remaster::Image im = remaster::DecodePlanes(data.data(), 22, rows);
      im.opaque = true;
      remaster::SetImage(id, std::move(im));
    }
    double q = double(Get16(m, kScroll) & 0xFFF) / div;
    int step = int(q) & 30;
    remaster::Item it;
    it.key = remaster::Key(kBand, uint32_t(top) + 1);
    it.x = -float(step), it.y = float(top);
    it.fine_x = -float(std::fmod(q, 32.0) - step);
    if (it.fine_x > 0) it.fine_x -= 32;  // (q wrapped below the step: keep it continuous)
    it.period = 32;
    it.image = id;
    it.clip_top = int16_t(top), it.clip_bottom = int16_t(top + rows - 1);
    Put(it);
  }
  remaster::Return(m);
}

void Install(st::Machine* m) {
  struct H {
    uint32_t at;
    std::vector<uint8_t> code;
    void (*fn)(st::Machine&);
  };
  static const H kHooks[] = {
      {kLoopCall, {0x4E, 0x93}, OnLoopCall},                                  // jsr (a3)
      {kLoopAfter, {0x4A, 0x79, 0x00, 0x01, 0xC5, 0x48, 0x67, 0x00}, OnLoopAfter},  // tst.w $1C548; beq.w
      {kCore, {0x24, 0x7C, 0x00, 0x04, 0xB8, 0x8C}, OnCore},                  // movea.l #$4B88C,a2
      {kSprite, {0x4A, 0x68, 0x00, 0x02, 0x66, 0x00}, OnSprite},              // tst.w 2(a0); bne.w
      {kDot1, {0xB2, 0x7C, 0x00, 0x1C, 0x6A, 0x00}, OnDot1},                  // cmp.w #$1C,d1; bpl.w
      {kDot2, {0x4A, 0x40, 0x6B, 0xFA}, OnDot2},                              // tst.w d0; bmi.b
      {kCell, {0x48, 0xA7, 0xFE, 0x00}, OnCell},                              // movem.w d0-d6,-(a7)
      {kStarsA, {0x2C, 0x8B, 0x83, 0x53}, OnStarsA},                          // move.l a3,(a6); or.w d1,(a3)
      {kStarsB, {0x20, 0xCD, 0x85, 0x55}, OnStarsB},                          // move.l a5,(a0)+; or.w d2,(a5)
      {kTerrain, {0x2A, 0xCC, 0x83, 0x54}, OnTerrain},                        // move.l a4,(a5)+; or.w d1,(a4)
      {kSpan, {0xB6, 0x7C, 0x00, 0x1C, 0x6D, 0xF8}, OnSpan},                  // cmp.w #$1C,d3; blt.b
      {kBand, {0x47, 0xF9, 0x00, 0x06, 0x45, 0x94}, OnBand},                  // lea $64594,a3
      {kParticle, {0x2C, 0x8D, 0x83, 0x55}, OnParticle},                      // move.l a5,(a6); or.w d1,(a5)
      {kPlanet, {0x47, 0xF9, 0x00, 0x03, 0xD3, 0x18}, OnPlanet},              // lea $3D318,a3
      {kMono, {0x4A, 0x40, 0x6B, 0x00}, OnMono},                              // tst.w d0; bmi.w
      {kPlanePixel, {0x4A, 0x40, 0x6B, 0x00}, OnPlanePixel},                  // tst.w d0; bmi.w
  };
  g_installed = true;
  for (const H& h : kHooks)
    if (std::memcmp(m->Ram() + h.at, h.code.data(), h.code.size()) != 0) {
      LOG("defender 2: remaster hooks not installed ($%06X differs)", h.at);
      return;
    }
  for (const H& h : kHooks) {
    Put16(m, h.at, 0x4AFC);
    m->AddHook(h.at, [fn = h.fn](st::Machine& running) { fn(running); });
  }
  g_capture = g_in_pass = false;
  remaster::Stop();
  struct Saved {
    bool capture, in_pass;
    uint32_t screen;
  };
  static Saved saved;
  remaster::SetAheadState([] { saved = {g_capture, g_in_pass, g_screen}; },
                          [] { g_capture = saved.capture, g_in_pass = saved.in_pass, g_screen = saved.screen; });
  LOG("defender 2: remaster hooks installed");
}

}  // namespace

void Start() { g_installed = g_capture = g_in_pass = false; }

void Update(st::Machine* m) {
  // From the logo or play on (DEFENDR2.EXE, loaded by the first program, is in place by then).
  if (!g_installed && DrawsEveryVbl(*m)) Install(m);
  if (!g_installed) return;
  remaster::DrawQueued(m, "defender 2");
  if (g_capture && !Captured(*m)) g_capture = g_in_pass = false, remaster::Stop();
}

}  // namespace remaster_d2
