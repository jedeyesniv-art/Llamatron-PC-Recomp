// Llamatron: the remaster (remaster.h; REMASTER.md). Addresses are text offsets (runtime = TextBase() + offset).
//
// The main loop's frame ($5BFE): a1 = the screen to draw into ($52FB4), a2 = its restore list ($3B69C), then
// "jsr (a3)" at $5C10 calls the screen's draw function ($52FBE: $2702 the game, $88BC the Ozric tentacle); $5C12
// ends the list ($FE0C). After the VBL the other screen's list is restored by copying the background ($52FB0)
// back (an inline loop, not the sprite routine), and the screens swap.
// Everything moving goes through one routine, $5E8A: a0 = the sprite (+2 width in 16-px groups - 1, +4 height,
// +6 width), d0 / d1 = its middle, a1 = the screen, d2 = -1 a plain draw (>= 0: drawn row by row with offsets,
// $5FC4: d2 / d3 / d4 shape it: left to the game), clipped to the screen (lines 0..198). Callers:
//   $61FE (the object loop $61D6: 70 slots of $40 at $52FCA, a6 = the object; each slot's draw handler from the
//     table $3B59A ends up here),
//   $58A2 (a wrapper: 11 registers pushed, its caller at sp + 48) from $1CFE (the shots: 64 slots of 16 at
//     $54FD4, a3 = the slot), $27A8 / $27AE / $27C8 (the llama's effects) and $5882 / $588C / $5896 (a mark drawn
//     into both screens and the background: left alone),
//   $27E6 (the llama), $2734 / $2760 (extras over it).
// The restore list is written by the routine itself: a draw we skip adds no entry, so nothing is restored there.
#include "remaster_llama.h"

#include <cstring>

#include "cpu.h"
#include "game.h"
#include "log.h"
#include "remaster.h"

namespace remaster_llama {
namespace {

constexpr uint32_t kDrawCall = 0x5C10, kDrawDone = 0x5C12, kScreen = 0x52FB4, kDrawFn = 0x52FBE, kGameDraw = 0x2702;
constexpr uint32_t kSprite = 0x5E8A, kPlain = 0x5E90, kShaped = 0x5FC4;
constexpr uint32_t kVblHook = 0x52FC2, kNoHandler = 0x5664;
constexpr uint32_t kWrapper = 0x58AA, kMarkFrom = 0x5870, kMarkTo = 0x58A2, kShotsCaller = 0x1D02;

bool g_installed = false;
bool g_capture = false;  // this frame's drawing goes to remaster
uint32_t g_screen = 0, g_text = 0;

uint32_t Get32(const st::Machine& m, uint32_t a) {
  const uint8_t* p = m.Ram() + (a & 0xFFFFFF);
  return uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3];
}
void Put16(st::Machine* m, uint32_t a, int v) { m->Ram()[a] = uint8_t(v >> 8), m->Ram()[a + 1] = uint8_t(v); }
int Get16(const st::Machine& m, uint32_t a) { return int16_t(m.Ram()[a] << 8 | m.Ram()[a + 1]); }
uint32_t Reg(int r) { return cpu::Get(cpu::Reg(r)); }
int RegW(int r) { return int16_t(Reg(r)); }

// No VBL handler ($52FC2 = $5664, an rts): the llama's death (set at $7B0) and other pauses in the action. The game
// draws nothing new: the last picture stays (the colours still cycle).
bool Frozen(st::Machine& m) { return Get32(m, g_text + kVblHook) == g_text + kNoHandler; }

// $5C10 "jsr (a3)": the screen's drawing starts.
void OnDrawCall(st::Machine& m) {
  const uint32_t t = g_text;
  if (!remaster::Decoding(m)) {
    bool game_screen = Get32(m, t + kDrawFn) == t + kGameDraw;
    g_capture = remaster::Enabled() && game::Playing(m) && game_screen;
    g_screen = Get32(m, t + kScreen) & 0xFFFFFF;
    if (g_capture) remaster::BeginFrame(g_screen);
    else if ((game::Paused(m) || Frozen(m)) && game_screen && remaster::Enabled()) remaster::Hold();
    else remaster::Stop();
  }
  uint32_t sp = Reg(cpu::A7) - 4;
  uint8_t* p = m.Ram() + (sp & 0xFFFFFF);
  uint32_t ret = t + kDrawDone;
  p[0] = uint8_t(ret >> 24), p[1] = uint8_t(ret >> 16), p[2] = uint8_t(ret >> 8), p[3] = uint8_t(ret);
  cpu::Set(cpu::A7, sp);
  cpu::Set(cpu::PC, Reg(cpu::A0 + 3));
}

// $5C12 "move.w #$FE0C,(a2)": the pass is complete.
void OnDrawDone(st::Machine& m) {
  if (!remaster::Decoding(m) && g_capture) remaster::EndFrame(g_screen);
  Put16(&m, Reg(cpu::A0 + 2) & 0xFFFFFF, 0xFE0C);
  cpu::Set(cpu::PC, g_text + kDrawDone + 4);
}

// $5E8A "tst.w d2; bpl $5FC4".
void OnSprite(st::Machine& m) {
  const uint32_t t = g_text;
  int d2 = RegW(cpu::D0 + 2);
  auto original = [t, d2] { cpu::Set(cpu::PC, t + (d2 >= 0 ? kShaped : kPlain)); };
  // Shaped draws (d2 >= 0: the llama warping in, enemies materialising: a new shape every frame, briefly) are
  // left to the game.
  if (remaster::Decoding(m) || !g_capture || d2 >= 0 || (Reg(cpu::A0 + 1) & 0xFFFFFF) != g_screen) return original();
  uint32_t a0 = Reg(cpu::A0) & 0xFFFFFF, sp = Reg(cpu::A7) & 0xFFFFFF, ret = (Get32(m, sp) & 0xFFFFFF) - t;
  uint32_t caller = ret, object = Reg(cpu::A0 + 6);
  if (ret == kWrapper) {
    caller = (Get32(m, sp + 48) & 0xFFFFFF) - t;
    if (caller >= kMarkFrom && caller < kMarkTo) return original();  // into the background too
    if (caller == kShotsCaller) object = Reg(cpu::A0 + 3);
  }
  uint64_t id = a0;
  if (!remaster::HaveImage(id)) {
    remaster::SandboxCall call;
    call.routine = t + kSprite;
    for (int r = 0; r < 16; ++r) call.regs[r] = Reg(r);
    // Drawn with its top-left at (an even) x 160 - width / 2, y 100 - height / 2: the image is kept relative to
    // its top-left, as the routine puts it.
    int half_w = Get16(m, a0 + 6) >> 1, half_h = Get16(m, a0 + 4) >> 1;
    call.regs[0] = uint32_t(160 + (half_w & 1)), call.regs[1] = 100;
    call.at_x = 160 + (half_w & 1) - half_w, call.at_y = 100 - half_h;
    remaster::QueueImage(id, call);
    if (!m.ahead) return original();  // drawn by the game this once
  // In the run-ahead copy the item is kept anyway (its image is decoded right after the copy's run): what that copy
  // draws is never shown, so letting it draw there would lose the sprite for a frame (a flicker).
  }
  remaster::Item it;
  it.key = remaster::Key(caller, object);
  // The top-left (the routine centres the sprite: d0 - width / 2, d1 - height / 2). Compiled sprites (type 0: code
  // made by $7696 for 8 shifts, x & $E) land on even x.
  it.x = float(RegW(cpu::D0) - (Get16(m, a0 + 6) >> 1)), it.y = float(RegW(cpu::D0 + 1) - (Get16(m, a0 + 4) >> 1));
  it.snap = Get16(m, a0) == 0 ? 2 : 1;
  it.image = id;
  it.clip_top = 0, it.clip_bottom = 198;
  remaster::Add(g_screen, it);
  remaster::Return(m);  // not drawn
}

void Install(st::Machine* m) {
  const uint32_t t = m->TextBase();
  struct H {
    uint32_t at;
    std::vector<uint8_t> code;
    void (*fn)(st::Machine&);
  };
  const H hooks[] = {
      {kDrawCall, {0x4E, 0x93}, OnDrawCall},             // jsr (a3)
      {kDrawDone, {0x34, 0xBC, 0xFE, 0x0C}, OnDrawDone},  // move.w #$FE0C,(a2)
      {kSprite, {0x4A, 0x42, 0x6A, 0x00}, OnSprite},     // tst.w d2; bpl.w
  };
  g_installed = true;
  for (const H& h : hooks)
    if (std::memcmp(m->Ram() + t + h.at, h.code.data(), h.code.size()) != 0) {
      LOG("llamatron: remaster hooks not installed ($%X differs)", h.at);
      return;
    }
  g_text = t;
  for (const H& h : hooks) {
    Put16(m, t + h.at, 0x4AFC);
    m->AddHook(t + h.at, [fn = h.fn](st::Machine& running) { fn(running); });
  }
  g_capture = false;
  remaster::Stop();
  struct Saved {
    bool capture;
    uint32_t screen;
  };
  static Saved saved;
  remaster::SetAheadState([] { saved = {g_capture, g_screen}, game::SaveAheadState(); },
                          [] { g_capture = saved.capture, g_screen = saved.screen, game::RestoreAheadState(); });
  LOG("llamatron: remaster hooks installed");
}

}  // namespace

void Start() { g_installed = g_capture = false; }

void Update(st::Machine* m) {
  // From play on (the program has unpacked itself by then).
  if (!g_installed && game::Playing(*m)) Install(m);
  if (!g_installed || !g_text) return;
  remaster::DrawQueued(m, "llamatron");
  if (game::Paused(*m) && remaster::Enabled()) remaster::Hold();
  // Left play some other way than through the draw call (wave transitions, the title...): the ST picture.
  else if (Frozen(*m)) remaster::Hold();
  else if (g_capture && !game::Playing(*m)) g_capture = false, remaster::Stop();
}

}  // namespace remaster_llama
