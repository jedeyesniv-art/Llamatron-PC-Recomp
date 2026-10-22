#include "launcher.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

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

struct Disk {
  const char* name;
  SDL_FColor body;
  SDL_FColor band;  // the label's printed stripe
};
const Disk kDisks[2] = {
    {"LLAMATRON", {0.10f, 0.16f, 0.36f, 1}, {0.95f, 0.55f, 0.12f, 1}},
    {"SUPER GRIDRUNNER", {0.13f, 0.13f, 0.15f, 1}, {0.62f, 0.25f, 0.85f, 1}},
};

constexpr float kDiskX = 62, kFloorY = -60;

}  // namespace

Launcher::Launcher(SDL_Renderer* renderer) : r_(renderer) {
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
  for (SDL_Texture* t : {white_, white_add_, labels_[0], labels_[1]})
    if (t) SDL_DestroyTexture(t);
}

// The owner's hand-written labels (Tools/make_labels.py -> labels.inc): grey ink on white, unpacked to textures.
void Launcher::BuildLabels() {
  const unsigned char* packed[2] = {kLabel_llamatron, kLabel_gridrunner};
  const size_t sizes[2] = {sizeof(kLabel_llamatron), sizeof(kLabel_gridrunner)};
  std::vector<uint32_t> pixels(size_t(kLabelW) * kLabelH);
  for (int i = 0; i < 2; ++i) {
    size_t at = 0;
    for (size_t k = 0; k + 1 < sizes[i] && at < pixels.size(); k += 2) {
      uint32_t v = packed[i][k];
      uint32_t argb = 0xFF000000u | v << 16 | v << 8 | v;
      for (int n = 0; n < packed[i][k + 1] && at < pixels.size(); ++n) pixels[at++] = argb;
    }
    SDL_Surface* s = SDL_CreateSurfaceFrom(kLabelW, kLabelH, SDL_PIXELFORMAT_ARGB8888, pixels.data(), kLabelW * 4);
    labels_[i] = SDL_CreateTextureFromSurface(r_, s);
    SDL_DestroySurface(s);
    SDL_SetTextureScaleMode(labels_[i], SDL_SCALEMODE_LINEAR);
  }
}

void Launcher::Enter(int game) {
  selected_ = game;
  sel_anim_[0] = game == 0 ? 1.0f : 0.0f;
  sel_anim_[1] = 1 - sel_anim_[0];
  fade_in_ = 0;
  launching_ = -1;
  info_ = false;
  quit_armed_ms_ = 0;
}

void Launcher::Choose(int game) {
  if (game != selected_) info_ = false;
  selected_ = game;
}

Action Launcher::HandleEvent(const SDL_Event& e) {
  if (launching_ >= 0) return Action::kNone;
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
  if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
    switch (e.gbutton.button) {
      case SDL_GAMEPAD_BUTTON_DPAD_LEFT: Choose(0); break;
      case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: Choose(1); break;
      case SDL_GAMEPAD_BUTTON_SOUTH:
      case SDL_GAMEPAD_BUTTON_START: play(); break;
      case SDL_GAMEPAD_BUTTON_NORTH: info_ = !info_; break;
      case SDL_GAMEPAD_BUTTON_EAST:
      case SDL_GAMEPAD_BUTTON_BACK: return back();
      default: break;
    }
  } else if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
    switch (e.key.scancode) {
      case SDL_SCANCODE_LEFT: case SDL_SCANCODE_A: Choose(0); break;
      case SDL_SCANCODE_RIGHT: case SDL_SCANCODE_D: Choose(1); break;
      case SDL_SCANCODE_RETURN: case SDL_SCANCODE_SPACE: play(); break;
      case SDL_SCANCODE_F1: case SDL_SCANCODE_I: case SDL_SCANCODE_H: case SDL_SCANCODE_TAB: info_ = !info_; break;
      case SDL_SCANCODE_ESCAPE: return back();
      default: break;
    }
  } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
    int w = 0, h = 0;
    SDL_GetRenderOutputSize(r_, &w, &h);
    int game = e.button.x < w / 2.0f ? 0 : 1;
    if (game == selected_ && !info_) play();
    else Choose(game);
  }
  return Action::kNone;
}

Action Launcher::Update(SDL_Gamepad* pad, double dt) {
  time_ += dt;
  fade_in_ = std::min(1.0, fade_in_ + dt * 2);
  if (pad && launching_ < 0) {
    float x = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f;
    if (stick_latch_ == 0 && std::fabs(x) > 0.5f) stick_latch_ = x < 0 ? -1 : 1, Choose(x < 0 ? 0 : 1);
    else if (std::fabs(x) < 0.3f) stick_latch_ = 0;
  }
  for (int i = 0; i < 2; ++i) {
    float target = i == selected_ ? 1.0f : 0.0f;
    sel_anim_[i] += (target - sel_anim_[i]) * float(std::min(1.0, dt * 7));
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
  float sel = Smooth(sel_anim_[game]);
  float side = game == 0 ? -1.0f : 1.0f;
  float launch = launching_ >= 0 && game == selected_ ? Smooth(float(launching_ / 0.75)) : 0.0f;
  // Where it is: the picked one lifts, comes forward and turns to face us; the other leans away. Both sway.
  V3 pos{side * kDiskX * (1 - launch), 2 + 7 * sel + 1.5f * std::sin(float(time_) * 1.3f + game * 2.1f) - 25 * launch,
         14 * sel + 250 * launch * launch};
  float yaw = side * 0.42f * (1 - sel) + 0.07f * std::sin(float(time_) * 0.8f + game * 1.7f) + launch * 0.6f * side;
  float pitch = -0.10f * (1 - sel) + 0.03f * std::sin(float(time_) * 0.6f + game) - launch * 0.5f;
  auto world = [&](float x, float y, float z) { return RotY(RotX(V3{x, y, z}, pitch), yaw) + pos; };
  auto normal = [&](V3 n) { return Normalize(RotY(RotX(n, pitch), yaw)); };
  // The spotlight above, in front; how much of it this disk gets.
  float spot = 0.18f + 0.95f * sel;
  V3 light_pos{side * kDiskX * (1 - launch), 230, 140};
  auto lit = [&](V3 centre, V3 n, float shine) {
    V3 l = Normalize(light_pos - centre);
    float diff = std::max(0.0f, Dot(n, l));
    V3 view = Normalize(cam.eye - centre), half = Normalize(l + view);
    float spec = shine > 0 ? std::pow(std::max(0.0f, Dot(n, half)), 24.0f) * shine : 0;
    return 0.07f + (0.95f * diff + spec) * spot;
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
      float d;
      f.p.push_back(cam.Project(p, &d));
      dsum += d;
      centre = centre + p;
    }
    if (ScreenArea(f.p.data(), int(f.p.size())) >= 0) return false;  // facing away
    centre = centre * (1.0f / float(pts.size()));
    f.depth = dsum / float(pts.size());
    f.c = Shade(base, lit(centre, normal(n_local), shine));
    faces.push_back(f);
    return true;
  };
  const SDL_FColor body = kDisks[game].body;
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
  rect(-38, -44, 38, 14, z, {1, 1, 1, 1}, 0.15f, labels_[game]);  // the label
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
  for (int g = 0; g < 2; ++g) {
    float sel = Smooth(sel_anim_[g]);
    float launch = launching_ >= 0 && g == selected_ ? Smooth(float(launching_ / 0.75)) : 0.0f;
    float k = (0.12f + 0.88f * sel) * (1 - launch);
    float cx = (g == 0 ? -kDiskX : kDiskX);
    glow({cx, kFloorY, 10}, 62, 38, true, {0.30f * k, 0.30f * k, 0.34f * k, 1});
    float bloom = (0.012f + 0.088f * sel) * (1 - launch);  // a hint behind the other one
    glow({cx, 2 + 7 * sel, 14 * sel - 6}, 80, 82, false, {bloom, bloom, bloom * 1.1f, 1});
  }
  // The disks, the one further back first.
  int order[2] = {selected_ == 0 ? 1 : 0, selected_};
  for (int g : order) DrawDisk(g, w, h);

  // Words.
  float s = std::max(1.0f, std::floor(h / 300));  // the 8x8 font's scale
  for (int g = 0; g < 2; ++g) {
    SDL_FPoint p = cam.Project({g == 0 ? -kDiskX : kDiskX, kFloorY - 8, 40});
    bool on = g == selected_;
    DrawText(kDisks[g].name, p.x, p.y, s * (on ? 1.5f : 1.2f), on ? SDL_Color{255, 255, 255, 255}
                                                                   : SDL_Color{90, 90, 100, 255}, true);
  }
  DrawText("LLAMASOFT ON THE ATARI ST", w / 2, h * 0.06f, s, {120, 120, 135, 255}, true);
  const char* hint = SDL_GetTicks() < quit_armed_ms_ ? "PRESS B AGAIN TO QUIT"
                                                      : "LEFT/RIGHT CHOOSE    A PLAY    Y HOW TO PLAY    B QUIT";
  DrawText(hint, w / 2, h * 0.92f, s * 0.9f, {150, 150, 165, 255}, true);

  if (info_) {
    static const char* const kInfo[2][16] = {
        {"LLAMATRON  -  JEFF MINTER, 1991", "",
         "Rescue the beasties. Kill everything else.", "",
         "LEFT STICK / D-PAD   move",
         "RIGHT STICK          aim (twin-stick game)",
         "A AT THE TITLE       start a twin-stick game",
         "X AT THE TITLE       start a one-stick game",
         "A IN PLAY            fire / lock the fire angle",
         "RB AT THE TITLE      analog aim on / off",
         "BACK                 pause    (Y: the tentacle)",
         "B ON THE PAUSE SCREEN  quit to the title",
         "B TWICE AT THE TITLE   back to the disks", "",
         "Keyboard: WASD + Space, cursors + Enter, P pause", nullptr},
        {"SUPER GRIDRUNNER  -  JEFF MINTER", "",
         "Shoot the snakes. Mind the zappers.", "",
         "LEFT STICK           move the ship (d-pad: slowly)",
         "RT OR A              fire",
         "LT (HOLD)            nosecone back to the ship",
         "RIGHT STICK FLICK    nosecone shot direction",
         "RB                   voluntary martyrdom (a life!)",
         "BACK                 pause",
         "B ON THE PAUSE SCREEN  quit to the title",
         "TITLE: X OPTIONS   Y GAME SECRETS   LB MORE INFO",
         "B TWICE AT THE TITLE   back to the disks", "",
         "The mouse works too: the original controls.", nullptr}};
    const char* const* lines = kInfo[selected_];
    int n = 0, longest = 0;
    for (; lines[n]; ++n) longest = std::max(longest, int(SDL_strlen(lines[n])));
    float ts = std::max(1.0f, std::floor(std::min(w * 0.85f / (longest * 8), h * 0.7f / (n * 12))));
    float pw = longest * 8 * ts + 48 * ts / 2, ph = n * 12 * ts + 40 * ts / 2;
    SDL_FRect panel{(w - pw) / 2, (h - ph) / 2, pw, ph};
    SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r_, 0, 0, 0, 225);
    SDL_RenderFillRect(r_, &panel);
    SDL_SetRenderDrawColorFloat(r_, kDisks[selected_].band.r, kDisks[selected_].band.g, kDisks[selected_].band.b, 1);
    SDL_RenderRect(r_, &panel);
    for (int i = 0; i < n; ++i)
      DrawText(lines[i], panel.x + 24 * ts / 2, panel.y + 20 * ts / 2 + i * 12 * ts, ts,
               i == 0 ? SDL_Color{255, 255, 255, 255} : SDL_Color{200, 200, 210, 255}, false);
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
