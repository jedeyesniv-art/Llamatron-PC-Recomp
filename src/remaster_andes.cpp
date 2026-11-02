// Andes Attack: the remaster (remaster.h; REMASTER.md). The same Arc engine family as Photon Storm, but built
// differently: nothing matches byte for byte, the shape does.
//
// Play draws in the routine at $18DCC (the jump table $20296 on $202BA picks it): $18DD2 sets the draw mode
// $22AE8 = 0 and draws into the screen $2B3F2 (objects $1BD7C, then $1ADA2), $18E78 shows it (jsr $15490); after
// the VBL ($2B678) the restore pass (mode 2: the background copied back over last time's list at $18C54 + $32)
// and the screens ($2B3F2 / $2B3F6) and lists ($18C50 / $18C54) swap.
//   $21D6C the sprite routine (everything moving: the landscape's 16 x 16 tiles, the ship, its laser, enemies,
//     llamas, explosions): a0 = the sprite (word type 0..3 -> the table $21D5C; +2 width in 16-px groups, +4
//     height), d0 / d1 = top-left, a1 = the screen; mode $22AE8 (0 draw, 2 restore); clipped to lines
//     $202C0..$202C2 (32..199 in play) and groups $202BC..$202BE (1..18). Types 0 and 2 draw at even x. The
//     callers write the restore list entry (y, x, sprite) themselves before calling, so a draw we skip leaves the
//     list valid, and the restore of something never drawn copies background over background: harmless.
//   $1B50A world -> screen (sub.w d7,d1 first): d0 = world x (0..$7FF, wraps), d1 = world y; then $1B53A writes
//     the list entry and calls the sprite routine (return $1B556; the caller of $1B50A at sp + 44 there).
//   $195C0..$195EC: a HUD sprite drawn into both screens and the background ($22AE4): left alone.
// Not captured: the scanner (dots in the band at the top) and the HUD.
#include "remaster_andes.h"

#include <algorithm>
#include <cstring>

#include "cpu.h"
#include "log.h"
#include "remaster.h"

namespace remaster_andes {
namespace {

constexpr uint32_t kState = 0x12EEE;
constexpr int kPlay = 1, kPaused = 3;
constexpr uint32_t kBegin = 0x18DD2, kEnd = 0x18E78, kScreen = 0x2B3F2;
constexpr uint32_t kSprite = 0x21D6C, kSpriteGo = 0x21D72, kSpriteTable = 0x21D5C;
constexpr uint32_t kMode = 0x22AE8;
constexpr uint32_t kClipLeft = 0x202BC, kClipRight = 0x202BE, kClipTop = 0x202C0, kClipBottom = 0x202C2;
constexpr uint32_t kWorld = 0x1B50A, kFromWorld = 0x1B556;
constexpr uint32_t kHudFrom = 0x195C0, kHudTo = 0x195F0;
constexpr int kWorldWidth = 0x800;

bool g_installed = false;
bool g_capture = false;  // this frame's drawing goes to remaster
uint32_t g_screen = 0;
uint32_t g_world_sp = 0;  // the last world -> screen call (its stack slot)
int g_world_x = 0, g_world_y = 0;

int Get16(const st::Machine& m, uint32_t a) { return int16_t(m.Ram()[a] << 8 | m.Ram()[a + 1]); }
uint32_t Get32(const st::Machine& m, uint32_t a) {
  const uint8_t* p = m.Ram() + (a & 0xFFFFFF);
  return uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3];
}
void Put16(st::Machine* m, uint32_t a, int v) { m->Ram()[a] = uint8_t(v >> 8), m->Ram()[a + 1] = uint8_t(v); }
uint32_t Reg(int r) { return cpu::Get(cpu::Reg(r)); }
int RegW(int r) { return int16_t(Reg(r)); }

// $18DD2 "move.w #0,$22AE8": play's draw pass starts.
void OnBegin(st::Machine& m) {
  if (!remaster::Decoding(m)) {
    int state = Get16(m, kState);
    g_capture = remaster::Enabled() && state == kPlay;
    g_screen = Get32(m, kScreen) & 0xFFFFFF;
    if (g_capture) remaster::BeginFrame(g_screen);
    else if (state == kPaused && remaster::Enabled()) remaster::Hold();
    else remaster::Stop();
  }
  Put16(&m, kMode, 0);
  cpu::Set(cpu::PC, kBegin + 8);
}

// $18E78 "movea.l $2B3F2,a1" (the screen is shown next): the pass is complete.
void OnEnd(st::Machine& m) {
  if (!remaster::Decoding(m) && g_capture) remaster::EndFrame(g_screen);
  cpu::Set(cpu::Reg(cpu::A0 + 1), Get32(m, kScreen));
  cpu::Set(cpu::PC, kEnd + 6);
}

// $1B50A "sub.w d7,d1": the object's world position.
void OnWorld(st::Machine&) {
  g_world_sp = Reg(cpu::A7) & 0xFFFFFF;
  g_world_x = RegW(cpu::D0) & (kWorldWidth - 1), g_world_y = RegW(cpu::D0 + 1);
  uint32_t d1 = Reg(cpu::D0 + 1);
  cpu::Set(cpu::Reg(cpu::D0 + 1), (d1 & 0xFFFF0000u) | uint16_t(d1 - Reg(cpu::D0 + 7)));
  cpu::Set(cpu::PC, kWorld + 2);
}

// $21D6C "movea.l #$21D5C,a2".
void OnSprite(st::Machine& m) {
  auto original = [] {
    cpu::Set(cpu::Reg(cpu::A0 + 2), kSpriteTable);
    cpu::Set(cpu::PC, kSpriteGo);
  };
  if (remaster::Decoding(m) || !g_capture || Get16(m, kMode) != 0 || (Reg(cpu::A0 + 1) & 0xFFFFFF) != g_screen)
    return original();
  uint32_t a0 = Reg(cpu::A0) & 0xFFFFFF, sp = Reg(cpu::A7) & 0xFFFFFF, ret = Get32(m, sp) & 0xFFFFFF;
  if (ret >= kHudFrom && ret < kHudTo) return original();
  uint64_t id = a0;
  int type = Get16(m, a0);
  if (!remaster::HaveImage(id)) {
    remaster::SandboxCall call;
    call.routine = kSprite;
    for (int r = 0; r < 16; ++r) call.regs[r] = Reg(r);
    int h = Get16(m, a0 + 4);
    call.at_x = 128, call.at_y = h > 0 && h <= 180 ? (200 - h) / 2 : 80;
    call.regs[0] = uint32_t(call.at_x), call.regs[1] = uint32_t(call.at_y);
    call.pokes = {{kMode, 0}, {kClipTop, 0}, {kClipBottom, 199}, {kClipLeft, 0}, {kClipRight, 19}};
    remaster::QueueImage(id, call);
    return original();  // drawn by the game this once
  }
  remaster::Item it;
  uint32_t caller = ret;
  if (ret == kFromWorld) {
    caller = Get32(m, sp + 44) & 0xFFFFFF;
    if (sp + 44 == g_world_sp) it.world = true, it.wx = float(g_world_x), it.wy = float(g_world_y), it.wrap_x = kWorldWidth;
  }
  it.key = remaster::Key(caller, 0);
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
      {kBegin, {0x33, 0xFC, 0x00, 0x00, 0x00, 0x02, 0x2A, 0xE8}, OnBegin},  // move.w #0,$22AE8
      {kEnd, {0x22, 0x79, 0x00, 0x02, 0xB3, 0xF2}, OnEnd},                  // movea.l $2B3F2,a1
      {kWorld, {0x92, 0x47}, OnWorld},                                      // sub.w d7,d1
      {kSprite, {0x24, 0x7C, 0x00, 0x02, 0x1D, 0x5C}, OnSprite},            // movea.l #$21D5C,a2
  };
  for (const H& h : kHooks)
    if (std::memcmp(m->Ram() + h.at, h.code.data(), h.code.size()) != 0) {
      LOG("andes attack: remaster hooks not installed ($%06X differs)", h.at);
      g_installed = true;  // don't try again
      return;
    }
  for (const H& h : kHooks) {
    Put16(m, h.at, 0x4AFC);
    m->AddHook(h.at, [fn = h.fn](st::Machine& running) { fn(running); });
  }
  g_installed = true;
  g_capture = false;
  remaster::Stop();
  struct Saved {
    bool capture;
    uint32_t screen, world_sp;
    int world_x, world_y;
  };
  static Saved saved;
  remaster::SetAheadState(
      [] { saved = {g_capture, g_screen, g_world_sp, g_world_x, g_world_y}; },
      [] {
        g_capture = saved.capture, g_screen = saved.screen, g_world_sp = saved.world_sp;
        g_world_x = saved.world_x, g_world_y = saved.world_y;
      });
  LOG("andes attack: remaster hooks installed");
}

}  // namespace

void Start() {
  g_installed = g_capture = false;
  g_world_sp = 0;
}

void Update(st::Machine* m) {
  int state = Get16(*m, kState);
  // From play on (the program is in place by then).
  if (!g_installed && state == kPlay) Install(m);
  if (!g_installed) return;
  remaster::DrawQueued(m, "andes attack");
  if (state == kPaused && remaster::Enabled()) remaster::Hold();
  // Left play some other way than through the draw pass (game over, the high score table): the ST picture.
  if (g_capture && state != kPlay && state != kPaused) g_capture = false, remaster::Stop();
}

}  // namespace remaster_andes
