#include "remaster.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "cpu.h"
#include "crt.h"
#include "log.h"
#include "settings.h"

namespace remaster {
namespace {

constexpr int kScale = 4;  // the composed picture: 1280 x 800
constexpr float kTeleport = 40;  // moved further than this between two frames: drawn where it is, not slid

bool g_enabled = true;
bool g_renderer = false;
// What the game drew: the real machine's, and the run-ahead copy's (g_cap points at the one running).
struct Capture {
  bool capturing = false, hold = false;
  std::unordered_map<uint32_t, std::vector<Item>> building;  // per screen: what the game drew into it
  std::unordered_map<uint32_t, std::unordered_map<uint64_t, int>> counts;  // per screen: keys so far
  std::vector<Item> latest;  // the last complete draw pass (EndFrame)
  bool have_latest = false;
};
Capture g_real, g_fork;
Capture* g_cap = &g_real;
std::vector<Item> g_prev, g_cur;  // the previous frame shown and this one
std::vector<std::pair<uint64_t, SandboxCall>> g_queue;  // sprite images to draw in the sandbox
std::vector<int> g_match;  // per item of g_cur: the item of g_prev it was (-1: new / jumped)
uint32_t g_cur_screen = 0;
bool g_have = false;
int g_ahead = 2;  // remaster_ahead
std::function<void()> g_save, g_restore;  // the game side's C++ state around a run-ahead
bool g_probe = false;
int g_shown = 0;  // frames shown (probe log)
uint64_t g_message_until = 0;
char g_message[64] = "";

std::unordered_map<uint64_t, Image> g_images;
// Per image: a texture per set of colours it was made with (only the colours its image uses count), so an image
// seen in a few palettes (rasters, flashes) isn't rebuilt every frame.
constexpr size_t kMaxVariants = 256;
std::unordered_map<uint64_t, std::unordered_map<uint64_t, SDL_Texture*>> g_textures;
std::unordered_map<uint64_t, uint16_t> g_used;  // per image: the colours it uses (bit per index)
SDL_Texture* g_target = nullptr;
SDL_Texture* g_over = nullptr;  // the ST picture with colour 0 see-through (items behind it)

uint32_t Argb(uint16_t c, bool ste) {
  auto ch = [&](unsigned v) {
    if (ste) return (((v & 7) << 1) | ((v >> 3) & 1)) * 17;
    v &= 7;
    return (v << 5) | (v << 2) | (v >> 1);
  };
  return 0xFF000000u | ch((c >> 8) & 15) << 16 | ch((c >> 4) & 15) << 8 | ch(c & 15);
}

void DropTextures(std::unordered_map<uint64_t, SDL_Texture*>& variants) {
  for (auto& [sig, t] : variants) SDL_DestroyTexture(t);
  variants.clear();
}
void DropTextures() {
  for (auto& [id, v] : g_textures) DropTextures(v);
  g_textures.clear();
}

// The image in the colours `pal` (ARGB): made once per set of the colours it uses.
int g_count_decoded = 0, g_count_textures = 0;
SDL_Texture* TextureFor(SDL_Renderer* r, uint64_t id, const uint32_t* pal) {
  auto im = g_images.find(id);
  if (im == g_images.end() || im->second.w <= 0 || im->second.h <= 0) return nullptr;
  const Image& image = im->second;
  uint16_t used = g_used[id];
  uint64_t sig = 1469598103934665603ull;
  for (int i = 0; i < 16; ++i)
    if (used >> i & 1) sig = (sig ^ pal[i]) * 1099511628211ull;
  auto& variants = g_textures[id];
  if (auto t = variants.find(sig); t != variants.end()) return t->second;
  if (variants.size() >= kMaxVariants) DropTextures(variants);
  std::vector<uint32_t> px(size_t(image.w) * size_t(image.h));
  for (size_t i = 0; i < px.size(); ++i) px[i] = image.pixels[i] ? pal[image.pixels[i] & 15] : 0;
  ++g_count_textures;
  SDL_Texture* t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, image.w, image.h);
  if (!t) return nullptr;
  SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
  SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST);
  SDL_UpdateTexture(t, nullptr, px.data(), image.w * 4);
  variants[sig] = t;
  return t;
}

// The frame's colours per line (ARGB), and which colour indices change between lines (rasters).
struct LineColours {
  uint32_t pal[200][16];
  uint16_t varying = 0;
  const uint32_t* At(int y) const { return pal[std::clamp(y, 0, 199)]; }
};
bool g_colour0_on = false;
uint32_t g_colour0 = 0;
void MakeLineColours(const uint16_t* line_palettes, bool ste, LineColours* out) {
  out->varying = 0;
  for (int y = 0; y < 200; ++y)
    for (int i = 0; i < 16; ++i) {
      uint16_t c = line_palettes[y * 16 + i];
      out->pal[y][i] = Argb(c, ste);
      if (c != line_palettes[i]) out->varying |= uint16_t(1u << i);
    }
  if (g_colour0_on) {
    for (int y = 0; y < 200; ++y) out->pal[y][0] = g_colour0;
    out->varying &= uint16_t(~1u);
  }
}

float LerpAngle(float a, float b, float t) {
  float d = std::remainder(b - a, 6.28318530718f);
  return a + d * t;
}


constexpr uint64_t kCountBits = 0xFF00000000000000ull;  // Add's occurrence count in the key

// Each item of g_cur takes the nearest item of g_prev in its key group (greedy, the closest pairs first; an image
// change costs a little, the same occurrence wins ties). Further than kTeleport on the screen: no match (a jump).
void Match() {
  g_match.assign(g_cur.size(), -1);
  std::unordered_map<uint64_t, std::vector<int>> groups;
  for (int j = 0; j < int(g_prev.size()); ++j) groups[(g_prev[j].key & ~kCountBits) ^ g_prev[j].kind].push_back(j);
  std::unordered_map<uint64_t, std::vector<int>> cur;
  for (int i = 0; i < int(g_cur.size()); ++i) cur[(g_cur[i].key & ~kCountBits) ^ g_cur[i].kind].push_back(i);
  struct Pair {
    float cost;
    int i, j;
  };
  std::vector<Pair> pairs;
  std::vector<bool> used(g_prev.size());
  for (auto& [k, is] : cur) {
    auto g = groups.find(k);
    if (g == groups.end()) continue;
    pairs.clear();
    for (int i : is)
      for (int j : g->second) {
        const Item &a = g_cur[i], &b = g_prev[j];
        if (std::fabs(a.x - b.x) > kTeleport || std::fabs(a.y - b.y) > kTeleport) continue;
        float dx, dy;
        if (a.world && b.world) {
          dx = a.wx - b.wx, dy = a.wy - b.wy;
          if (a.wrap_x) dx = std::remainder(dx, float(a.wrap_x));
        } else {
          dx = a.x - b.x, dy = a.y - b.y;
        }
        float cost = std::fabs(dx) + std::fabs(dy) + (a.image != b.image ? 4.0f : 0.0f) + (a.key != b.key ? 0.01f : 0.0f);
        pairs.push_back({cost, i, j});
      }
    if (is.size() == 1 && g->second.size() == 1) {  // the common case
      if (!pairs.empty()) g_match[pairs[0].i] = pairs[0].j;
      continue;
    }
    std::sort(pairs.begin(), pairs.end(), [](const Pair& a, const Pair& b) { return a.cost < b.cost; });
    for (const Pair& p : pairs)
      if (g_match[p.i] < 0 && !used[p.j]) g_match[p.i] = p.j, used[p.j] = true;
  }
}
}  // namespace

bool Enabled() { return g_enabled && g_renderer; }
void SetRenderer(bool have) { g_renderer = have; }
void LoadSettings() {
  g_enabled = settings::Get("remaster", 1) != 0;
  g_ahead = std::clamp(int(settings::Get("remaster_ahead", 2)), 0, 3);
}
void ShowMessage() {
  if (!g_enabled) std::snprintf(g_message, sizeof(g_message), "REMASTER OFF (F8)");
  else std::snprintf(g_message, sizeof(g_message), "REMASTER ON   RUN-AHEAD %d (F7)", g_ahead);
  g_message_until = SDL_GetTicks() + 2500;
  LOG("remaster: %s, run-ahead %d", g_enabled ? "on" : "off", g_ahead);
}
void Toggle() {
  g_enabled = !g_enabled;
  settings::Set("remaster", g_enabled ? 1 : 0);
  settings::Save();
  ShowMessage();
}
void CycleAhead() {
  g_ahead = (g_ahead + 1) % 4;
  settings::Set("remaster_ahead", g_ahead);
  settings::Save();
  ShowMessage();
}
int Ahead() { return g_ahead; }
void SetAhead(int frames) { g_ahead = std::clamp(frames, 0, 3); }
const char* Message() { return SDL_GetTicks() < g_message_until ? g_message : nullptr; }
void SetProbe(bool on) { g_probe = on; }
void SetAheadState(std::function<void()> save, std::function<void()> restore) {
  g_save = std::move(save), g_restore = std::move(restore);
}

int g_trace_pass = -1, g_passes = 0;
uint32_t g_trace_screen = 0;
std::vector<std::pair<uint32_t, int>> g_writers;
void WriteWatch(uint32_t a, uint32_t pc) {
  if (a < g_trace_screen || a >= g_trace_screen + 32000) return;
  for (auto& w : g_writers)
    if (w.first == (pc & ~15u)) return void(++w.second);
  g_writers.push_back({pc & ~15u, 1});
}
void TraceWriters(int pass) { g_trace_pass = pass; }
void TakeCounts(int* decoded, int* textures) {
  *decoded = g_count_decoded, *textures = g_count_textures;
  g_count_decoded = g_count_textures = 0;
}

void BeginFrame(uint32_t screen) {
  if (g_cap == &g_real && g_trace_pass >= 0) {
    int n = g_passes++;
    if (n == g_trace_pass || n == g_trace_pass + 1) g_trace_screen = screen, g_writers.clear(), cpu::g_write_watch = WriteWatch;
  }
  g_cap->capturing = true;
  g_cap->hold = false;
  g_cap->building[screen].clear();
  g_cap->counts[screen].clear();
}

void EndFrame(uint32_t screen) {
  if (cpu::g_write_watch == WriteWatch && g_cap == &g_real) {
    cpu::g_write_watch = nullptr;
    std::string s;
    char buf[32];
    for (auto& w : g_writers) std::snprintf(buf, sizeof(buf), " %d@%06X", w.second, w.first), s += buf;
    LOG("remaster: pass %d, writes into the screen (count@code):%s", g_passes - 1, s.c_str());
  }
  auto b = g_cap->building.find(screen);
  if (b == g_cap->building.end()) return;
  g_cap->latest = b->second;
  g_cap->have_latest = true;
}

void Add(uint32_t screen, Item item) {
  int n = g_cap->counts[screen][item.key]++;
  item.key ^= uint64_t(n) << 56;
  g_cap->building[screen].push_back(item);
}

int IndexAt(const st::Machine& m, uint32_t screen, int x, int y) {
  if (x < 0 || x > 319 || y < 0 || y > 199) return 0;
  if (auto b = g_cap->building.find(screen); b != g_cap->building.end())
    for (auto it = b->second.rbegin(); it != b->second.rend(); ++it) {
      if (x < std::max<int>(0, it->clip_left) || x > it->clip_right || y < it->clip_top || y > it->clip_bottom) continue;
      int ix = int(std::lround(it->x)) & -int(it->snap), iy = int(std::lround(it->y));
      if (it->kind == kDot) {
        if (x >= ix && x < ix + it->w && y >= iy && y < iy + it->h) return it->colour & 15;
        continue;
      }
      auto im = g_images.find(it->image);
      if (im == g_images.end()) continue;
      int px = x - ix - im->second.ox, py = y - iy - im->second.oy;
      if (px < 0 || py < 0 || px >= im->second.w || py >= im->second.h) continue;
      if (uint8_t c = im->second.pixels[size_t(py) * size_t(im->second.w) + size_t(px)]) return c & 15;
    }
  const uint8_t* p = m.Ram() + ((screen + uint32_t(y) * 160 + uint32_t(x / 16) * 8) & 0xFFFFFF);
  int s = 15 - x % 16, c = 0;
  for (int k = 0; k < 4; ++k) c |= (((p[k * 2] << 8 | p[k * 2 + 1]) >> s) & 1) << k;
  return c;
}

bool HaveImage(uint64_t id) { return g_images.count(id) != 0; }
void SetImage(uint64_t id, Image image) {
  if (image.opaque)
    for (uint8_t& c : image.pixels)
      if (c == 0) c = kBlack;
  uint16_t used = 0;  // bit 0: colour 0 drawn (kBlack)
  for (uint8_t c : image.pixels)
    if (c) used |= uint16_t(1u << (c & 15));
  g_used[id] = used;
  g_images[id] = std::move(image);
  if (auto t = g_textures.find(id); t != g_textures.end()) DropTextures(t->second), g_textures.erase(t);
}

void Hold() {
  if (g_cap->capturing) g_cap->hold = true;
}

void Stop() {
  *g_cap = Capture{};
  if (g_cap == &g_real) g_queue.clear();
  if (g_cap == &g_real) g_have = false, g_prev.clear(), g_cur.clear(), g_match.clear(), g_fork = Capture{};
}

void RunAhead(st::Machine* m) {
  g_fork.have_latest = false;
  int frames = g_ahead - 1;
  if (frames <= 0 || !Enabled() || !g_real.capturing || g_real.hold) return;
  static std::unique_ptr<st::Machine> fork;
  if (!fork) fork = std::make_unique<st::Machine>(*m);
  else *fork = *m;
  fork->sandbox = fork->ahead = true;
  if (g_save) g_save();
  std::vector<uint8_t> context = cpu::SaveContext();
  cpu::SetMachine(fork.get());
  g_fork = Capture{};
  g_fork.capturing = true;
  g_cap = &g_fork;
  for (int i = 0; i < frames; ++i)
    if (!fork->RunFrame()) break;
  g_cap = &g_real;
  cpu::SetMachine(m);
  cpu::RestoreContext(context);
  if (g_restore) g_restore();
  if (!g_fork.capturing || g_fork.hold) g_fork.have_latest = false;  // it left play (paused, died...)
  // Images the copy met first (its items are kept): decoded now, from its state, so this frame shows them.
  DrawQueued(fork.get(), "run-ahead");
  cpu::SetMachine(m);  // (the sandbox puts the CPU back on the machine it was given: the copy)
}

Image DecodePlanes(const uint8_t* data, int groups, int rows) {
  Image im;
  im.w = groups * 16, im.h = rows;
  im.pixels.resize(size_t(im.w) * size_t(rows));
  for (int y = 0; y < rows; ++y)
    for (int g = 0; g < groups; ++g) {
      const uint8_t* p = data + (y * groups + g) * 8;
      uint16_t w[4];
      for (int k = 0; k < 4; ++k) w[k] = uint16_t(p[k * 2] << 8 | p[k * 2 + 1]);
      for (int x = 0; x < 16; ++x) {
        int s = 15 - x, idx = 0;
        for (int k = 0; k < 4; ++k) idx |= ((w[k] >> s) & 1) << k;
        im.pixels[size_t(y) * im.w + g * 16 + x] = uint8_t(idx);
      }
    }
  return im;
}

namespace {
// One run of the call onto a screen filled with `fill` (0 or $FF): the screen afterwards (false: it didn't return).
bool RunInSandbox(st::Machine* m, const SandboxCall& call, uint8_t fill, Image* all) {
  uint32_t screen = call.regs[9] & 0xFFFFFE;
  uint32_t sp = (call.regs[15] & 0xFFFFFE) - 512;
  auto fork = std::make_unique<st::Machine>(*m);
  fork->sandbox = true;
  fork->ahead = false;  // (from a run-ahead copy: the hooks must only draw here)
  uint8_t* ram = fork->Ram();
  std::memset(ram + screen, fill, 32000);
  for (auto [a, v] : call.pokes) ram[a] = uint8_t(v >> 8), ram[a + 1] = uint8_t(v);
  // Its return lands on an ILLEGAL of ours just past the screen: the end.
  const uint32_t magic = screen + 32000 + 16;
  ram[magic] = 0x4A, ram[magic + 1] = 0xFC;
  bool done = false;
  fork->AddHook(magic, [&done, magic](st::Machine&) {
    done = true;
    cpu::Set(cpu::PC, magic);
    cpu::EndSlice();
  });
  for (int k = 0; k < 4; ++k) ram[sp + k] = uint8_t(magic >> (24 - 8 * k));
  std::vector<uint8_t> context = cpu::SaveContext();
  cpu::SetMachine(fork.get());
  cpu::Set(cpu::SR, 0x2700);
  for (int r = 0; r < 15; ++r) cpu::Set(cpu::Reg(r), call.regs[r]);
  cpu::Set(cpu::A7, sp);
  cpu::Set(cpu::PC, call.routine);
  for (int i = 0; i < 20 && !done; ++i) cpu::Execute(100000);
  cpu::SetMachine(m);
  cpu::RestoreContext(context);
  if (!done) {
    LOG("remaster: sandbox call of $%06X didn't return", call.routine);
    return false;
  }
  *all = DecodePlanes(ram + screen, 20, 200);
  return true;
}
}  // namespace

bool DrawInSandbox(st::Machine* m, const SandboxCall& call, Image* out) {
  uint32_t screen = call.regs[9] & 0xFFFFFE;
  uint32_t sp = (call.regs[15] & 0xFFFFFE) - 512;
  if (screen + 32000 + 64 > st::kRamSize || sp < 0x1000) return false;
  Image all;
  if (!RunInSandbox(m, call, 0, &all)) return false;
  if (call.opaque_black) {  // again onto colour 15: what is the same both times was drawn (black too)
    Image over15;
    if (!RunInSandbox(m, call, 0xFF, &over15)) return false;
    for (size_t i = 0; i < all.pixels.size(); ++i)
      all.pixels[i] = all.pixels[i] != over15.pixels[i] ? 0 : all.pixels[i] ? all.pixels[i] : kBlack;
  }
  int x0 = 320, y0 = 200, x1 = -1, y1 = -1;
  for (int y = 0; y < 200; ++y)
    for (int x = 0; x < 320; ++x)
      if (all.pixels[size_t(y) * 320 + x]) x0 = std::min(x0, x), x1 = std::max(x1, x), y0 = std::min(y0, y), y1 = std::max(y1, y);
  Image im;
  if (x1 >= 0) {
    im.w = x1 - x0 + 1, im.h = y1 - y0 + 1, im.ox = x0 - call.at_x, im.oy = y0 - call.at_y;
    im.pixels.resize(size_t(im.w) * size_t(im.h));
    for (int y = 0; y < im.h; ++y)
      std::memcpy(&im.pixels[size_t(y) * im.w], &all.pixels[size_t(y + y0) * 320 + x0], size_t(im.w));
  }
  *out = std::move(im);
  return true;
}

void QueueImage(uint64_t id, const SandboxCall& call) {
  for (const auto& q : g_queue)
    if (q.first == id) return;
  g_queue.push_back({id, call});
}

void DrawQueued(st::Machine* m, const char* game) {
  if (g_queue.empty()) return;
  uint64_t t0 = SDL_GetTicksNS();
  static int total = 0;
  static double ms = 0;
  for (auto& [id, call] : g_queue) {
    Image im;
    if (!DrawInSandbox(m, call, &im)) im = {};
    SetImage(id, std::move(im));
  }
  total += int(g_queue.size()), ms += (SDL_GetTicksNS() - t0) / 1e6;
  g_count_decoded += int(g_queue.size());
  g_queue.clear();
  if (total % 50 < 1 || total < 2)  // now and then (the first, then about every 50)
    LOG("%s: %d sprite images drawn in the sandbox so far, %.2f ms each", game, total, ms / total);
}

void Return(st::Machine& m) {
  uint32_t sp = cpu::Get(cpu::A7);
  const uint8_t* r = m.Ram() + (sp & 0xFFFFFF);
  cpu::Set(cpu::PC, uint32_t(r[0]) << 24 | r[1] << 16 | r[2] << 8 | r[3]);
  cpu::Set(cpu::A7, sp + 4);
}

void FrameShown(uint32_t screen) {
  ++g_shown;
  if (!g_real.capturing) {
    g_have = false;
    return;
  }
  if (g_real.hold) {  // no new picture: nothing moves; the items drawn into the screen on show
    if (auto b = g_real.building.find(screen); b != g_real.building.end() && screen != g_cur_screen)
      g_cur = b->second, g_cur_screen = screen, g_have = true;
    g_prev = g_cur;
    g_match.resize(g_cur.size());
    for (int i = 0; i < int(g_cur.size()); ++i) g_match[i] = i;
    return;
  }
  // Which list: the run-ahead copy's, else the last complete draw pass, else (0) the one in the screen shown.
  const std::vector<Item>* pick = nullptr;
  if (g_ahead >= 2 && g_fork.have_latest) pick = &g_fork.latest;
  else if (g_ahead >= 1 && g_real.have_latest) pick = &g_real.latest;
  else if (g_ahead == 0) {
    if (g_have && screen == g_cur_screen) {
      g_prev = g_cur;
      g_match.resize(g_cur.size());
      for (int i = 0; i < int(g_cur.size()); ++i) g_match[i] = i;
      return;
    }
    auto b = g_real.building.find(screen);
    if (b != g_real.building.end()) pick = &b->second;
  }
  if (!pick) {
    g_have = false;
    return;
  }
  g_prev = std::move(g_cur);
  g_cur = *pick;
  if (!g_have) g_prev = g_cur;
  g_cur_screen = screen;
  g_have = true;
  if (g_probe)
    for (const Item& it : g_cur)
      if (it.rotate) LOG("probe %d: angle %.1f, %zu items", g_shown, it.angle * 57.2958, g_cur.size());
  Match();
  // Stats (logged every 500 frames shown): items a frame, how many slid from the frame before, how many jumped
  // (the same key there, but too far to slide).
  static int frames = 0, items = 0, slid = 0, jumped = 0, moved = 0;
  std::unordered_map<uint64_t, const Item*> before;
  for (const Item& it : g_prev) before[it.key] = &it;
  for (int i = 0; i < int(g_cur.size()); ++i) {
    const Item& it = g_cur[i];
    ++items;
    if (g_match[i] >= 0) {
      const Item& o = g_prev[g_match[i]];
      ++slid, moved += o.x != it.x || o.y != it.y;
    } else if (before.count(it.key)) {
      ++jumped;
    }
  }
  if (++frames == 500) {
    LOG("remaster: %.1f items a frame: %.1f%% slid from the frame before (%.1f%% moving), %.1f%% jumped, %.1f%% new",
        items / 500.0, 100.0 * slid / std::max(items, 1), 100.0 * moved / std::max(items, 1),
        100.0 * jumped / std::max(items, 1), 100.0 * (items - slid - jumped) / std::max(items, 1));
    frames = items = slid = jumped = moved = 0;
  }
}

bool Active() { return Enabled() && g_real.capturing && g_have; }

void Draw(SDL_Renderer* r, SDL_Texture* st_texture, const SDL_FRect& view, float alpha,
          const uint16_t* line_palettes, bool ste_colours, const uint32_t* st_pixels) {
  const int W = 320 * kScale, H = 200 * kScale;
  if (!g_target) {
    g_target = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, W, H);
    if (!g_target) {
      LOG("remaster: no render target (%s)", SDL_GetError());
      g_enabled = false;
      return;
    }
    SDL_SetTextureScaleMode(g_target, SDL_SCALEMODE_LINEAR);
  }
  static LineColours lines;
  MakeLineColours(line_palettes, ste_colours, &lines);

  int lw = 0, lh = 0;
  SDL_RendererLogicalPresentation mode = SDL_LOGICAL_PRESENTATION_DISABLED;
  SDL_GetRenderLogicalPresentation(r, &lw, &lh, &mode);
  SDL_SetRenderTarget(r, g_target);
  SDL_SetRenderLogicalPresentation(r, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
  SDL_FRect full{0, 0, float(W), float(H)};
  bool any_behind = false;
  if (st_pixels)
    for (const Item& it : g_cur) any_behind |= it.behind;
  if (!any_behind) {
    SDL_RenderTexture(r, st_texture, nullptr, &full);
  } else {  // colour 0 (line by line), then the items behind; the picture comes over them below
    for (int y = 0; y < 200;) {
      uint32_t c = lines.pal[y][0];
      int e = y + 1;
      while (e < 200 && lines.pal[e][0] == c) ++e;
      SDL_SetRenderDrawColor(r, uint8_t(c >> 16), uint8_t(c >> 8), uint8_t(c), 255);
      SDL_FRect band{0, float(y * kScale), float(W), float((e - y) * kScale)};
      SDL_RenderFillRect(r, &band);
      y = e;
    }
  }

  alpha = std::clamp(alpha, 0.0f, 1.0f);
  int clip_top = -1, clip_bottom = -1, clip_left = -1, clip_right = -1;
  auto draw_item = [&](int i) {
    const Item& it = g_cur[i];
    float x = it.x + it.fine_x, y = it.y + it.fine_y, angle = it.angle;
    if (i < int(g_match.size()) && g_match[i] >= 0) {
      const Item& o = g_prev[g_match[i]];
      float ox = o.x + o.fine_x, oy = o.y + o.fine_y;
      float dx = x - ox;
      if (it.period) dx = std::remainder(dx, float(it.period));  // wrapped round: the short way
      x = ox + dx * alpha, y = oy + (y - oy) * alpha;
      if (it.rotate && o.rotate) angle = LerpAngle(o.angle, it.angle, alpha);
    }
    if (it.clip_top != clip_top || it.clip_bottom != clip_bottom || it.clip_left != clip_left || it.clip_right != clip_right) {
      clip_top = it.clip_top, clip_bottom = it.clip_bottom, clip_left = it.clip_left, clip_right = it.clip_right;
      SDL_Rect clip{clip_left * kScale, clip_top * kScale, (clip_right - clip_left + 1) * kScale,
                    (clip_bottom - clip_top + 1) * kScale};
      SDL_SetRenderClipRect(r, &clip);
    }
    if (it.kind == kDot) {
      uint32_t c = lines.At(int(std::lround(y)))[it.colour & 15];
      SDL_SetRenderDrawColor(r, uint8_t(c >> 16), uint8_t(c >> 8), uint8_t(c), 255);
      SDL_FRect d{x * kScale, y * kScale, float(it.w * kScale), float(it.h * kScale)};
      SDL_RenderFillRect(r, &d);
      return;
    }
    auto found = g_images.find(it.image);
    if (found == g_images.end()) return;
    const Image& im = found->second;
    float top = y + im.oy;
    if (!it.rotate && (g_used[it.image] & lines.varying)) {  // raster colours: each row in its line's colours
      for (int j = 0; j < im.h; ++j) {
        SDL_Texture* t = TextureFor(r, it.image, lines.At(int(std::floor(top + j + 0.5f))));
        if (!t) break;
        SDL_FRect src{0, float(j), float(im.w), 1};
        SDL_FRect d{(x + im.ox) * kScale, (top + j) * kScale, float(im.w * kScale), float(kScale)};
        SDL_RenderTexture(r, t, &src, &d);
      }
      return;
    }
    SDL_Texture* t = TextureFor(r, it.image, lines.At(int(top + im.h / 2)));
    if (!t) return;
    SDL_FRect d{(x + im.ox) * kScale, top * kScale, float(im.w * kScale), float(im.h * kScale)};
    if (it.rotate) SDL_RenderTextureRotated(r, t, nullptr, &d, (angle - it.angle_base) * 57.2957795, nullptr, SDL_FLIP_NONE);
    else SDL_RenderTexture(r, t, nullptr, &d);
  };
  if (any_behind) {
    for (int i = 0; i < int(g_cur.size()); ++i)
      if (g_cur[i].behind) draw_item(i);
    // The picture over them, colour 0 see-through.
    if (!g_over) {
      g_over = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 320, 200);
      if (g_over) SDL_SetTextureBlendMode(g_over, SDL_BLENDMODE_BLEND), SDL_SetTextureScaleMode(g_over, SDL_SCALEMODE_NEAREST);
    }
    void* px = nullptr;
    int pitch = 0;
    if (g_over && SDL_LockTexture(g_over, nullptr, &px, &pitch)) {
      for (int y = 0; y < 200; ++y) {
        const uint32_t* src = st_pixels + y * 320;
        uint32_t* dst = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(px) + y * pitch);
        uint32_t c0 = lines.pal[y][0] & 0xFFFFFF;
        for (int x = 0; x < 320; ++x) dst[x] = (src[x] & 0xFFFFFF) == c0 ? 0 : src[x] | 0xFF000000u;
      }
      SDL_UnlockTexture(g_over);
      SDL_SetRenderClipRect(r, nullptr);
      clip_top = clip_bottom = clip_left = clip_right = -1;
      SDL_RenderTexture(r, g_over, nullptr, &full);
    } else {
      SDL_SetRenderClipRect(r, nullptr);
      clip_top = clip_bottom = clip_left = clip_right = -1;
      SDL_RenderTexture(r, st_texture, nullptr, &full);  // no texture: the picture over them, as it is
    }
  }
  for (int i = 0; i < int(g_cur.size()); ++i)
    if (!any_behind || !g_cur[i].behind) draw_item(i);
  SDL_SetRenderClipRect(r, nullptr);
  SDL_SetRenderTarget(r, nullptr);
  SDL_SetRenderLogicalPresentation(r, lw, lh, mode);
  SDL_SetTextureScaleMode(g_target, crt::Enabled() ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_PIXELART);
  crt::DrawPicture(r, g_target, view, 320, 200);
}

void LogFrame() {
  for (const Item& it : g_cur) {
    const Image* im = g_images.count(it.image) ? &g_images[it.image] : nullptr;
    char fine[48] = "";
    if (it.fine_x != 0 || it.fine_y != 0) std::snprintf(fine, sizeof(fine), " fine %+.2f,%+.2f", it.fine_x, it.fine_y);
    LOG("remaster item %016llX %s at %.0f,%.0f clip %d..%d image %016llX %dx%d%+d%+d colour %d%s", (unsigned long long)it.key,
        it.kind == kDot ? "dot" : "sprite", it.x, it.y, it.clip_top, it.clip_bottom, (unsigned long long)it.image,
        im ? im->w : -1, im ? im->h : -1, im ? im->ox : 0, im ? im->oy : 0, it.colour, fine);
  }
}

void ComposeSoftware(const uint32_t* st, const uint16_t* line_palettes, bool ste_colours, std::vector<uint32_t>* out) {
  out->assign(st, st + 320 * 200);
  static LineColours lines;
  MakeLineColours(line_palettes, ste_colours, &lines);
  for (int pass = 0; pass < 2; ++pass)  // the items behind the picture first (only where it is colour 0)
  for (const Item& it : g_cur) {
    if (it.behind != (pass == 0)) continue;
    auto put = [&](int x, int y, int c) {
      if (x >= std::max<int>(0, it.clip_left) && x <= std::min<int>(319, it.clip_right) && y >= std::max<int>(0, it.clip_top) &&
          y <= std::min<int>(199, it.clip_bottom) && (!it.behind || (st[size_t(y) * 320 + x] & 0xFFFFFF) == (lines.pal[y][0] & 0xFFFFFF)))
        (*out)[size_t(y) * 320 + x] = lines.pal[y][c & 15];
    };
    int x = int(std::lround(it.x)) & -int(it.snap), y = int(std::lround(it.y));
    if (it.kind == kDot) {
      for (int j = 0; j < it.h; ++j)
        for (int i = 0; i < it.w; ++i) put(x + i, y + j, it.colour);
      continue;
    }
    auto im = g_images.find(it.image);
    if (im == g_images.end()) continue;
    const Image& m = im->second;
    for (int j = 0; j < m.h; ++j)
      for (int i = 0; i < m.w; ++i)
        if (uint8_t c = m.pixels[size_t(j) * m.w + i]) put(x + m.ox + i, y + m.oy + j, c);
  }
}

void SetShownColour0(bool on, uint32_t argb) { g_colour0_on = on, g_colour0 = argb; }

void Shutdown() {
  DropTextures();
  if (g_target) SDL_DestroyTexture(g_target), g_target = nullptr;
  if (g_over) SDL_DestroyTexture(g_over), g_over = nullptr;
}

}  // namespace remaster
