#include "remaster.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
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
uint32_t g_cur_screen = 0;
bool g_have = false;
int g_ahead = 2;  // remaster_ahead
std::function<void()> g_save, g_restore;  // the game side's C++ state around a run-ahead
bool g_probe = false;
int g_shown = 0;  // frames shown (probe log)
uint64_t g_message_until = 0;
char g_message[64] = "";

std::unordered_map<uint64_t, Image> g_images;
struct Texture {
  SDL_Texture* t = nullptr;
  uint64_t sig = 0;  // the colours it was made with (only those its image uses)
};
std::unordered_map<uint64_t, Texture> g_textures;
std::unordered_map<uint64_t, uint16_t> g_used;  // per image: the colours it uses (bit per index)
SDL_Texture* g_target = nullptr;

uint32_t Argb(uint16_t c, bool ste) {
  auto ch = [&](unsigned v) {
    if (ste) return (((v & 7) << 1) | ((v >> 3) & 1)) * 17;
    v &= 7;
    return (v << 5) | (v << 2) | (v >> 1);
  };
  return 0xFF000000u | ch((c >> 8) & 15) << 16 | ch((c >> 4) & 15) << 8 | ch(c & 15);
}

void DropTextures() {
  for (auto& [id, t] : g_textures) SDL_DestroyTexture(t.t);
  g_textures.clear();
}

// The image in the current colours: made again only when a colour it uses has changed.
SDL_Texture* TextureFor(SDL_Renderer* r, uint64_t id, const uint32_t* pal) {
  auto im = g_images.find(id);
  if (im == g_images.end() || im->second.w <= 0 || im->second.h <= 0) return nullptr;
  const Image& image = im->second;
  uint16_t used = g_used[id];
  uint64_t sig = 1469598103934665603ull;
  for (int i = 1; i < 16; ++i)
    if (used >> i & 1) sig = (sig ^ pal[i]) * 1099511628211ull;
  Texture& tex = g_textures[id];
  if (tex.t && tex.sig == sig) return tex.t;
  std::vector<uint32_t> px(size_t(image.w) * size_t(image.h));
  for (size_t i = 0; i < px.size(); ++i) px[i] = image.pixels[i] ? pal[image.pixels[i]] : 0;
  if (!tex.t) {
    tex.t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, image.w, image.h);
    if (!tex.t) return nullptr;
    SDL_SetTextureBlendMode(tex.t, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(tex.t, SDL_SCALEMODE_NEAREST);
  }
  SDL_UpdateTexture(tex.t, nullptr, px.data(), image.w * 4);
  tex.sig = sig;
  return tex.t;
}

float LerpAngle(float a, float b, float t) {
  float d = std::remainder(b - a, 6.28318530718f);
  return a + d * t;
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

void BeginFrame(uint32_t screen) {
  g_cap->capturing = true;
  g_cap->hold = false;
  g_cap->building[screen].clear();
  g_cap->counts[screen].clear();
}

void EndFrame(uint32_t screen) {
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

bool HaveImage(uint64_t id) { return g_images.count(id) != 0; }
void SetImage(uint64_t id, Image image) {
  uint16_t used = 0;
  for (uint8_t c : image.pixels) used |= uint16_t(1u << (c & 15));
  g_used[id] = used;
  g_images[id] = std::move(image);
  if (auto t = g_textures.find(id); t != g_textures.end()) SDL_DestroyTexture(t->second.t), g_textures.erase(t);
}

void Hold() {
  if (g_cap->capturing) g_cap->hold = true;
}

void Stop() {
  *g_cap = Capture{};
  if (g_cap == &g_real) g_have = false, g_prev.clear(), g_cur.clear(), g_fork = Capture{};
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

bool DrawInSandbox(st::Machine* m, const SandboxCall& call, Image* out) {
  uint32_t screen = call.regs[9] & 0xFFFFFE;
  uint32_t sp = (call.regs[15] & 0xFFFFFE) - 512;
  if (screen + 32000 + 64 > st::kRamSize || sp < 0x1000) return false;
  auto fork = std::make_unique<st::Machine>(*m);
  fork->sandbox = true;
  uint8_t* ram = fork->Ram();
  std::memset(ram + screen, 0, 32000);
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
  Image all = DecodePlanes(ram + screen, 20, 200);
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

void FrameShown(uint32_t screen) {
  ++g_shown;
  if (!g_real.capturing) {
    g_have = false;
    return;
  }
  if (g_real.hold) {  // no new picture: nothing moves
    g_prev = g_cur;
    return;
  }
  // Which list: the run-ahead copy's, else the last complete draw pass, else (0) the one in the screen shown.
  const std::vector<Item>* pick = nullptr;
  if (g_ahead >= 2 && g_fork.have_latest) pick = &g_fork.latest;
  else if (g_ahead >= 1 && g_real.have_latest) pick = &g_real.latest;
  else if (g_ahead == 0) {
    if (g_have && screen == g_cur_screen) {
      g_prev = g_cur;
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
  // Stats (logged every 500 frames shown): items a frame, how many slid from the frame before, how many jumped.
  static int frames = 0, items = 0, slid = 0, jumped = 0, moved = 0;
  std::unordered_map<uint64_t, const Item*> before;
  for (const Item& it : g_prev) before[it.key] = &it;
  for (const Item& it : g_cur) {
    ++items;
    auto p = before.find(it.key);
    if (p == before.end()) continue;
    float dx = std::fabs(it.x - p->second->x), dy = std::fabs(it.y - p->second->y);
    if (dx > kTeleport || dy > kTeleport) ++jumped;
    else ++slid, moved += dx + dy > 0;
  }
  if (++frames == 500) {
    LOG("remaster: %.1f items a frame: %.1f%% slid from the frame before (%.1f%% moving), %.1f%% jumped, %.1f%% new",
        items / 500.0, 100.0 * slid / std::max(items, 1), 100.0 * moved / std::max(items, 1),
        100.0 * jumped / std::max(items, 1), 100.0 * (items - slid - jumped) / std::max(items, 1));
    frames = items = slid = jumped = moved = 0;
  }
}

bool Active() { return Enabled() && g_real.capturing && g_have; }

void Draw(SDL_Renderer* r, SDL_Texture* st_texture, const SDL_FRect& view, float alpha, const uint16_t* palette,
          bool ste_colours) {
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
  uint32_t pal[16];
  for (int i = 0; i < 16; ++i) pal[i] = Argb(palette[i], ste_colours);

  int lw = 0, lh = 0;
  SDL_RendererLogicalPresentation mode = SDL_LOGICAL_PRESENTATION_DISABLED;
  SDL_GetRenderLogicalPresentation(r, &lw, &lh, &mode);
  SDL_SetRenderTarget(r, g_target);
  SDL_SetRenderLogicalPresentation(r, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
  SDL_FRect full{0, 0, float(W), float(H)};
  SDL_RenderTexture(r, st_texture, nullptr, &full);

  std::unordered_map<uint64_t, const Item*> before;
  before.reserve(g_prev.size());
  for (const Item& it : g_prev) before[it.key] = &it;
  alpha = std::clamp(alpha, 0.0f, 1.0f);
  int clip_top = -1, clip_bottom = -1;
  for (const Item& it : g_cur) {
    float x = it.x, y = it.y, angle = it.angle;
    if (auto p = before.find(it.key); p != before.end() && p->second->kind == it.kind) {
      const Item& o = *p->second;
      if (std::fabs(x - o.x) <= kTeleport && std::fabs(y - o.y) <= kTeleport) {
        x = o.x + (x - o.x) * alpha, y = o.y + (y - o.y) * alpha;
        if (it.rotate && o.rotate) angle = LerpAngle(o.angle, it.angle, alpha);
      }
    }
    if (it.clip_top != clip_top || it.clip_bottom != clip_bottom) {
      clip_top = it.clip_top, clip_bottom = it.clip_bottom;
      SDL_Rect clip{0, clip_top * kScale, W, (clip_bottom - clip_top + 1) * kScale};
      SDL_SetRenderClipRect(r, &clip);
    }
    if (it.kind == kDot) {
      uint32_t c = pal[it.colour & 15];
      SDL_SetRenderDrawColor(r, uint8_t(c >> 16), uint8_t(c >> 8), uint8_t(c), 255);
      SDL_FRect d{x * kScale, y * kScale, float(it.w * kScale), float(it.h * kScale)};
      SDL_RenderFillRect(r, &d);
      continue;
    }
    SDL_Texture* t = TextureFor(r, it.image, pal);
    if (!t) continue;
    const Image& im = g_images[it.image];
    SDL_FRect d{(x + im.ox) * kScale, (y + im.oy) * kScale, float(im.w * kScale), float(im.h * kScale)};
    if (it.rotate) SDL_RenderTextureRotated(r, t, nullptr, &d, (angle - it.angle_base) * 57.2957795, nullptr, SDL_FLIP_NONE);
    else SDL_RenderTexture(r, t, nullptr, &d);
  }
  SDL_SetRenderClipRect(r, nullptr);
  SDL_SetRenderTarget(r, nullptr);
  SDL_SetRenderLogicalPresentation(r, lw, lh, mode);
  SDL_SetTextureScaleMode(g_target, crt::Enabled() ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_PIXELART);
  crt::DrawPicture(r, g_target, view, 320, 200);
}

void LogFrame() {
  for (const Item& it : g_cur) {
    const Image* im = g_images.count(it.image) ? &g_images[it.image] : nullptr;
    LOG("remaster item %016llX %s at %.0f,%.0f clip %d..%d image %016llX %dx%d%+d%+d colour %d", (unsigned long long)it.key,
        it.kind == kDot ? "dot" : "sprite", it.x, it.y, it.clip_top, it.clip_bottom, (unsigned long long)it.image,
        im ? im->w : -1, im ? im->h : -1, im ? im->ox : 0, im ? im->oy : 0, it.colour);
  }
}

void ComposeSoftware(const uint32_t* st, const uint16_t* palette, bool ste_colours, std::vector<uint32_t>* out) {
  out->assign(st, st + 320 * 200);
  uint32_t pal[16];
  for (int i = 0; i < 16; ++i) pal[i] = Argb(palette[i], ste_colours);
  for (const Item& it : g_cur) {
    auto put = [&](int x, int y, int c) {
      if (x >= 0 && x < 320 && y >= std::max<int>(0, it.clip_top) && y <= std::min<int>(199, it.clip_bottom))
        (*out)[size_t(y) * 320 + x] = pal[c & 15];
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

void Shutdown() {
  DropTextures();
  if (g_target) SDL_DestroyTexture(g_target), g_target = nullptr;
}

}  // namespace remaster
