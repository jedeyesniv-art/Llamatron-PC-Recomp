// Revenge of the Mutant Camels and Hardcore: the remaster (remaster.h; REMASTER.md). Runtime addresses of the
// Camels' 1 MB STE build (REVSTE1M.TOS); the TT Megamix is the same code elsewhere (Addrs kTt), Hardcore the
// same library (kHc: the sprites and plotter, plus its compiled sprites and one-plane plotters).
//
// The main loop ($1A842) draws a frame with "jsr (a3)" at $1A854 (a3 = *$4036A: play's draw routine $11450) into
// the screen $3DF54 (double-buffered with $3DF58; a2 = the restore list $3DF62), writes the list's end marker at
// $1A856, shows the screen at the next VBL, then restores the other screen from the background $3DF5E (copies:
// skipping a draw is harmless) and swaps.
// Sprites: $1B0DA dispatches on the sprite's type word (header: +0 type, +2 16-px groups - 1, +4 height, +6
// width, +8 the data) to: type 2 $1A580 (pre-shifted, x even), type 1 $1A3B6 (stretched: d2 / d3 / d4; d3 = 0 ->
// type 3), type 3 $1B0FA (any x; +2 > 0 -> the wide path $1AF14). All centre the image on d0 / d1 and clip to
// lines 16..199 (the HUD is above) and the screen's columns. $1A4D6 turns a type 3 sprite into type 2 (its
// pre-shifted copy) at run time: the type is read at each draw.
// Also captured: the stars and sparks (plotter $1A772: 2 x 1 dots), the falling blocks ($11D18, drawn inline: hooked
// per block, sub-pixel y from the frame counter), the ground's six stripes ($1247A: 16.16 positions, drawn as wide
// repeating lines), the buildings on the ground ($1A956 from $125CA: each row clears a 16-px strip, so their black
// covers the stars) and the level message ($10BEA). Opaque things (blocks, stripes, message, buildings' strips)
// carry their black (remaster::kBlack). Left to the game: the HUD, the pause screen (it writes text over the
// picture), other screens.
// Outside play (2026-11-06; Camels only): the two menus (hook $1084A drawing with $10EE8: "what is thy bidding",
// hook $10C4A with $10E1E: the game mode) and the end-of-wave bonus (hook $112A2 with $115AA: the play scene and
// the bonus text) draw through the same main loop and routines: captured too. The menus draw only their starfield
// per frame ($10EE8: stars in the table $37BF6, 10 bytes each - screen offset, plane bits, a countdown and its
// reload, the pixel's mask -, OR-ed in; the VBL ($10EAA) moves each one a line down when its countdown runs
// out): hooked, each star a dot sliding by its countdown. Not the change-overs between them
// (hook $1AC3C: CPU-timed) nor the title (a still picture).
// Between levels (2026-11-07, owner: still 50 a second): the warp (hook $11018, draw $110C4: "warp to next oasis!",
// the play scene rushing by; ends when $390E2 passes $15E, counted in the VBL) and the oasis (hooks $122AC ->
// $12328 -> $12362 (the password: fire or 500 frames) -> $12396 -> $123DA, draw $123EC: the camel walks in and off
// over a still picture) - VBL-timed, captured, on the faster CPU (games.cpp play_more). TT: warp $1102C / $110D8,
// oasis $122D4 $12350 $1238A $123BE $12402 / $12414.
// A new sprite image is drawn by the game the first time (decoded in the sandbox between frames): for that frame
// it sits under the captured items (seen: an animated score popup under the camel for a frame).
#include "remaster_rmc.h"

#include <cstring>
#include <vector>

#include "cpu.h"
#include "log.h"
#include "remaster.h"

namespace remaster_rmc {
namespace {

struct Addrs {
  uint32_t hook, play, pause;   // the VBL hook pointer, play's / pause's VBL routines (installed in play)
  uint32_t draw_ptr, play_draw;  // the main loop's draw routine pointer, play's draw routine
  uint32_t loop_call;           // the main loop's "jsr (a3)"; the end marker's "move.w #$FE0C,(a2)" follows
  uint32_t screen;              // the screen being drawn
  uint32_t type1, type2, type3, type3_wide;
  uint32_t objects_from, objects_to;  // the game's object tables (a6 there = an object)
  uint32_t plotter, posts;
  uint32_t block_draw, block_next, block_images, block_scroll, frame_count;
  uint32_t stripes, stripe_patterns, stripe_pos;
  uint32_t banner, banner_y, banner_image, banner_none;  // the level message
  // (0 above: not in this game - Hardcore has only the sprites and the plotter)
  int clip_top2, clip_top3;  // the first line types 2 and 1 / 3 draw on
  const char* name;
  uint32_t type6, plot2x2, plot2x1;  // Hardcore: compiled sprites (type 6), 2 x 2 / 2 x 1 one-plane plotters
  uint32_t menu1, menu1_draw, menu2, menu2_draw, bonus, bonus_draw;  // other screens' hooks and draw routines
  uint32_t menu_stars;  // the menus' starfield ("lea table,a6")
  uint32_t warp, warp_draw;  // between levels: "warp to next oasis!" (the play scene rushing by)
  uint32_t oasis[5], oasis_draw;  // then the oasis: the camel walks in, the password, it walks off (five hooks)
};
const Addrs kSte = {0x3DD3E, 0x111DE, 0x1056C, 0x4036A, 0x11450, 0x1A854, 0x3DF54,
                    0x1A3B6, 0x1A580, 0x1B0FA, 0x1AF14, 0x37000, 0x3DF54,
                    0x1A772, 0x1A956,
                    0x11D50, 0x11D78, 0x27C4C, 0x3D5C0, 0x3D5BE,
                    0x1247A, 0x1B700, 0x39384,
                    0x10BEA, 0x1B286, 0x3CB1A, 0x10BCE, 16, 16, "mutant camels", 0, 0, 0,
                    0x1084A, 0x10EE8, 0x10C4A, 0x10E1E, 0x112A2, 0x115AA, 0x10EE8,
                    0x11018, 0x110C4, {0x122AC, 0x12328, 0x12362, 0x12396, 0x123DA}, 0x123EC};
// The TT Megamix (REVTT.TOS): found from the STE build's code bytes (32 bullets, twice the objects: the tables
// after the first are further on).
const Addrs kTt = {0x3E0CE, 0x11206, 0x10580, 0x426FA, 0x11478, 0x1A88C, 0x3E2E4,
                   0x1A3EE, 0x1A5B8, 0x1B138, 0x1AF52, 0x37000, 0x3E2E4,
                   0x1A7AA, 0x1A98E,
                   0x11D78, 0x11DA0, 0x27C8A, 0x3D950, 0x3D94E,
                   0x124A2, 0x1B73E, 0x39686,
                   0x10BFE, 0x1B2C4, 0x3CEAA, 0x10BE2, 16, 16, "camels tt", 0, 0, 0,
                   0x1085E, 0x10EFC, 0x10C5E, 0x10E32, 0x112CA, 0x115D2, 0x10EFC,
                   0x1102C, 0x110D8, {0x122D4, 0x12350, 0x1238A, 0x123BE, 0x12402}, 0x12414};
// Hardcore (HARDCORE.PRG, Yak 1992): the same library - main loop, dispatcher $171E2 and its types, the plotter -
// found with Tools/mapaddr.py from the Camels' STE dump. Play's draw routine $10974 (pause $11242). Type 2 clips
// from line 1 here (the playfield's frame is drawn as part of the background).
const Addrs kHc = {0x338BA, 0x10C82, 0x11242, 0x37EE6, 0x10974, 0x165C6, 0x33AD0,
                   0x1612A, 0x162F4, 0x1720E, 0x16EEE, 0x10000, 0x33AD0,
                   0x164E4, 0,
                   0, 0, 0, 0, 0,
                   0, 0, 0,
                   0, 0, 0, 0, 1, 16, "hardcore", 0x173B0, 0x1710A, 0x170A8};
// The 512K build (REVENGE.TOS, D-Bug 28) isn't covered: the launcher runs the STE build for that disk id, and a
// scripted run stays on its shareware screen, so its addresses couldn't be checked in play.
Addrs A = kSte;
bool g_supported = false;
bool g_installed = false;
bool g_capture = false;  // this frame's drawing goes to remaster
uint32_t g_screen = 0;

uint32_t Get32(const st::Machine& m, uint32_t a) {
  const uint8_t* p = m.Ram() + (a & 0xFFFFFF);
  return uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3];
}
int Get16(const st::Machine& m, uint32_t a) { return int16_t(m.Ram()[a & 0xFFFFFF] << 8 | m.Ram()[(a + 1) & 0xFFFFFF]); }
void Put16(st::Machine* m, uint32_t a, int v) { m->Ram()[a] = uint8_t(v >> 8), m->Ram()[a + 1] = uint8_t(v); }
uint32_t Reg(int r) { return cpu::Get(cpu::Reg(r)); }
int RegW(int r) { return int16_t(Reg(r)); }

// The main loop draws play's frame in play (and the attract demo). Paused, it still draws it (the falling blocks
// still fall with the frame counter) and the pause code writes text over it: not captured then, the game draws it
// all (nothing else moves).
bool Playing(const st::Machine& m) { return Get32(m, A.hook) == A.play && Get32(m, A.draw_ptr) == A.play_draw; }

// The frames captured: play's, and (the Camels) the menus' and the bonus screen's.
bool Captured(const st::Machine& m) {
  if (Playing(m)) return true;
  uint32_t hook = Get32(m, A.hook), draw = Get32(m, A.draw_ptr);
  if (!A.menu1) return false;
  if ((hook == A.menu1 && draw == A.menu1_draw) || (hook == A.menu2 && draw == A.menu2_draw) ||
      (hook == A.bonus && draw == A.bonus_draw) || (hook == A.warp && draw == A.warp_draw))
    return true;
  if (draw == A.oasis_draw)
    for (uint32_t h : A.oasis)
      if (hook == h) return true;
  return false;
}

// "jsr (a3)": a frame is drawn.
void OnLoopCall(st::Machine& m) {
  if (!remaster::Decoding(m)) {
    g_capture = remaster::Enabled() && Captured(m);
    g_screen = Get32(m, A.screen) & 0xFFFFFF;
    if (g_capture) remaster::BeginFrame(g_screen);
    else remaster::Stop();
  }
  uint32_t sp = Reg(cpu::A7) - 4;
  cpu::Set(cpu::Reg(cpu::A7), sp);
  m.Ram()[sp & 0xFFFFFF] = 0, m.Ram()[(sp + 1) & 0xFFFFFF] = uint8_t((A.loop_call + 2) >> 16);
  Put16(&m, (sp + 2) & 0xFFFFFF, int((A.loop_call + 2) & 0xFFFF));
  cpu::Set(cpu::PC, Reg(cpu::A0 + 3));
}

// "move.w #$FE0C,(a2)": the frame is complete.
void OnLoopEnd(st::Machine& m) {
  if (!remaster::Decoding(m) && g_capture) remaster::EndFrame(g_screen);
  Put16(&m, Reg(cpu::A0 + 2) & 0xFFFFFF, 0xFE0C);
  cpu::Set(cpu::PC, A.loop_call + 6);
}

// A sprite handler's entry: captured (true: return) or not (false: the caller does the replaced instruction).
// x_left: the item's x is the top-left the routine computes (type 2: snapped to even from there).
bool Capture(st::Machine& m, uint32_t routine, uint64_t id, int type) {
  if (remaster::Decoding(m) || !g_capture || (Reg(cpu::A0 + 1) & 0xFFFFFF) != g_screen) return false;
  uint32_t a0 = Reg(cpu::A0) & 0xFFFFFF, sp = Reg(cpu::A7) & 0xFFFFFF;
  uint32_t caller = Get32(m, sp) & 0xFFFFFF, object = Reg(cpu::A0 + 6) & 0xFFFFFF;
  // a6 is the object for the bullets and the object tables; some callers leave a scratch value in it: those
  // key by the caller alone (the matcher pairs them by position).
  if (object < A.objects_from || object >= A.objects_to) object = 0;
  int half_w = Get16(m, a0 + 6) >> 1;
  // Types 2, 3 and 6 centre the image by half its width: placed (and decoded) by that top-left, so an image means
  // the same whatever the type - a sprite can be converted from type 3 to 2 (Camels) or 6 (Hardcore) between draws.
  // (Type 1, stretched, centres by its own rule: its image ids are separate.)
  bool left = type == 2 || type == 3 || type == 6;
  int x = RegW(cpu::D0), y = RegW(cpu::D0 + 1);
  // Placed in the sandbox at (160, 108) - for type 2 its top-left, on an even x.
  constexpr int kAtX = 160, kAtY = 108;
  // Type 6 runs one of 8 compiled routines by the left edge's x & 14: each is decoded at its own shift (they don't
  // all draw alike: one decoded at shift 0 came out a group off at others).
  int shift = type == 6 ? (x - half_w) & 14 : 0;
  if (type == 6) id |= uint64_t(shift + 1) << 48;
  if (!remaster::HaveImage(id)) {
    remaster::SandboxCall call;
    call.routine = routine;
    for (int r = 0; r < 16; ++r) call.regs[r] = Reg(r);
    call.at_x = kAtX + shift, call.at_y = kAtY;
    call.regs[0] = uint32_t(left ? call.at_x + half_w : call.at_x), call.regs[1] = uint32_t(kAtY);
    remaster::QueueImage(id, call);
    // (Kept at first sight too: main decodes the image right after the frame - remaster::DrawQueued - before it is shown.)
  }
  remaster::Item it;
  it.key = remaster::Key(caller, object);
  it.x = float(left ? x - half_w : x), it.y = float(y);
  it.image = id;
  it.snap = type == 2 || type == 6 ? 2 : 1;
  it.clip_top = int16_t(type == 2 ? A.clip_top2 : type == 6 ? 0 : A.clip_top3), it.clip_bottom = 199;
  it.clip_left = 0, it.clip_right = 319;
  remaster::Add(g_screen, it);
  remaster::Return(m);  // not drawn
  return true;
}

// $1A580 "move.w 4(a0),d2".
void OnType2(st::Machine& m) {
  uint32_t a0 = Reg(cpu::A0) & 0xFFFFFF;
  if (Capture(m, A.type2, a0, 2)) return;
  cpu::Set(cpu::Reg(cpu::D0 + 2), (Reg(cpu::D0 + 2) & 0xFFFF0000) | uint16_t(Get16(m, a0 + 4)));
  cpu::Set(cpu::PC, A.type2 + 4);
}

// $1A3B6 "tst.w d3; beq $1B0FA": stretched by d3 (0: type 3).
void OnType1(st::Machine& m) {
  uint32_t a0 = Reg(cpu::A0) & 0xFFFFFF;
  int d3 = RegW(cpu::D0 + 3);
  uint64_t id = d3 == 0 ? a0
                         : a0 | uint64_t(1) << 63 | uint64_t(uint8_t(RegW(cpu::D0 + 2))) << 32 |
                               uint64_t(uint8_t(d3)) << 40 | uint64_t(uint8_t(RegW(cpu::D0 + 4))) << 48;
  if (Capture(m, A.type1, id, d3 == 0 ? 3 : 1)) return;
  cpu::Set(cpu::PC, d3 == 0 ? A.type3 : A.type1 + 6);
}

// $1B0FA "tst.w 2(a0); bne $1AF14".
void OnType3(st::Machine& m) {
  uint32_t a0 = Reg(cpu::A0) & 0xFFFFFF;
  if (Capture(m, A.type3, a0, 3)) return;
  cpu::Set(cpu::PC, Get16(m, a0 + 2) != 0 ? A.type3_wide : A.type3 + 8);
}

bool CapturingInto(const st::Machine& m) {
  return !remaster::Decoding(m) && g_capture && (Reg(cpu::A0 + 1) & 0xFFFFFF) == g_screen;
}

uint32_t ObjectKey() {
  uint32_t a6 = Reg(cpu::A0 + 6) & 0xFFFFFF;
  return a6 >= A.objects_from && a6 < A.objects_to ? a6 : 0;
}

// $1A772 "tst.w d0; bmi rts": a 2 x 1 dot (stars, sparks) at d0 (rounded down to even) / d1 in colour d2.
void OnPlotter(st::Machine& m) {
  int x = RegW(cpu::D0);
  if (x < 0) return remaster::Return(m);
  if (!CapturingInto(m)) return cpu::Set(cpu::PC, A.plotter + 6);
  if (x < 320) {
    remaster::Item it;
    it.kind = remaster::kDot;
    it.key = remaster::Key(Get32(m, Reg(cpu::A7)) & 0xFFFFFF, ObjectKey());
    it.x = float(x), it.y = float(RegW(cpu::D0 + 1));
    it.colour = uint8_t(Reg(cpu::D0 + 2) & 15);
    it.w = 2, it.h = 1, it.snap = 2;
    it.clip_top = 0, it.clip_bottom = 199;
    remaster::Add(g_screen, it);
  }
  remaster::Return(m);
}

// $1A956 "asl.w #2,d1": the buildings on the ground (from $125CA), drawn upwards from d1, d0 = x - 16, d2 = how
// much of it.
void OnPosts(st::Machine& m) {
  if (CapturingInto(m)) {
    uint32_t a0 = Reg(cpu::A0) & 0xFFFFFF;
    uint64_t id = a0 | uint64_t(uint16_t(RegW(cpu::D0 + 2))) << 32 | uint64_t(1) << 61;
    int x = RegW(cpu::D0), y = RegW(cpu::D0 + 1);
    if (!remaster::HaveImage(id)) {
      remaster::SandboxCall call;
      call.routine = A.posts;
      for (int r = 0; r < 16; ++r) call.regs[r] = Reg(r);
      call.at_x = 160, call.at_y = y;
      call.regs[0] = 160;
      call.opaque_black = true;  // each row clears a 16-px strip, then draws: black there covers the stars
      remaster::QueueImage(id, call);  // decoded right after the frame (main: remaster::DrawQueued)
    }
    {
      remaster::Item it;
      it.key = remaster::Key(Get32(m, Reg(cpu::A7)) & 0xFFFFFF, 0);
      it.x = float(x), it.y = float(y);
      it.image = id;
      it.clip_top = 16, it.clip_bottom = 199;
      remaster::Add(g_screen, it);
      return remaster::Return(m);
    }
  }
  cpu::Set(cpu::Reg(cpu::D0 + 1), (Reg(cpu::D0 + 1) & 0xFFFF0000) | uint16_t(RegW(cpu::D0 + 1) << 2));
  cpu::Set(cpu::PC, A.posts + 2);
}

// $11D50 "move.w #0,(a2)+" in $11D18: one falling block (16 x 16, column d0 bytes in, a3 its table entry: +0 the
// image, +2 when it started; y = (scroll - start) / 4 + 16: a pixel every other frame) is drawn.
void OnBlock(st::Machine& m) {
  if (CapturingInto(m)) {
    uint32_t a3 = Reg(cpu::A0 + 3) & 0xFFFFFF;
    int start = Get16(m, a3 + 2), scroll = Get16(m, A.block_scroll), count = Get16(m, A.frame_count);
    if (count != scroll) count = scroll + 1;  // the frame's own count (not one a VBL during the drawing moved on)
    int y = (int16_t(scroll - start) >> 2) + 16;
    uint32_t data = A.block_images + (Reg(cpu::D0 + 1) & 0xFFFF);
    if (!remaster::HaveImage(data) && data + 128 <= st::kRamSize) {
      remaster::Image im = remaster::DecodePlanes(m.Ram() + data, 1, 16);
      im.opaque = true;  // copied as whole words: its black covers what is under it
      remaster::SetImage(data, std::move(im));
    }
    remaster::Item it;
    it.key = remaster::Key(A.block_draw, a3);
    it.x = float((Reg(cpu::D0) & 0xFFFF) * 2), it.y = float(y);
    it.fine_y = float(int16_t(count - start)) / 4.0f + 16 - float(y);
    it.image = data;
    it.clip_top = 0, it.clip_bottom = 199;
    remaster::Add(g_screen, it);
    return cpu::Set(cpu::PC, A.block_next);  // not drawn (nor put in the restore list)
  }
  uint32_t a2 = Reg(cpu::A0 + 2);
  Put16(&m, a2 & 0xFFFFFF, 0);
  cpu::Set(cpu::Reg(cpu::A0 + 2), a2 + 2);
  cpu::Set(cpu::PC, A.block_draw + 4);
}

// $1247A "lea $1B700,a3": the ground's six stripes (lines 178, 180, 183, 187, 192, 198, each a 16-px pattern
// across the screen, rotated left by its position's whole part: 16.16 longs that the VBL moves at their speeds).
void OnStripes(st::Machine& m) {
  if (!CapturingInto(m)) {
    cpu::Set(cpu::Reg(cpu::A0 + 3), A.stripe_patterns);
    return cpu::Set(cpu::PC, A.stripes + 6);
  }
  int row = 178;
  for (int b = 0; b < 6; ++b) {
    const uint8_t* pat = m.Ram() + A.stripe_patterns + b * 8;
    uint64_t id = 1469598103934665603ull;
    for (int i = 0; i < 8; ++i) id = (id ^ pat[i]) * 1099511628211ull;
    id = (id & ((uint64_t(1) << 60) - 1)) | uint64_t(1) << 62;
    if (!remaster::HaveImage(id)) {
      uint8_t line[21 * 8];
      for (int g = 0; g < 21; ++g) std::memcpy(line + g * 8, pat, 8);
      remaster::Image im = remaster::DecodePlanes(line, 21, 1);
      im.opaque = true;
      remaster::SetImage(id, std::move(im));
    }
    uint32_t pos = Get32(m, A.stripe_pos + b * 4);
    remaster::Item it;
    it.key = remaster::Key(A.stripes, uint32_t(b + 1));
    it.x = -float(pos >> 16 & 15), it.y = float(row);
    it.fine_x = -float(pos & 0xFFFF) / 65536.0f;
    it.period = 16;
    it.image = id;
    it.clip_top = int16_t(row), it.clip_bottom = int16_t(row);
    remaster::Add(g_screen, it);
    row += 2 + b;
  }
  remaster::Return(m);
}

// $10BEA "move.w $1B286,d0; beq": the level's message, a band 16 lines high across the screen (pre-drawn at
// $3CB1A, copied as whole words) with its top at $1B286 (0: none), sliding in under the HUD.
void OnBanner(st::Machine& m) {
  int y = Get16(m, A.banner_y);
  cpu::Set(cpu::Reg(cpu::D0), (Reg(cpu::D0) & 0xFFFF0000) | uint16_t(y));
  if (y == 0) return cpu::Set(cpu::PC, A.banner_none);
  if (!CapturingInto(m)) return cpu::Set(cpu::PC, A.banner + 8);
  const uint8_t* data = m.Ram() + A.banner_image;
  uint64_t id = 1469598103934665603ull;
  for (int i = 0; i < 20 * 8 * 16; ++i) id = (id ^ data[i]) * 1099511628211ull;
  id = (id & ((uint64_t(1) << 60) - 1)) | uint64_t(1) << 60;
  if (!remaster::HaveImage(id)) {
    remaster::Image im = remaster::DecodePlanes(data, 20, 16);
    im.opaque = true;
    remaster::SetImage(id, std::move(im));
  }
  remaster::Item it;
  it.key = remaster::Key(A.banner, 0);
  it.x = 0, it.y = float(y);
  it.image = id;
  it.clip_top = 16, it.clip_bottom = 199;
  remaster::Add(g_screen, it);
  remaster::Return(m);
}

// Hardcore $173B0 "move.w 4(a0),d2": type 6, a compiled sprite (8 pre-shifted routines: x even), centred. Type 3
// sprites are compiled into type 6 at run time ($173F4): same image, the type read at each draw.
void OnType6(st::Machine& m) {
  uint32_t a0 = Reg(cpu::A0) & 0xFFFFFF;
  cpu::Set(cpu::Reg(cpu::D0 + 2), (Reg(cpu::D0 + 2) & 0xFFFF0000) | uint16_t(Get16(m, a0 + 4)));
  cpu::Set(cpu::PC, A.type6 + 4);
}

// Hardcore $1710A / $170A8 "tst.w d0; bmi rts": a 2 x 2 / 2 x 1 block (x rounded down to even; up to x 276 and
// y 198 / 199) set in one plane (d2: its byte offset): stars, sparks. Its colour: that plane OR-ed into what is
// under it now (remaster::IndexAt).
void PlotOr(st::Machine& m, uint32_t entry, int rows) {
  int x = RegW(cpu::D0), y = RegW(cpu::D0 + 1);
  if (x < 0) return remaster::Return(m);
  if (!CapturingInto(m)) return cpu::Set(cpu::PC, entry + 6);
  x &= ~1;
  if (y >= 0 && y <= 0xC8 - rows && x <= 0x114) {
    int bit = 1 << ((Reg(cpu::D0 + 2) & 7) >> 1);
    uint64_t key = remaster::Key(Get32(m, Reg(cpu::A7)) & 0xFFFFFF, ObjectKey());
    for (int r = 0; r < rows; ++r)
      for (int c = 0; c < 2; ++c) {
        remaster::Item it;
        it.kind = remaster::kDot;
        it.key = key;
        it.x = float(x + c), it.y = float(y + r);
        it.colour = uint8_t((remaster::IndexAt(m, g_screen, x + c, y + r) | bit) & 15);
        it.clip_top = 0, it.clip_bottom = 199;
        remaster::Add(g_screen, it);
      }
  }
  remaster::Return(m);
}
void OnPlot2x2(st::Machine& m) { PlotOr(m, A.plot2x2, 2); }
void OnPlot2x1(st::Machine& m) { PlotOr(m, A.plot2x1, 1); }

// The menus' starfield: "lea table,a6; move.w count,d7" then a dbra loop over the stars.
void OnMenuStars(st::Machine& m) {
  const uint32_t table = Get32(m, A.menu_stars + 2) & 0xFFFFFF, count = Get32(m, A.menu_stars + 8) & 0xFFFFFF;
  if (remaster::Decoding(m) || !g_capture || (Reg(cpu::A0 + 1) & 0xFFFFFF) != g_screen) {
    cpu::Set(cpu::Reg(cpu::A0 + 6), table);
    cpu::Set(cpu::PC, A.menu_stars + 6);
    return;
  }
  int n = Get16(m, count) + 1;
  for (int i = 0; i < n && i < 1000; ++i) {
    uint32_t s = table + uint32_t(i) * 10, off = Get32(m, s);
    int bits = Get16(m, s + 4), mask = Get16(m, s + 8) & 0xFFFF;
    int down = int8_t(m.Ram()[s + 6]), reload = int8_t(m.Ram()[s + 7]);
    if (off >= 32000 || mask == 0) continue;
    int bit = 15;
    while (!(mask & (1 << bit))) --bit;
    int x = int(off % 160 / 8) * 16 + (15 - bit), y = int(off / 160);
    remaster::Item it;
    it.kind = remaster::kDot;
    it.key = remaster::Key(A.menu_stars, uint32_t(i) + 1);
    it.x = float(x), it.y = float(y);
    it.colour = uint8_t((remaster::IndexAt(m, g_screen, x, y) | bits) & 15);
    if (reload >= 0 && down <= reload) it.fine_y = float(reload - down) / float(reload + 1);  // towards the next line
    it.clip_top = 0, it.clip_bottom = 199;
    remaster::Add(g_screen, it);
  }
  remaster::Return(m);  // not drawn (and no restore entries: nothing to restore)
}

void Install(st::Machine* m) {
  struct H {
    uint32_t at;
    std::vector<uint8_t> code;
    void (*fn)(st::Machine&);
  };
  const H hooks[] = {
      {A.loop_call, {0x4E, 0x93}, OnLoopCall},              // jsr (a3)
      {A.loop_call + 2, {0x34, 0xBC, 0xFE, 0x0C}, OnLoopEnd},  // move.w #$FE0C,(a2)
      {A.type2, {0x34, 0x28, 0x00, 0x04}, OnType2},         // move.w 4(a0),d2
      {A.type1, {0x4A, 0x43, 0x67, 0x00}, OnType1},         // tst.w d3; beq.w
      {A.type3, {0x4A, 0x68, 0x00, 0x02, 0x66, 0x00}, OnType3},  // tst.w 2(a0); bne.w
      {A.plotter, {0x4A, 0x40, 0x6B, 0x00}, OnPlotter},     // tst.w d0; bmi.w
      {A.posts, {0xE5, 0x41, 0x47, 0xF9}, OnPosts},         // asl.w #2,d1; lea
      {A.block_draw, {0x34, 0xFC, 0x00, 0x00}, OnBlock},    // move.w #0,(a2)+
      {A.stripes, {0x47, 0xF9, uint8_t(A.stripe_patterns >> 24), uint8_t(A.stripe_patterns >> 16),
                   uint8_t(A.stripe_patterns >> 8), uint8_t(A.stripe_patterns)}, OnStripes},  // lea patterns,a3
      {A.type6, {0x34, 0x28, 0x00, 0x04}, OnType6},         // move.w 4(a0),d2
      {A.plot2x2, {0x4A, 0x40, 0x6B, 0x00}, OnPlot2x2},     // tst.w d0; bmi.w
      {A.plot2x1, {0x4A, 0x40, 0x6B, 0x00}, OnPlot2x1},     // tst.w d0; bmi.w
      {A.banner, {0x30, 0x39, uint8_t(A.banner_y >> 24), uint8_t(A.banner_y >> 16), uint8_t(A.banner_y >> 8),
                  uint8_t(A.banner_y), 0x67}, OnBanner},  // move.w $1B286,d0; beq.b
      {A.menu_stars, {0x4D, 0xF9}, OnMenuStars},  // lea table,a6
  };
  g_installed = true;
  for (const H& h : hooks)
    if (h.at >= 0x10000 && std::memcmp(m->Ram() + h.at, h.code.data(), h.code.size()) != 0) {
      LOG("%s: remaster hooks not installed ($%06X differs)", A.name, h.at);
      return;
    }
  for (const H& h : hooks) {
    if (h.at < 0x10000) continue;  // not in this game
    Put16(m, h.at, 0x4AFC);
    m->AddHook(h.at, [fn = h.fn](st::Machine& running) { fn(running); });
  }
  g_capture = false;
  remaster::Stop();
  struct Saved {
    bool capture;
    uint32_t screen;
  };
  static Saved saved;
  remaster::SetAheadState([] { saved = {g_capture, g_screen}; },
                          [] { g_capture = saved.capture, g_screen = saved.screen; });
  LOG("%s: remaster hooks installed", A.name);
}

}  // namespace

void Start(const std::string& program) {
  g_supported = program == "REVSTE1M.TOS" || program == "REVTT.TOS" || program == "HARDCORE.PRG";
  A = program == "REVTT.TOS" ? kTt : program == "HARDCORE.PRG" ? kHc : kSte;
  g_installed = g_capture = false;
}

void Update(st::Machine* m) {
  if (!g_supported) return;
  if (!g_installed && (Get32(*m, A.hook) == A.play || (A.menu1 && Get32(*m, A.hook) == A.menu1))) Install(m);
  if (!g_installed) return;
  remaster::DrawQueued(m, A.name);
  if (g_capture && !Captured(*m)) g_capture = false, remaster::Stop();
}

}  // namespace remaster_rmc
