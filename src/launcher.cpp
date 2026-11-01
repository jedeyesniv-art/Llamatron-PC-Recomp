#include "launcher.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "crt.h"

namespace launcher {
namespace {

#include "labels.inc"

constexpr float kPi = 3.14159265f;

struct V3 {
  float x, y, z;
};
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(V3 a, float k) { return {a.x * k, a.y * k, a.z * k}; }
float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 Normalize(V3 a) {
  float l = std::sqrt(Dot(a, a));
  return l > 0 ? a * (1 / l) : a;
}
V3 RotX(V3 p, float a) {
  float c = std::cos(a), s = std::sin(a);
  return {p.x, c * p.y - s * p.z, s * p.y + c * p.z};
}
V3 RotY(V3 p, float a) {
  float c = std::cos(a), s = std::sin(a);
  return {c * p.x + s * p.z, p.y, -s * p.x + c * p.z};
}
float Smooth(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return t * t * (3 - 2 * t);
}

// A 3.5" floppy, in millimetres: 90 x 94 x 3.3, the clipped corner at the top right of the label side.
constexpr float kThick = 3.3f;
const float kOutline[5][2] = {{-45, -47}, {45, -47}, {45, 41}, {39, 47}, {-45, 47}};

// The camera: above and in front, looking slightly down; perspective with focal length f (pixels).
struct Camera {
  V3 eye{0, 38, 330};
  float pitch = 0.11f;
  float f = 0, cx = 0, cy = 0;
  V3 ToCamera(V3 p) const { return RotX(p - eye, pitch); }
  SDL_FPoint Project(V3 p, float* depth = nullptr) const {
    V3 c = ToCamera(p);
    float z = std::max(1.0f, -c.z);
    if (depth) *depth = z;
    return {cx + f * c.x / z, cy - f * c.y / z};
  }
};

SDL_FColor Shade(SDL_FColor base, float light) {
  light = std::max(0.0f, light);
  return {std::min(1.0f, base.r * light), std::min(1.0f, base.g * light), std::min(1.0f, base.b * light), base.a};
}

// A convex polygon (fan), optionally textured.
void Polygon(SDL_Renderer* r, SDL_Texture* tex, const SDL_FPoint* p, const SDL_FPoint* uv, int n, SDL_FColor c) {
  std::vector<SDL_Vertex> v(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) v[size_t(i)] = {p[i], c, uv ? uv[i] : SDL_FPoint{0, 0}};
  std::vector<int> idx;
  for (int i = 1; i + 1 < n; ++i) idx.insert(idx.end(), {0, i, i + 1});
  SDL_RenderGeometry(r, tex, v.data(), n, idx.data(), int(idx.size()));
}

float ScreenArea(const SDL_FPoint* p, int n) {
  float a = 0;
  for (int i = 0; i < n; ++i) a += p[i].x * p[(i + 1) % n].y - p[(i + 1) % n].x * p[i].y;
  return a / 2;
}

constexpr float kFloorY = -60;

// The carousel: where a disk `d` places from the middle sits (x, how far back), how much it's lit and seen.
struct Place {
  float x, z, sel, vis;
};
Place PlaceAt(float d) {
  float a = std::fabs(d), sign = d < 0 ? -1.0f : 1.0f;
  Place p;
  p.x = sign * (a <= 1 ? a * 122 : 122 + (a - 1) * 78 - (a > 2 ? (a - 2) * 18 : 0));
  p.z = -55 * a - 28 * a * a;
  p.sel = Smooth(1 - a);
  // Further out: dimmer, and the ones beyond the second neighbour fade out.
  p.vis = std::clamp(2.9f - a, 0.0f, 1.0f) * (a > 1 ? 1 - 0.3f * std::min(a - 1, 1.0f) : 1.0f);
  return p;
}

// The built-in labels by game id (Tools/make_labels.py -> labels.inc).
struct BuiltIn {
  const char* id;
  const unsigned char* data;
  size_t size;
};
const BuiltIn kBuiltIn[] = {
    {"llamatron", kLabel_llamatron, sizeof(kLabel_llamatron)},
    {"gridrunner", kLabel_gridrunner, sizeof(kLabel_gridrunner)},
    {"tripatron", kLabel_tripatron, sizeof(kLabel_tripatron)},
    {"rmc", kLabel_rmc, sizeof(kLabel_rmc)},
    {"defender2", kLabel_defender2, sizeof(kLabel_defender2)},
    {"andes", kLabel_andes, sizeof(kLabel_andes)},
    {"hardcore", kLabel_hardcore, sizeof(kLabel_hardcore)},
};

}  // namespace

Launcher::Launcher(SDL_Renderer* renderer, const std::vector<games::Game>* games) : r_(renderer), games_(games) {
  uint32_t px = 0xFFFFFFFF;
  SDL_Surface* s = SDL_CreateSurfaceFrom(1, 1, SDL_PIXELFORMAT_ARGB8888, &px, 4);
  white_ = SDL_CreateTextureFromSurface(r_, s);
  white_add_ = SDL_CreateTextureFromSurface(r_, s);
  SDL_DestroySurface(s);
  SDL_SetTextureBlendMode(white_, SDL_BLENDMODE_BLEND);
  SDL_SetTextureBlendMode(white_add_, SDL_BLENDMODE_ADD);
  BuildLabels();
}

Launcher::~Launcher() {
  for (SDL_Texture* t : {white_, white_add_})
    if (t) SDL_DestroyTexture(t);
  for (SDL_Texture* t : labels_)
    if (t) SDL_DestroyTexture(t);
}

// The owner's hand-written labels (grey ink on white, unpacked to textures); a user disk's picture
// (<image name>.png next to it), or a label written for it.
void Launcher::BuildLabels() {
  std::vector<uint32_t> pixels(size_t(kLabelW) * kLabelH);
  for (const games::Game& g : *games_) {
    SDL_Texture* tex = nullptr;
    for (const BuiltIn& b : kBuiltIn) {
      if (g.user || g.id != b.id) continue;
      size_t at = 0;
      for (size_t k = 0; k + 1 < b.size && at < pixels.size(); k += 2) {
        uint32_t v = b.data[k];
        uint32_t argb = 0xFF000000u | v << 16 | v << 8 | v;
        for (int n = 0; n < b.data[k + 1] && at < pixels.size(); ++n) pixels[at++] = argb;
      }
      SDL_Surface* s = SDL_CreateSurfaceFrom(kLabelW, kLabelH, SDL_PIXELFORMAT_ARGB8888, pixels.data(), kLabelW * 4);
      tex = SDL_CreateTextureFromSurface(r_, s);
      SDL_DestroySurface(s);
    }
    if (!tex && !g.label_picture.empty()) {
      if (SDL_Surface* s = SDL_LoadPNG(g.label_picture.string().c_str())) {
        tex = SDL_CreateTextureFromSurface(r_, s);
        SDL_DestroySurface(s);
      }
    }
    if (!tex) tex = StandInLabel(g);
    if (tex) SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
    labels_.push_back(tex);
  }
}

// A label for a disk without one: its name in capitals, a ruled box, on white.
SDL_Texture* Launcher::StandInLabel(const games::Game& g) {
  SDL_Texture* tex = SDL_CreateTexture(r_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, kLabelW, kLabelH);
  if (!tex) return nullptr;
  SDL_Texture* old = SDL_GetRenderTarget(r_);
  SDL_SetRenderTarget(r_, tex);
  SDL_SetRenderDrawColor(r_, 255, 255, 255, 255);
  SDL_RenderClear(r_);
  SDL_SetRenderDrawColor(r_, 30, 30, 40, 255);
  for (int i = 0; i < 6; ++i) {
    SDL_FRect box{36.0f + i, 40.0f + i, kLabelW - 72.0f - 2 * i, kLabelH - 80.0f - 2 * i};
    SDL_RenderRect(r_, &box);
  }
  // The name, broken into lines that fit, as big as fits.
  std::vector<std::string> lines;
  std::string word, line;
  auto flush_word = [&]() {
    if (word.empty()) return;
    if (!line.empty() && (line.size() + 1 + word.size()) > 10) lines.push_back(line), line.clear();
    line += (line.empty() ? "" : " ") + word;
    word.clear();
  };
  for (char c : g.name) {
    if (c == ' ' || c == '_' || c == '-') flush_word();
    else word += c;
  }
  flush_word();
  if (!line.empty()) lines.push_back(line);
  size_t longest = 1;
  for (const std::string& l : lines) longest = std::max(longest, l.size());
  float scale = std::min(std::floor(380.0f / (8 * float(longest))), std::floor(220.0f / (12 * float(lines.size()))));
  scale = std::max(2.0f, std::min(scale, 7.0f));
  float y = (kLabelH - float(lines.size()) * 12 * scale) / 2;
  SDL_SetRenderScale(r_, scale, scale);
  for (const std::string& l : lines) {
    float x = (kLabelW - float(l.size()) * 8 * scale) / 2;
    SDL_RenderDebugText(r_, x / scale, y / scale, l.c_str());
    y += 12 * scale;
  }
  SDL_SetRenderScale(r_, 1, 1);
  SDL_SetRenderTarget(r_, old);
  return tex;
}

void Launcher::Enter(int game) {
  selected_ = std::clamp(game, 0, std::max(0, Count() - 1));
  scroll_ = target_ = float(selected_);
  fade_in_ = 0;
  launching_ = -1;
  info_ = false;
  quit_armed_ms_ = 0;
}

void Launcher::Step(int dir) {
  if (Count() < 2) return;
  info_ = false;
  option_row_ = 0;
  selected_ = (selected_ + dir + Count()) % Count();
  target_ += float(dir);
}

float Launcher::Offset(int game) const {
  float n = float(Count());
  float d = std::fmod(float(game) - scroll_, n);
  if (d < -n / 2) d += n;
  if (d >= n / 2) d -= n;
  return d;
}

void Launcher::ChangeOption(int dir) {
  const games::Game& g = (*games_)[size_t(selected_)];
  if (g.options.empty()) return;
  const games::Game::Option& o = g.options[size_t(option_row_)];
  int n = int(o.values.size());
  games::SetOption(g, o.key, (games::OptionValue(g, o.key) + dir + n) % n);
}

Action Launcher::HandleEvent(const SDL_Event& e) {
  if (launching_ >= 0 || Count() == 0) return Action::kNone;
  const games::Game& g = (*games_)[size_t(selected_)];
  const bool options = info_ && !g.options.empty();
  auto back = [&]() {
    if (info_) {
      info_ = false;
      return Action::kNone;
    }
    if (SDL_GetTicks() < quit_armed_ms_) return Action::kQuit;
    quit_armed_ms_ = SDL_GetTicks() + 3000;
    return Action::kNone;
  };
  auto play = [&]() {
    info_ = false;
    launching_ = 0;
  };
  auto left_right = [&](int dir) { options ? ChangeOption(dir) : Step(dir); };
  auto up_down = [&](int dir) {
    if (options) option_row_ = std::clamp(option_row_ + dir, 0, int(g.options.size()) - 1);
  };
  auto toggle_info = [&]() {
    info_ = !info_;
    option_row_ = 0;
  };
  if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
    switch (e.gbutton.button) {
      case SDL_GAMEPAD_BUTTON_DPAD_LEFT: left_right(-1); break;
      case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: left_right(1); break;
      case SDL_GAMEPAD_BUTTON_DPAD_UP: up_down(-1); break;
      case SDL_GAMEPAD_BUTTON_DPAD_DOWN: up_down(1); break;
      case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: Step(-1); break;
      case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: Step(1); break;
      case SDL_GAMEPAD_BUTTON_SOUTH:
      case SDL_GAMEPAD_BUTTON_START: play(); break;
      case SDL_GAMEPAD_BUTTON_NORTH: toggle_info(); break;
      case SDL_GAMEPAD_BUTTON_WEST: if (crt::Available()) crt::Toggle(); break;
      case SDL_GAMEPAD_BUTTON_EAST:
      case SDL_GAMEPAD_BUTTON_BACK: return back();
      default: break;
    }
  } else if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
    switch (e.key.scancode) {
      case SDL_SCANCODE_LEFT: case SDL_SCANCODE_A: left_right(-1); break;
      case SDL_SCANCODE_RIGHT: case SDL_SCANCODE_D: left_right(1); break;
      case SDL_SCANCODE_UP: case SDL_SCANCODE_W: up_down(-1); break;
      case SDL_SCANCODE_DOWN: case SDL_SCANCODE_S: up_down(1); break;
      case SDL_SCANCODE_RETURN: case SDL_SCANCODE_SPACE: play(); break;
      case SDL_SCANCODE_F1: case SDL_SCANCODE_I: case SDL_SCANCODE_H: case SDL_SCANCODE_TAB: toggle_info(); break;
      case SDL_SCANCODE_F9: case SDL_SCANCODE_C: if (crt::Available()) crt::Toggle(); break;
      case SDL_SCANCODE_ESCAPE: return back();
      default: break;
    }
  } else if (e.type == SDL_EVENT_MOUSE_WHEEL) {
    if (e.wheel.y != 0) Step(e.wheel.y > 0 ? -1 : 1);
  } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
    int w = 0, h = 0;
    SDL_GetRenderOutputSize(r_, &w, &h);
    float x = e.button.x / float(std::max(1, w));
    if (x < 0.3f) Step(-1);
    else if (x > 0.7f) Step(1);
    else if (!info_) play();
  }
  return Action::kNone;
}

Action Launcher::Update(SDL_Gamepad* pad, double dt) {
  time_ += dt;
  fade_in_ = std::min(1.0, fade_in_ + dt * 2);
  if (pad && launching_ < 0 && Count()) {
    const games::Game& g = (*games_)[size_t(selected_)];
    const bool options = info_ && !g.options.empty();
    float x = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f;
    float y = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f;
    if (stick_latch_ == 0 && std::fabs(x) > 0.5f) {
      stick_latch_ = x < 0 ? -1 : 1;
      options ? ChangeOption(stick_latch_) : Step(stick_latch_);
    } else if (std::fabs(x) < 0.3f) {
      stick_latch_ = 0;
    }
    if (stick_latch_y_ == 0 && std::fabs(y) > 0.5f) {
      stick_latch_y_ = y < 0 ? -1 : 1;
      if (options) option_row_ = std::clamp(option_row_ + stick_latch_y_, 0, int(g.options.size()) - 1);
    } else if (std::fabs(y) < 0.3f) {
      stick_latch_y_ = 0;
    }
  }
  scroll_ += (target_ - scroll_) * float(std::min(1.0, dt * 8));
  if (std::fabs(target_ - scroll_) < 0.001f) scroll_ = target_;
  if (Count() && std::fabs(target_) > 1000) {  // keep the numbers small
    float k = std::floor(target_ / float(Count())) * float(Count());
    target_ -= k, scroll_ -= k;
  }
  if (launching_ >= 0) {
    launching_ += dt;
    if (launching_ >= 0.75) return Action::kPlay;
  }
  return Action::kNone;
}

void Launcher::DrawText(const char* s, float x, float y, float scale, SDL_Color c, bool centred) {
  if (centred) x -= float(SDL_strlen(s)) * 8 * scale / 2;
  SDL_SetRenderScale(r_, scale, scale);
  SDL_SetRenderDrawColor(r_, c.r, c.g, c.b, c.a);
  SDL_RenderDebugText(r_, x / scale, y / scale, s);
  SDL_SetRenderScale(r_, 1, 1);
}

void Launcher::DrawDisk(int game, float w, float h) {
  Camera cam;
  cam.f = h * 1.45f, cam.cx = w / 2, cam.cy = h * 0.47f;
  float d = Offset(game);
  Place pl = PlaceAt(d);
  if (pl.vis <= 0) return;
  float sel = pl.sel;
  float side = std::clamp(d, -1.0f, 1.0f);
  float launch = launching_ >= 0 && game == selected_ ? Smooth(float(launching_ / 0.75)) : 0.0f;
  // Where it is: the picked one lifts, comes forward and turns to face us; the others lean away. All sway.
  V3 pos{pl.x * (1 - launch), 2 + 7 * sel + 1.5f * std::sin(float(time_) * 1.3f + game * 2.1f) - 25 * launch,
         pl.z + 14 * sel + 250 * launch * launch};
  float yaw = side * 0.42f * (1 - sel) + 0.07f * std::sin(float(time_) * 0.8f + game * 1.7f) + launch * 0.6f * side;
  float pitch = -0.10f * (1 - sel) + 0.03f * std::sin(float(time_) * 0.6f + game) - launch * 0.5f;
  auto world = [&](float x, float y, float z) { return RotY(RotX(V3{x, y, z}, pitch), yaw) + pos; };
  auto normal = [&](V3 n) { return Normalize(RotY(RotX(n, pitch), yaw)); };
  // The spotlight above, in front; how much of it this disk gets.
  float spot = (0.18f + 0.95f * sel) * pl.vis;
  V3 light_pos{pl.x * (1 - launch), 230, 140};
  auto lit = [&](V3 centre, V3 n, float shine) {
    V3 l = Normalize(light_pos - centre);
    float diff = std::max(0.0f, Dot(n, l));
    V3 view = Normalize(cam.eye - centre), half = Normalize(l + view);
    float spec = shine > 0 ? std::pow(std::max(0.0f, Dot(n, half)), 24.0f) * shine : 0;
    return 0.07f * pl.vis + (0.95f * diff + spec) * spot;
  };

  // The body: front, back, edges (culled to what faces us), then the parts on the front.
  struct Face {
    std::vector<SDL_FPoint> p;
    float depth;
    SDL_FColor c;
  };
  std::vector<Face> faces;
  auto add = [&](const std::vector<V3>& pts, V3 n_local, SDL_FColor base, float shine) {
    Face f;
    float dsum = 0;
    V3 centre{0, 0, 0};
    for (V3 p : pts) {
      float dep;
      f.p.push_back(cam.Project(p, &dep));
      dsum += dep;
      centre = centre + p;
    }
    if (ScreenArea(f.p.data(), int(f.p.size())) >= 0) return false;  // facing away
    centre = centre * (1.0f / float(pts.size()));
    f.depth = dsum / float(pts.size());
    f.c = Shade(base, lit(centre, normal(n_local), shine));
    faces.push_back(f);
    return true;
  };
  const SDL_FColor body = (*games_)[size_t(game)].body;
  std::vector<V3> front, back;
  for (int i = 0; i < 5; ++i) front.push_back(world(kOutline[i][0], kOutline[i][1], kThick / 2));
  for (int i = 4; i >= 0; --i) back.push_back(world(kOutline[i][0], kOutline[i][1], -kThick / 2));
  for (int i = 0; i < 5; ++i) {
    int j = (i + 1) % 5;
    float ex = kOutline[j][0] - kOutline[i][0], ey = kOutline[j][1] - kOutline[i][1];
    V3 n{ey, -ex, 0};
    add({world(kOutline[i][0], kOutline[i][1], kThick / 2), world(kOutline[i][0], kOutline[i][1], -kThick / 2),
         world(kOutline[j][0], kOutline[j][1], -kThick / 2), world(kOutline[j][0], kOutline[j][1], kThick / 2)},
        n, Shade(body, 0.8f), 0.2f);
  }
  add(back, {0, 0, -1}, body, 0.25f);
  bool front_visible = add(front, {0, 0, 1}, body, 0.25f);
  std::sort(faces.begin(), faces.end(), [](const Face& a, const Face& b) { return a.depth > b.depth; });
  for (const Face& f : faces) Polygon(r_, nullptr, f.p.data(), nullptr, int(f.p.size()), f.c);
  if (!front_visible) return;

  auto rect = [&](float x0, float y0, float x1, float y1, float z, SDL_FColor base, float shine, SDL_Texture* tex) {
    V3 q[4] = {world(x0, y0, z), world(x1, y0, z), world(x1, y1, z), world(x0, y1, z)};
    SDL_FPoint p[4];
    for (int i = 0; i < 4; ++i) p[i] = cam.Project(q[i]);
    SDL_FPoint uv[4] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
    V3 centre = (q[0] + q[2]) * 0.5f;
    Polygon(r_, tex, p, tex ? uv : nullptr, 4, Shade(base, lit(centre, normal({0, 0, 1}), shine)));
  };
  float z = kThick / 2 + 0.05f;
  rect(-38, -44, 38, 14, z, {1, 1, 1, 1}, 0.15f, labels_[size_t(game)]);  // the label
  rect(-24, 20, 26, 47, z + 0.02f, {0.74f, 0.76f, 0.80f, 1}, 0.9f, nullptr);  // the metal shutter
  rect(-10, 24, 4, 44, z + 0.04f, {0.05f, 0.05f, 0.07f, 1}, 0, nullptr);  // its window
  rect(-16, 22, -14, 46, z + 0.03f, {0.55f, 0.57f, 0.60f, 1}, 0.5f, nullptr);  // a groove
  rect(-42, -45, -36, -39, z, {0.02f, 0.02f, 0.02f, 1}, 0, nullptr);  // the write-protect hole
}

void Launcher::Render() {
  int iw = 0, ih = 0;
  SDL_GetRenderOutputSize(r_, &iw, &ih);
  float w = float(iw), h = float(ih);
  SDL_SetRenderDrawColor(r_, 0, 0, 0, 255);
  SDL_RenderClear(r_);
  float s = std::max(1.0f, std::floor(h / 300));  // the 8x8 font's scale
  if (Count() == 0) {
    DrawText("NO DISKS FOUND", w / 2, h * 0.45f, s * 1.5f, {255, 255, 255, 255}, true);
    DrawText("Put the disk images (.msa / .st) in the game folder next to the program.", w / 2, h * 0.52f, s,
             {150, 150, 165, 255}, true);
    return;
  }
  Camera cam;
  cam.f = h * 1.45f, cam.cx = w / 2, cam.cy = h * 0.47f;

  // Pure black, a pool of light under each disk and a soft bloom behind it (both stronger for the picked one).
  auto glow = [&](V3 centre, float rx, float ry, bool floor, SDL_FColor c) {
    std::vector<SDL_Vertex> v;
    v.push_back({cam.Project(centre), c, {0, 0}});
    const int n = 40;
    for (int i = 0; i <= n; ++i) {
      float a = i * 2 * kPi / n;
      V3 p = floor ? V3{centre.x + rx * std::cos(a), centre.y, centre.z + ry * std::sin(a)}
                   : V3{centre.x + rx * std::cos(a), centre.y + ry * std::sin(a), centre.z};
      v.push_back({cam.Project(p), {0, 0, 0, 1}, {0, 0}});
    }
    std::vector<int> idx;
    for (int i = 1; i <= n; ++i) idx.insert(idx.end(), {0, i, i + 1});
    SDL_RenderGeometry(r_, white_add_, v.data(), int(v.size()), idx.data(), int(idx.size()));
  };
  for (int g = 0; g < Count(); ++g) {
    Place pl = PlaceAt(Offset(g));
    if (pl.vis <= 0) continue;
    float launch = launching_ >= 0 && g == selected_ ? Smooth(float(launching_ / 0.75)) : 0.0f;
    float k = (0.12f + 0.88f * pl.sel) * (1 - launch) * pl.vis;
    float rx = 62 * (1 - 0.25f * (1 - pl.sel));
    glow({pl.x, kFloorY, pl.z + 10}, rx, 38, true, {0.30f * k, 0.30f * k, 0.34f * k, 1});
    float bloom = (0.012f + 0.088f * pl.sel) * (1 - launch) * pl.vis;  // a hint behind the others
    glow({pl.x, 2 + 7 * pl.sel, pl.z + 14 * pl.sel - 6}, 80, 82, false, {bloom, bloom, bloom * 1.1f, 1});
  }
  // The disks, back to front: the furthest from the middle first.
  std::vector<int> order;
  for (int g = 0; g < Count(); ++g) order.push_back(g);
  std::sort(order.begin(), order.end(),
            [&](int a, int b) { return std::fabs(Offset(a)) > std::fabs(Offset(b)); });
  for (int g : order) DrawDisk(g, w, h);

  // Words: the picked disk's name, where it is in the row.
  const games::Game& picked = (*games_)[size_t(selected_)];
  SDL_FPoint p = cam.Project({0, kFloorY - 8, 40});
  DrawText(picked.name.c_str(), p.x, p.y, s * 1.3f, {255, 255, 255, 255}, true);
  if (Count() > 1) {
    std::string pos = "< " + std::to_string(selected_ + 1) + " / " + std::to_string(Count()) + " >";
    DrawText(pos.c_str(), p.x, p.y + 14 * s, s * 0.9f, {90, 90, 100, 255}, true);
  }
  DrawText("LLAMASOFT ON THE ATARI ST", w / 2, h * 0.06f, s, {120, 120, 135, 255}, true);
  const char* hint = SDL_GetTicks() < quit_armed_ms_ ? "PRESS B AGAIN TO QUIT"
                     : !crt::Available() ? "LEFT/RIGHT CHOOSE    A PLAY    Y HOW TO PLAY    B QUIT"
                     : crt::Enabled() ? "LEFT/RIGHT CHOOSE   A PLAY   Y HOW TO PLAY   X CRT: ON   B QUIT"
                                      : "LEFT/RIGHT CHOOSE   A PLAY   Y HOW TO PLAY   X CRT: OFF   B QUIT";
  DrawText(hint, w / 2, h * 0.92f, s * 0.9f, {150, 150, 165, 255}, true);

  if (info_) {
    std::vector<std::string> lines = picked.help;
    int first_option = -1;
    if (!picked.options.empty()) {
      lines.push_back("");
      first_option = int(lines.size());
      for (size_t i = 0; i < picked.options.size(); ++i) {
        const games::Game::Option& o = picked.options[i];
        std::string label = o.label;
        label.resize(std::max<size_t>(label.size() + 2, 16), ' ');
        lines.push_back(label + "< " + o.values[size_t(games::OptionValue(picked, o.key))] + " >");
      }
      lines.push_back("UP/DOWN pick   LEFT/RIGHT change");
    }
    int n = int(lines.size()), longest = 0;
    for (const std::string& l : lines) longest = std::max(longest, int(l.size()));
    float ts = std::max(1.0f, std::floor(std::min(w * 0.85f / (longest * 8), h * 0.7f / (n * 12))));
    float pw = longest * 8 * ts + 48 * ts / 2, ph = n * 12 * ts + 40 * ts / 2;
    SDL_FRect panel{(w - pw) / 2, (h - ph) / 2, pw, ph};
    SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r_, 0, 0, 0, 225);
    SDL_RenderFillRect(r_, &panel);
    SDL_SetRenderDrawColorFloat(r_, picked.band.r, picked.band.g, picked.band.b, 1);
    SDL_RenderRect(r_, &panel);
    for (int i = 0; i < n; ++i) {
      bool option = first_option >= 0 && i >= first_option && i < first_option + int(picked.options.size());
      bool on = option && i - first_option == option_row_;
      SDL_Color c = i == 0 ? SDL_Color{255, 255, 255, 255}
                    : on   ? SDL_Color{uint8_t(picked.band.r * 255), uint8_t(picked.band.g * 255),
                               uint8_t(picked.band.b * 255), 255}
                    : option ? SDL_Color{150, 150, 160, 255}
                             : SDL_Color{200, 200, 210, 255};
      DrawText(lines[size_t(i)].c_str(), panel.x + 24 * ts / 2, panel.y + 20 * ts / 2 + i * 12 * ts, ts, c, false);
    }
  }

  // Fades: in at the start, out into the game.
  float black = float(1 - fade_in_);
  if (launching_ >= 0) black = std::max(black, Smooth(float((launching_ - 0.35) / 0.4)));
  if (black > 0) {
    SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r_, 0, 0, 0, uint8_t(255 * std::min(1.0f, black)));
    SDL_FRect all{0, 0, w, h};
    SDL_RenderFillRect(r_, &all);
  }
}

}  // namespace launcher
