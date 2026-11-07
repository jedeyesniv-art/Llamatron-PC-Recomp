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
// Outside play (2026-11-06) the same draw call and sprite routine draw the title (VBL hook $551C, draw function
// $5668: the llama heads) and the continue screen (hook $C94, draw $D5A: "CONTINUE?" and its count over the last
// play picture, which the game leaves in the screens - so the last play pass's items are added to each frame).
// Captured too. Not the version
// screen / instruction scroller (hook $CEC; CPU-timed) nor the wave change's zoom ($AEC: boxes drawn by $5ACE
// straight into the screen shown, as fast as the CPU goes).
#include "remaster_llama.h"

#include <cstring>
#include <vector>

#include "cpu.h"
#include "game.h"
#include "log.h"
#include "remaster.h"

namespace remaster_llama {
namespace {

constexpr uint32_t kDrawCall = 0x5C10, kDrawDone = 0x5C12, kScreen = 0x52FB4, kDrawFn = 0x52FBE, kGameDraw = 0x2702;
constexpr uint32_t kSprite = 0x5E8A, kPlain = 0x5E90, kShaped = 0x5FC4;
constexpr uint32_t kVblHook = 0x52FC2, kNoHandler = 0x5664;
constexpr uint32_t kTitleHook = 0x551C, kTitleDraw = 0x5668, kOverHook = 0xC94, kOverDraw = 0xD5A;
// The pause screen ($1D32) with the Ozric tentacle toy drawing ($88BC: 20 sprites through $58A2). $CEC: the VBL
// that types text - under the continue screen's draw for its last 10 frames before the scroller, where the ST's
// screens lack the frozen arena the capture drew and nothing is drawn (its VBL writes GAME OVER into the screen
// itself): the screen shown gets the last play picture's items under it (Update), as during the continue screen.
constexpr uint32_t kPauseHook = 0x1D32, kTentacleDraw = 0x88BC, kTypeHook = 0xCEC;
// The wave change's zoom ($AEC, VBL handler $5CC0 meanwhile): rectangles of 2 x 2 dots, each through $5B08
// ("movem.w d0-d6,-(a7)"; then $59FA: the dot at d0 / d1, colour d2, in the screen a1 - the one shown).
constexpr uint32_t kZoomVbl = 0x5CC0, kZoomDot = 0x5B08;
// The version screen and instruction scroller ($7E6E, before the title): a line a drawn frame (2 VBLs), the draw
// function $7F20 scrolling lines 2..197 at x 16..303 ($4EE) and adding a row at the bottom; $7F52 the whole screen.
constexpr uint32_t kScrollText = 0x7F20, kScrollAll = 0x7F52;
constexpr uint32_t kWrapper = 0x58AA, kMarkFrom = 0x5870, kMarkTo = 0x58A2, kShotsCaller = 0x1D02;

bool g_installed = false;
bool g_capture = false;  // this frame's drawing goes to remaster
bool g_play_pass = false;  // the pass being captured is play's
std::vector<remaster::Item> g_pass_items, g_last_play;  // play's items: this pass, the last complete one
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

// The screens captured: play, the title, game over (the continue screen and its hand-over), the pause screen's
// tentacle (each with its own draw function).
bool OverScreen(st::Machine& m) {
  const uint32_t t = g_text, fn = Get32(m, t + kDrawFn), hook = Get32(m, t + kVblHook);
  return fn == t + kOverDraw && hook == t + kOverHook;
}
bool CapturedScreen(st::Machine& m) {
  const uint32_t t = g_text, fn = Get32(m, t + kDrawFn), hook = Get32(m, t + kVblHook);
  return (game::Playing(m) && fn == t + kGameDraw) || (hook == t + kTitleHook && fn == t + kTitleDraw) ||
         OverScreen(m) || (hook == t + kPauseHook && fn == t + kTentacleDraw);
}

// Between the draw call and its end: sprites drawn outside a pass (a VBL's - the continue screen's GAME OVER as it
// hands over) go into the screen as the game drew them.
bool g_in_pass = false;

// $5C10 "jsr (a3)": the screen's drawing starts.
void OnDrawCall(st::Machine& m) {
  const uint32_t t = g_text;
  if (!remaster::Decoding(m)) {
    g_in_pass = true;
    bool game_screen = Get32(m, t + kDrawFn) == t + kGameDraw;
    g_capture = remaster::Enabled() && CapturedScreen(m);
    g_screen = Get32(m, t + kScreen) & 0xFFFFFF;
    g_play_pass = g_capture && game_screen;
    g_pass_items.clear();
    if (g_capture) {
      remaster::BeginFrame(g_screen);
      // The continue screen: the last play picture under it, still.
      if (OverScreen(m))
        for (const remaster::Item& it : g_last_play) remaster::Add(g_screen, it);
    } else if ((game::Paused(m) || Frozen(m)) && game_screen && remaster::Enabled()) {
      remaster::Hold();
    } else {
      remaster::Stop();
    }
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
  if (!remaster::Decoding(m)) g_in_pass = false;
  if (!remaster::Decoding(m) && g_capture) {
    remaster::EndFrame(g_screen);
    if (g_play_pass) g_last_play.swap(g_pass_items);
  }
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
  if (remaster::Decoding(m) || !g_capture || !g_in_pass || d2 >= 0 || (Reg(cpu::A0 + 1) & 0xFFFFFF) != g_screen)
    return original();
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
    // (Kept at first sight too: main decodes the image right after the frame - remaster::DrawQueued - before it is shown.)
  }
  remaster::Item it;
  it.key = remaster::Key(caller, object);
  // The top-left (the routine centres the sprite: d0 - width / 2, d1 - height / 2). Compiled sprites (type 0: code
  // made by $7696 for 8 shifts, x & $E) land on even x.
  it.x = float(RegW(cpu::D0) - (Get16(m, a0 + 6) >> 1)), it.y = float(RegW(cpu::D0 + 1) - (Get16(m, a0 + 4) >> 1));
  it.snap = Get16(m, a0) == 0 ? 2 : 1;
  // Analog shots (game.cpp keeps their exact positions; the game moves them in whole pixels): shown where they
  // really are, so diagonals at any angle glide instead of stepping.
  if (caller == kShotsCaller) {
    float fx, fy;
    if (game::ShotFraction(m, object & 0xFFFFFF, &fx, &fy)) it.fine_x = fx, it.fine_y = fy;
  }
  it.image = id;
  it.clip_top = 0, it.clip_bottom = 198;
  if (g_play_pass) g_pass_items.push_back(it);
  remaster::Add(g_screen, it);
  remaster::Return(m);  // not drawn
}

// A zoom dot: each of its 4 pixels handed to remaster::Reveal (colour before, when in the frame), then the original
// instruction (the game draws it as always).
void OnZoomDot(st::Machine& m) {
  uint32_t sp = (Reg(cpu::A7) - 14) & 0xFFFFFF;
  if (!m.sandbox && Get32(m, kVblHook + g_text) == g_text + kZoomVbl) {
    int x = int16_t(Reg(cpu::D0)), y = int16_t(Reg(cpu::D0 + 1));
    uint32_t screen = Reg(cpu::A0 + 1) & 0xFFFFFF;
    float at = float(m.FramePhase());
    if (x >= 0 && x < 320 && y >= 0 && y < 200)
      for (int dy = 0; dy < 2; ++dy)
        for (int dx = 0; dx < 2; ++dx) remaster::Reveal(x + dx, y + dy, remaster::IndexAt(m, screen, x + dx, y + dy), at);
  }
  for (int r = 0; r < 7; ++r) Put16(&m, sp + uint32_t(r) * 2, int(Reg(cpu::D0 + r) & 0xFFFF));  // movem.w d0-d6,-(a7)
  cpu::Set(cpu::A7, sp);
  cpu::Set(cpu::PC, g_text + kZoomDot + 4);
  cpu::UseCycles(32);  // movem.w of 7 registers: 36 cycles, the hook's ILLEGAL 4 of them (the zoom is CPU-timed)
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
      {kZoomDot, {0x48, 0xA7, 0xFE, 0x00}, OnZoomDot},   // movem.w d0-d6,-(a7)
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
    bool capture, play_pass;
    uint32_t screen;
    std::vector<remaster::Item> pass_items, last_play;
  };
  static Saved saved;
  remaster::SetAheadState(
      [] {
        saved.capture = g_capture, saved.play_pass = g_play_pass, saved.screen = g_screen;
        saved.pass_items = g_pass_items, saved.last_play = g_last_play;
        game::SaveAheadState();
      },
      [] {
        g_capture = saved.capture, g_play_pass = saved.play_pass, g_screen = saved.screen;
        g_pass_items.swap(saved.pass_items), g_last_play.swap(saved.last_play);
        game::RestoreAheadState();
      });
  LOG("llamatron: remaster hooks installed");
}

}  // namespace

void Start() {  // (every game's start)
  remaster::ClearScroll();
  g_installed = g_capture = g_play_pass = g_in_pass = false;
  g_pass_items.clear(), g_last_play.clear();
}

void Update(st::Machine* m) {
  {  // the scroller (no hooks needed: the screen is the game's, only its motion is smoothed)
    const uint32_t t = m->TextBase(), fn = Get32(*m, t + kDrawFn);
    if (fn == t + kScrollText) remaster::SetScroll(16, 2, 303, 197);
    else if (fn == t + kScrollAll) remaster::SetScroll(0, 0, 319, 199);
    else remaster::ClearScroll();
  }
  // From the title or play on (the program has unpacked itself by then).
  if (!g_installed && (game::Playing(*m) || game::AtTitle(*m))) Install(m);
  if (!g_installed || !g_text) return;
  remaster::DrawQueued(m, "llamatron");
  const uint32_t t = g_text;
  const bool hand_over = Get32(*m, t + kVblHook) == t + kTypeHook && Get32(*m, t + kDrawFn) == t + kOverDraw;
  if (hand_over && remaster::Enabled() && !g_last_play.empty()) {  // the frozen arena under the ST's own picture
    const uint32_t screen = m->FrameBase() & 0xFFFFFF;
    remaster::BeginFrame(screen);
    for (const remaster::Item& it : g_last_play) remaster::Add(screen, it);
    remaster::EndFrame(screen);
    g_capture = true;
    return;
  }
  // Paused (not the tentacle: that's captured and moves) or frozen: the picture held.
  if (game::Paused(*m) && !game::Tentacle(*m) && remaster::Enabled()) remaster::Hold();
  else if (Frozen(*m)) remaster::Hold();
  // Left the captured screens some other way than through the draw call (wave changes...): the ST picture.
  else if (g_capture && !CapturedScreen(*m)) g_capture = false, remaster::Stop();
}

}  // namespace remaster_llama
