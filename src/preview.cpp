// The launcher's live previews (preview.h).
#include "preview.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>

#include "cpu.h"
#include "disk.h"
#include "log.h"
#include "machine.h"
#include "settings.h"

namespace fs = std::filesystem;

namespace preview {
namespace {

constexpr double kWarmBudgetMs = 4;    // warm-up time a launcher frame (the picked game's)
constexpr double kSpareBudgetMs = 1.5;  // the others', once the picked one is showing
constexpr float kOpacity = 0.4f;

uint32_t Get32(const st::Machine& m, uint32_t a) {
  const uint8_t* p = m.Ram() + (a & 0xFFFFFF);
  return uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3];
}
int Get16(const st::Machine& m, uint32_t a) { return m.Ram()[a & 0xFFFFFF] << 8 | m.Ram()[(a + 1) & 0xFFFFFF]; }

// The registry's faster-CPU rule (main.cpp PlayRule): the attract demos and title animations draw every frame.
bool PlayRule(const st::Machine& m, const games::Game& g) {
  if (!g.play_at) return false;
  const uint8_t* p = m.Ram() + g.play_at;
  uint32_t v = g.play_word ? uint32_t(p[0] << 8 | p[1]) : uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
  if (g.play_vbl && Get32(m, 0x70) != g.play_vbl) return false;
  for (uint32_t more : g.play_more)
    if (v == more) return true;
  return v == g.play_value || (g.play_value2 && v == g.play_value2) || (g.play_value3 && v == g.play_value3);
}

// How a game is walked to its footage: input every frame (frame = frames since boot), when it's ready to be
// seen, the CPU speed for a frame. Frames are counted at whatever speed the CPU runs.
struct Script {
  std::function<void(st::Machine&, int)> input;
  std::function<bool(const st::Machine&, int)> ready;
  std::function<int(const st::Machine&, int)> cpu;
};

void Tap(st::Machine& m, int frame, int at, uint8_t scancode) {
  if (frame == at) m.KeyEvent(scancode, true);
  if (frame == at + 3) m.KeyEvent(scancode, false);
}

bool ScriptFor(const games::Game& g, Script* s) {
  const std::string& id = g.id;
  auto registry_cpu = [g](const st::Machine& m, int) { return PlayRule(m, g) ? 8 : 1; };
  s->cpu = registry_cpu;
  s->input = [](st::Machine& m, int) { m.MouseInput(0, 0, 0); };
  if (id == "llamatron") {
    // The shareware screen wants fire; then the version screen and scroller (CPU-timed) lead to the title.
    s->input = [](st::Machine& m, int f) {
      if (f >= 150 && f < 155) m.SetJoystick(1, st::kJoyFire);
      if (f == 155) m.SetJoystick(1, 0);
    };
    auto title = [](const st::Machine& m) { return Get32(m, m.TextBase() + 0x52FC2) == m.TextBase() + 0x551C; };
    s->ready = [title](const st::Machine& m, int f) { return f > 150 && title(m); };
    s->cpu = [title](const st::Machine& m, int) { return title(m) ? 8 : 1; };  // (the title is VBL-timed: its parade
                                                                            // draws every frame on the faster CPU)
  } else if (id == "gridrunner") {
    s->input = [](st::Machine& m, int f) { m.MouseInput(0, 0, f >= 600 && f < 604 ? 2 : 0); };  // the Llamasoft screen
    s->ready = [](const st::Machine&, int f) { return f >= 700; };
    s->cpu = [](const st::Machine& m, int f) {  // play (the attract demo) and the wave intro on the faster CPU
      int state = f > 700 ? Get16(m, 0x4B632) : 0;
      return state == 1 || state == 3 ? 8 : 1;
    };
  } else if (id == "tripatron") {
    // The title, then Esc (the panel away), then a light show: painting in a slow Lissajous loop, a palette
    // now and then (not 4 / 5 / 6: their flashes), the symmetry changing.
    s->input = [](st::Machine& m, int f) {
      uint8_t buttons = f >= 1500 && f < 1505 ? 2 : 0;
      Tap(m, f, 1700, 0x01);
      int dx = 0, dy = 0;
      if (f >= 1800) {
        double t = f - 1800;
        dx = int(std::lround(5 * std::cos(t * 0.047) + 2 * std::cos(t * 0.13)));
        dy = int(std::lround(4 * std::sin(t * 0.031) + 2 * std::sin(t * 0.11)));
        buttons = (f / 240) % 5 == 4 ? 0 : 2;
        static const uint8_t kPalettes[] = {0x02, 0x03, 0x04, 0x08, 0x09, 0x0A};
        static const uint8_t kSymmetry[] = {0x48, 0x62, 0x52, 0x47, 0x61, 0x4B, 0x4D, 0x50};
        int k = (f - 1800) / 420;
        Tap(m, f, 1800 + k * 420 + 60, kPalettes[k % 6]);
        int j = (f - 1800) / 610;
        Tap(m, f, 1800 + j * 610 + 300, kSymmetry[j % 8]);
      }
      m.MouseInput(dx, dy, buttons);
    };
    s->ready = [](const st::Machine&, int f) { return f >= 1830; };
    s->cpu = [](const st::Machine&, int) { return 1; };
  } else if (id == "rmc") {
    s->input = [](st::Machine& m, int f) {  // its shareware screen: fire
      m.SetJoystick(1, f >= 300 && f < 305 ? st::kJoyFire : 0);
    };
    s->ready = [](const st::Machine&, int f) { return f >= 500; };
  } else if (id == "rmctt") {
    s->input = [](st::Machine& m, int f) {
      // The shareware screen counts 40 s before it takes fire: the same patch as stgames.cpp, then fire.
      constexpr uint32_t kNag = 0x102A0;
      if (Get16(m, kNag) == 0x0C79 && Get16(m, kNag + 2) == 0x07D0 && Get32(m, kNag + 4) == 0x0003D94E &&
          Get16(m, kNag + 8) == 0x6FF6)
        m.Ram()[kNag + 8] = 0x4E, m.Ram()[kNag + 9] = 0x71;
      if (f >= 300 && f < 305) m.SetJoystick(1, st::kJoyFire);
      if (f == 305) m.SetJoystick(1, 0);
    };
    s->ready = [](const st::Machine&, int f) { return f >= 500; };
  } else if (id == "andes") {
    s->input = [](st::Machine& m, int f) { m.MouseInput(0, 0, f >= 300 && f < 305 ? 2 : 0); };  // the crack's screen
    s->ready = [](const st::Machine&, int f) { return f >= 400; };
  } else if (id == "defender2") {
    s->ready = [](const st::Machine&, int f) { return f >= 1450; };
  } else if (id == "hardcore") {
    // Its title is a still picture: play instead - the blasters wander round the edges, side lasers now and then.
    s->input = [](st::Machine& m, int f) {
      uint8_t b = (f >= 600 && f < 605) || (f >= 1500 && f < 1505) || (f > 1600 && f % 700 < 5) ? 2 : 0;
      int dx = 0, dy = 0;
      if (f >= 1550) {
        double t = f - 1550;
        dx = int(std::lround(4 * std::cos(t * 0.023)));
        dy = int(std::lround(3 * std::sin(t * 0.017)));
        if ((f / 90) % 3 == 0) b = 2;
      }
      m.MouseInput(dx, dy, b);
      // No strobes (its own switch, as stgames.cpp's hc_strobes: $10D66 = 1 while playing, $338BA -> $10C82).
      if (Get32(m, 0x338BA) == 0x10C82) m.Ram()[0x10D66] = 0, m.Ram()[0x10D67] = 1;
    };
    s->ready = [](const st::Machine&, int f) { return f >= 1560; };
  } else if (id == "photonstorm") {
    s->ready = [](const st::Machine&, int f) { return f >= 1200; };
  } else {
    return false;  // (user disks, the Settings disk)
  }
  return true;
}

struct Run {
  std::unique_ptr<st::Machine> m;
  std::vector<uint8_t> ctx;  // its CPU context between slices
  Script script;
  int frame = 0;
  bool ready = false, failed = false;
};

SDL_Renderer* g_r = nullptr;
fs::path g_userdata;
std::map<std::string, Run> g_runs;
const games::Game* g_want = nullptr;  // picked
std::string g_shown;                  // the game whose picture is in the trail
float g_show = 0;                     // its fade, 0..1
double g_clock = 0, g_time = 0;
bool g_have_frame = false;
double g_bright = 0;  // the picture's recent average brightness (Push: the flash guard)
SDL_Texture *g_frame = nullptr, *g_trail = nullptr, *g_vignette = nullptr;
std::vector<const games::Game*> g_all;  // every known game (SetGames: warmed in spare time)

std::string Upper(std::string s) {
  for (char& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
  return s;
}

bool Start(Run& r, const games::Game& g) {
  std::map<std::string, disk::Bytes> files = games::ReadSource(g.source, nullptr);
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(games::SaveDir(g, g_userdata), ec)) {  // saves (and installs) too
    std::string name = Upper(e.path().filename().string());
    std::string ext = name.size() > 4 ? name.substr(name.size() - 4) : "";
    if (!e.is_regular_file() || ext == ".LOG" || ext == ".TXT" || ext == ".BMP") continue;
    files[name] = disk::ReadHostFile(e.path().string());
  }
  const std::string prog = games::ProgramToRun(g);
  auto it = files.find(prog);
  if (it == files.end()) return LOG("preview: %s: no %s (not installed?)", g.id.c_str(), prog.c_str()), false;
  disk::Program program;
  std::string error;
  bool ok = g.id == "llamatron" ? disk::UnpackLlamatron(it->second, &program, &error)
                                : disk::LoadPrg(it->second, &program, &error);
  if (!ok) return LOG("preview: %s: %s", g.id.c_str(), error.c_str()), false;
  r.m = std::make_unique<st::Machine>();
  if (!r.m->Load(program, std::move(files), std::string(), &error))  // (no save folder: it writes nothing)
    return LOG("preview: %s: %s", g.id.c_str(), error.c_str()), false;
  r.m->SetSampleRate(48000);
  r.m->SetSte(g.ste);
  if (g.boot_60hz && settings::Get(g.id + "_60hz", 1) != 0) r.m->SetBoot60Hz(true);
  r.m->TypeAhead(games::ConsoleAnswers(g));
  if (g.id == "tripatron") r.m->SetCalmBackground(true);
  if (g.id == "llamatron") {  // no screen flashes (game.cpp ApplyPatches' first patch: the palette write at $53DA)
    uint8_t* p = r.m->Ram() + r.m->TextBase() + 0x53DA;
    static const uint8_t kWas[] = {0x33, 0xC0, 0x00, 0xFF, 0x82, 0x40};
    if (std::equal(std::begin(kWas), std::end(kWas), p))
      for (int i = 0; i < 6; i += 2) p[i] = 0x4E, p[i + 1] = 0x71;
  }
  r.ctx = cpu::SaveContext();
  LOG("preview: %s started", g.id.c_str());
  return true;
}

// One frame of a run (the CPU switched to it by the caller).
void Step(Run& r) {
  r.m->SetCpuSpeed(r.script.cpu(*r.m, r.frame));
  r.script.input(*r.m, r.frame);
  r.m->RunFrame();
  r.m->Audio().clear();
  ++r.frame;
  if (!r.ready && r.script.ready(*r.m, r.frame)) r.ready = true, LOG("preview: ready after %d frames", r.frame);
}

// Runs `fn` with the CPU on the run's machine.
template <class F>
void With(Run& r, F fn) {
  cpu::SetMachine(r.m.get());
  cpu::RestoreContext(r.ctx);
  fn();
  r.ctx = cpu::SaveContext();
}

Run* Get(const games::Game& g) {
  auto it = g_runs.find(g.id);
  if (it != g_runs.end()) return it->second.failed ? nullptr : &it->second;
  Run& r = g_runs[g.id];
  if (!ScriptFor(g, &r.script) || !Start(r, g)) {
    r.failed = true;
    r.m.reset();
    return nullptr;
  }
  return &r;
}

void WarmUp(Run& r, double budget_ms) {
  uint64_t t0 = SDL_GetTicksNS();
  With(r, [&] {
    while (!r.ready && (SDL_GetTicksNS() - t0) / 1e6 < budget_ms) Step(r);
  });
}

void Push(const st::Machine& m) {
  if (!g_r || m.FrameWidth() != st::kScreenW) return;
  if (!g_frame) {
    g_frame = SDL_CreateTexture(g_r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, st::kScreenW, st::kScreenH);
    g_trail = SDL_CreateTexture(g_r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, st::kScreenW, st::kScreenH);
    if (!g_frame || !g_trail) return;
    SDL_SetTextureScaleMode(g_frame, SDL_SCALEMODE_LINEAR);
    SDL_SetTextureScaleMode(g_trail, SDL_SCALEMODE_LINEAR);
    SDL_SetTextureBlendMode(g_frame, SDL_BLENDMODE_BLEND);
    SDL_SetTextureBlendMode(g_trail, SDL_BLENDMODE_BLEND);
  }
  // Flash guard: a frame much brighter than the picture has been (an explosion's flash, a strobe) goes into the
  // trail fainter, so a flash is a soft swell behind the disks, not a blink.
  double sum = 0;
  for (int i = 0; i < st::kScreenW * st::kScreenH; i += 7) {
    uint32_t c = m.Frame()[i];
    sum += ((c >> 16 & 255) * 3 + (c >> 8 & 255) * 6 + (c & 255)) / 2550.0;
  }
  const double bright = sum / (st::kScreenW * st::kScreenH / 7.0);
  if (!g_have_frame) g_bright = bright;
  const float calm = float(std::min(1.0, (g_bright + 0.06) / std::max(bright, 1e-3)));
  g_bright += (bright - g_bright) * 0.04;
  void* px = nullptr;
  int pitch = 0;
  if (!SDL_LockTexture(g_frame, nullptr, &px, &pitch)) return;
  for (int y = 0; y < st::kScreenH; ++y) {
    const uint32_t* src = m.Frame() + y * st::kScreenW;
    uint32_t* dst = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(px) + y * pitch);
    for (int x = 0; x < st::kScreenW; ++x) dst[x] = src[x] | 0xFF000000u;
  }
  SDL_UnlockTexture(g_frame);
  // Into the trail: what was there fades a little, the new frame goes over it (persistence, like a projector's
  // phosphor-slow dream).
  SDL_Texture* old = SDL_GetRenderTarget(g_r);
  SDL_SetRenderTarget(g_r, g_trail);
  if (!g_have_frame) {
    SDL_SetRenderDrawColor(g_r, 0, 0, 0, 255);
    SDL_RenderClear(g_r);
  }
  SDL_SetRenderDrawBlendMode(g_r, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(g_r, 0, 0, 0, 70);
  SDL_RenderFillRect(g_r, nullptr);
  SDL_SetTextureAlphaModFloat(g_frame, g_have_frame ? 0.55f * calm : 1.0f);
  SDL_RenderTexture(g_r, g_frame, nullptr, nullptr);
  SDL_SetRenderTarget(g_r, old);
  g_have_frame = true;
}

SDL_Texture* Vignette() {
  if (g_vignette || !g_r) return g_vignette;
  const int n = 256;
  std::vector<uint32_t> px(size_t(n * n));
  for (int y = 0; y < n; ++y)
    for (int x = 0; x < n; ++x) {
      float u = (x + 0.5f) / n * 2 - 1, v = (y + 0.5f) / n * 2 - 1;
      float d = std::pow(std::pow(std::fabs(u), 3.0f) + std::pow(std::fabs(v), 3.0f), 1 / 3.0f);  // a rounded rectangle
      float a = std::clamp((d - 0.45f) / 0.55f, 0.0f, 1.0f);
      a = a * a * (3 - 2 * a);
      px[size_t(y * n + x)] = uint32_t(std::lround(a * 255)) << 24;  // black, this opaque
    }
  g_vignette = SDL_CreateTexture(g_r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, n, n);
  if (g_vignette) {
    SDL_UpdateTexture(g_vignette, nullptr, px.data(), n * 4);
    SDL_SetTextureBlendMode(g_vignette, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(g_vignette, SDL_SCALEMODE_LINEAR);
  }
  return g_vignette;
}

}  // namespace

void Init(SDL_Renderer* renderer, const fs::path& userdata) { g_r = renderer, g_userdata = userdata; }

void SetGames(const std::vector<games::Game>& games) {
  g_all.clear();
  for (const games::Game& g : games)
    if (!g.user && !g.settings) g_all.push_back(&g);
}

void Select(const games::Game* game) {
  g_want = game;
}

void Update(double dt) {
  g_time += dt;
  const uint64_t t0 = SDL_GetTicksNS();
  struct Timer {  // the log's "preview: ... ms a launcher frame" every ~10 s (the Deck's budget)
    uint64_t t0;
    ~Timer() {
      static double sum = 0, worst = 0, since = 0;
      static int n = 0;
      double ms = (SDL_GetTicksNS() - t0) / 1e6;
      sum += ms, worst = std::max(worst, ms), ++n;
      if ((since += ms) >= 0 && n >= 600) {
        LOG("preview: %.2f ms a launcher frame (worst %.2f)", sum / n, worst);
        sum = worst = since = 0, n = 0;
      }
    }
  } timer{t0};
  Run* want = g_want ? Get(*g_want) : nullptr;
  const bool switching = !want || g_want->id != g_shown;
  if (switching) {  // the old picture fades out first
    g_show = std::max(0.0f, g_show - float(dt) * 2.5f);
    if (g_show == 0 && want && want->ready) g_shown = g_want->id, g_have_frame = false, g_clock = 0;
  }
  if (want && !want->ready) WarmUp(*want, kWarmBudgetMs);
  if (want && want->ready && g_want->id == g_shown) {
    g_clock += dt * want->m->FrameHz();
    int n = std::min(int(g_clock), 3);
    g_clock -= std::floor(g_clock);
    if (n > 0) {
      With(*want, [&] {
        for (int i = 0; i < n; ++i) Step(*want);
      });
      Push(*want->m);
    }
    if (g_have_frame) g_show = std::min(1.0f, g_show + float(dt) * 0.7f);
    // Spare time: the next game not warmed up yet, so it shows at once when picked.
    for (const games::Game* g : g_all)
      if (Run* r = Get(*g); r && !r->ready) {
        WarmUp(*r, kSpareBudgetMs);
        break;
      }
  }
}

void Draw(int w, int h) {
  if (!g_r || !g_trail || g_show <= 0 || !g_have_frame) return;
  const float t = float(g_time);
  // A soft wall of light behind the disks: big, faint, breathing and drifting a little, flickering like a
  // projector; four offset copies blur it (out of focus), its edges sink into the dark.
  float ph = h * 0.80f, pw = ph * 320 / 217;
  if (pw > w * 0.96f) pw = w * 0.96f, ph = pw * 217 / 320;
  float breathe = 1 + 0.018f * std::sin(t * 0.21f);
  pw *= breathe, ph *= breathe;
  float cx = w * 0.5f + w * 0.006f * std::sin(t * 0.13f), cy = h * 0.43f + h * 0.005f * std::sin(t * 0.17f);
  SDL_FRect rect{cx - pw / 2, cy - ph / 2, pw, ph};
  float flicker = 1 + 0.05f * std::sin(t * 7.3f) * std::sin(t * 1.1f) + 0.03f * std::sin(t * 13.7f + 1.3f);
  float a = kOpacity * g_show * flicker;
  SDL_SetTextureColorModFloat(g_trail, 0.88f, 0.84f, 1.0f);  // a little cool
  SDL_SetTextureBlendMode(g_trail, SDL_BLENDMODE_BLEND);
  SDL_SetTextureAlphaModFloat(g_trail, a * 0.75f);
  SDL_RenderTexture(g_r, g_trail, nullptr, &rect);
  SDL_SetTextureBlendMode(g_trail, SDL_BLENDMODE_ADD);
  SDL_SetTextureAlphaModFloat(g_trail, a * 0.16f);
  const float o = pw * 0.006f;
  const float offs[4][2] = {{o, 0}, {-o, 0}, {0, o * 0.8f}, {0, -o * 0.8f}};
  for (const auto& d : offs) {
    SDL_FRect r2{rect.x + d[0] + o * 0.4f * std::sin(t * 0.7f), rect.y + d[1], rect.w, rect.h};
    SDL_RenderTexture(g_r, g_trail, nullptr, &r2);
  }
  SDL_SetTextureBlendMode(g_trail, SDL_BLENDMODE_BLEND);
  if (SDL_Texture* v = Vignette()) {
    const float m = o * 1.5f + 4;  // past every offset copy: no edge left showing
    SDL_FRect vr{rect.x - m, rect.y - m, rect.w + 2 * m, rect.h + 2 * m};
    SDL_RenderTexture(g_r, v, nullptr, &vr);
  }
}

void Pause() {
  g_show = 0;
  g_shown.clear();
  g_have_frame = false;
}

void Shutdown() {
  g_runs.clear();
  for (SDL_Texture** t : {&g_frame, &g_trail, &g_vignette})
    if (*t) SDL_DestroyTexture(*t), *t = nullptr;
}

bool Test(const std::vector<games::Game>& games, const std::string& id, int step, int count, const std::string& dir) {
  for (const games::Game& g : games) {
    if (g.id != id || g.user) continue;
    Run* r = Get(g);
    if (!r) return false;
    With(*r, [&] {
      while (!r->ready && r->frame < 20000) Step(*r);
      for (int n = 0; n < count; ++n) {
        for (int i = 0; i < step; ++i) Step(*r);
        std::vector<uint32_t> px(r->m->Frame(), r->m->Frame() + st::kScreenW * st::kScreenH);
        if (SDL_Surface* s = SDL_CreateSurfaceFrom(st::kScreenW, st::kScreenH, SDL_PIXELFORMAT_ARGB8888, px.data(),
                                                  st::kScreenW * 4)) {
          SDL_SaveBMP(s, (dir + "/" + id + "_" + std::to_string(n) + ".bmp").c_str());
          SDL_DestroySurface(s);
        }
      }
    });
    LOG("preview test: %s ready at %d, ran to %d", id.c_str(), r->frame - step * count, r->frame);
    return true;
  }
  return false;
}

}  // namespace preview
