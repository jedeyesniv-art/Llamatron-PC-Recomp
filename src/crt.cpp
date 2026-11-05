#include "crt.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "log.h"
#include "settings.h"

namespace crt {
namespace {

#include "shaders/crt.dxil.h"
#include "shaders/glow.dxil.h"
#include "shaders/psy_trail.dxil.h"
#include "shaders/psy_mix.dxil.h"

// The shader's cbuffer (six float4s, see crt.frag.hlsl).
struct Params {
  float src[4];
  float dst[4];
  float beam[4];    // scanline strength, beam width min, max, horizontal blur
  float glow[4];    // mask strength, mask bleed, halation, halation size
  float shape[4];   // curvature, 0, colour overflow, brightness
  float colour[4];  // saturation, contrast, temperature
};

SDL_GPUShader* g_shader = nullptr;
SDL_GPURenderState* g_state = nullptr;
SDL_GPUDevice* g_device = nullptr;
int g_mode = 1;  // 0 off, 1 CRT+, 2 CRT++ (setting crt, or <game>_crt)
std::string g_game;  // SetGame

// A setting the running game can have its own of: its key if it has one, else the shared key.
std::string GameKey(const char* key) {
  if (g_game.empty()) return key;
  std::string own = g_game + "_" + key;
  return settings::Has(own) ? own : std::string(key);
}
Params g_params{};

// The halation's glow, rebuilt every frame (BuildGlow): the picture as linear light at 320 x 200 (level 0; from the
// remaster's 1280 x 800 through 640 x 400), then 160 x 100 ... 10 x 7, each a smooth blur of the one above. Levels
// 1-5 are bound to the shader. Float textures where the renderer has them (a faint tail in 8 bits would band).
constexpr int kLevels = 6;
constexpr int kLevelW[kLevels] = {320, 160, 80, 40, 20, 10}, kLevelH[kLevels] = {200, 100, 50, 25, 13, 7};
SDL_Texture* g_level[kLevels] = {};
SDL_Texture* g_mid = nullptr;  // 640 x 400
SDL_GPUSampler* g_sampler = nullptr;
SDL_GPUShader* g_lin_shader = nullptr;  // glow.frag.hlsl: level 0 drawn as linear light
SDL_GPURenderState* g_lin_state = nullptr;

// CRT++ (2026-11-05, owner: "lurid and overwhelming", after Minter's later games): every frame shown, the
// picture copied to 1280 x 800 (g_cur), the trails fed back (psy_trail: the last trail zoomed / turned about the
// centre, faded, tinted, this frame's bright parts on top; two textures in turn), then the picture with its
// colour bled and the trails screened over it (psy_mix -> g_out), which the CRT shader draws (glow and
// saturation boosted). Fades, zoom and colour turns are per second, so any refresh rate looks the same.
struct Psy {
  float length = 0.4f, trails = 0.9f, zoom = 0.25f, twist = 0, hue = 0.66f, tint = 0.3f, cycle = 0, threshold = 0.1f;
  float gain = 1, glow = 4, bleed = 0.5f, saturation = 1.3f, keep = 1;
} g_psy;
constexpr int kPsyW = 1280, kPsyH = 800;
SDL_Texture* g_cur = nullptr;
SDL_Texture* g_trail[2] = {};
SDL_Texture* g_out = nullptr;
int g_trail_at = 0;  // the newest trail
uint64_t g_psy_last = 0;
SDL_GPUShader* g_trail_shader = nullptr;
SDL_GPUShader* g_mix_shader = nullptr;
SDL_GPURenderState* g_trail_state = nullptr;
SDL_GPURenderState* g_mix_state = nullptr;

SDL_Texture* GlowTexture(SDL_Renderer* r, int w, int h) {
  SDL_Texture* t = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA64_FLOAT, SDL_TEXTUREACCESS_TARGET, w, h);
  if (!t) t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
  if (t) SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR), SDL_SetTextureBlendMode(t, SDL_BLENDMODE_NONE);
  return t;
}

void FreePsy() {
  for (SDL_Texture** t : {&g_cur, &g_trail[0], &g_trail[1], &g_out})
    if (*t) SDL_DestroyTexture(*t), *t = nullptr;
  if (g_trail_state) SDL_DestroyGPURenderState(g_trail_state), g_trail_state = nullptr;
  if (g_mix_state) SDL_DestroyGPURenderState(g_mix_state), g_mix_state = nullptr;
  if (g_trail_shader) SDL_ReleaseGPUShader(g_device, g_trail_shader), g_trail_shader = nullptr;
  if (g_mix_shader) SDL_ReleaseGPUShader(g_device, g_mix_shader), g_mix_shader = nullptr;
}

void FreeGlow() {
  for (SDL_Texture*& t : g_level)
    if (t) SDL_DestroyTexture(t), t = nullptr;
  if (g_mid) SDL_DestroyTexture(g_mid), g_mid = nullptr;
  if (g_sampler) SDL_ReleaseGPUSampler(g_device, g_sampler), g_sampler = nullptr;
  if (g_lin_state) SDL_DestroyGPURenderState(g_lin_state), g_lin_state = nullptr;
  if (g_lin_shader) SDL_ReleaseGPUShader(g_device, g_lin_shader), g_lin_shader = nullptr;
}

bool MakeGlow(SDL_Renderer* r) {
  for (int i = 0; i < kLevels; ++i)
    if (!(g_level[i] = GlowTexture(r, kLevelW[i], kLevelH[i]))) return false;
  if (!(g_mid = GlowTexture(r, 640, 400))) return false;
  SDL_GPUSamplerCreateInfo si{};
  si.min_filter = si.mag_filter = SDL_GPU_FILTER_LINEAR;
  si.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
  si.address_mode_u = si.address_mode_v = si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  g_sampler = SDL_CreateGPUSampler(g_device, &si);
  if (!g_sampler) return false;
  SDL_GPUShaderCreateInfo info{};
  info.code = k_glow_dxil;
  info.code_size = sizeof(k_glow_dxil);
  info.entrypoint = "main";
  info.format = SDL_GPU_SHADERFORMAT_DXIL;
  info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
  info.num_samplers = 1;
  if (!(g_lin_shader = SDL_CreateGPUShader(g_device, &info))) return false;
  SDL_GPURenderStateCreateInfo state{};
  state.fragment_shader = g_lin_shader;
  g_lin_state = SDL_CreateGPURenderState(r, &state);
  return g_lin_state != nullptr;
}

SDL_GPUTexture* GpuTexture(SDL_Texture* t) {
  return static_cast<SDL_GPUTexture*>(
      SDL_GetPointerProperty(SDL_GetTextureProperties(t), SDL_PROP_TEXTURE_GPU_TEXTURE_POINTER, nullptr));
}

// A fragment shader taking the drawn texture + g_cur (and uniforms), as a render state.
SDL_GPURenderState* PsyState(SDL_Renderer* r, const unsigned char* code, size_t size, SDL_GPUShader** shader) {
  SDL_GPUShaderCreateInfo info{};
  info.code = code;
  info.code_size = size;
  info.entrypoint = "main";
  info.format = SDL_GPU_SHADERFORMAT_DXIL;
  info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
  info.num_samplers = 2;
  info.num_uniform_buffers = 1;
  if (!(*shader = SDL_CreateGPUShader(g_device, &info))) return nullptr;
  SDL_GPUTextureSamplerBinding bind{GpuTexture(g_cur), g_sampler};
  SDL_GPURenderStateCreateInfo state{};
  state.fragment_shader = *shader;
  state.num_sampler_bindings = 1;
  state.sampler_bindings = &bind;
  return SDL_CreateGPURenderState(r, &state);
}

bool MakePsy(SDL_Renderer* r) {
  if (!(g_cur = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, kPsyW, kPsyH))) return false;
  SDL_SetTextureBlendMode(g_cur, SDL_BLENDMODE_NONE);
  for (SDL_Texture** t : {&g_trail[0], &g_trail[1], &g_out})
    if (!(*t = GlowTexture(r, kPsyW, kPsyH))) return false;
  g_trail_state = PsyState(r, k_psy_trail_dxil, sizeof(k_psy_trail_dxil), &g_trail_shader);
  g_mix_state = PsyState(r, k_psy_mix_dxil, sizeof(k_psy_mix_dxil), &g_mix_shader);
  return g_trail_state && g_mix_state;
}

// Draws `from` over the whole of the current target.
void Blit(SDL_Renderer* r, SDL_Texture* from) {
  SDL_RenderTexture(r, from, nullptr, nullptr);
}

void BuildGlow(SDL_Renderer* r, SDL_Texture* picture) {
  SDL_Texture* old_target = SDL_GetRenderTarget(r);
  int lw = 0, lh = 0;
  SDL_RendererLogicalPresentation mode = SDL_LOGICAL_PRESENTATION_DISABLED;
  SDL_GetRenderLogicalPresentation(r, &lw, &lh, &mode);
  SDL_ScaleMode scale = SDL_SCALEMODE_NEAREST;
  SDL_BlendMode blend = SDL_BLENDMODE_NONE;
  SDL_GetTextureScaleMode(picture, &scale);
  SDL_GetTextureBlendMode(picture, &blend);
  SDL_SetTextureScaleMode(picture, SDL_SCALEMODE_LINEAR);
  SDL_SetTextureBlendMode(picture, SDL_BLENDMODE_NONE);
  auto target = [r](SDL_Texture* t) {
    SDL_SetRenderTarget(r, t);
    SDL_SetRenderLogicalPresentation(r, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
  };
  float pw = 0, ph = 0;
  SDL_GetTextureSize(picture, &pw, &ph);
  SDL_Texture* from = picture;
  if (pw >= 1280) target(g_mid), Blit(r, picture), from = g_mid;  // 4x -> 2x: each bilinear tap a 2 x 2 average
  target(g_level[0]);
  SDL_SetGPURenderState(r, g_lin_state);  // as linear light from here on
  Blit(r, from);
  SDL_SetGPURenderState(r, nullptr);
  // Each level from the one above: four quarter-strength copies half a pixel apart, added (a smooth kernel).
  for (int i = 1; i < kLevels; ++i) {
    SDL_Texture* prev = g_level[i - 1];
    target(g_level[i]);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    SDL_SetTextureBlendMode(prev, SDL_BLENDMODE_ADD);
    SDL_SetTextureColorModFloat(prev, 0.25f, 0.25f, 0.25f);
    const float w = float(kLevelW[i]), h = float(kLevelH[i]);
    for (int k = 0; k < 4; ++k) {
      SDL_FRect d{(k & 1) ? 0.5f : -0.5f, (k & 2) ? 0.5f : -0.5f, w, h};
      SDL_RenderTexture(r, prev, nullptr, &d);
    }
    SDL_SetTextureColorModFloat(prev, 1, 1, 1);
    SDL_SetTextureBlendMode(prev, SDL_BLENDMODE_NONE);
  }
  SDL_SetRenderTarget(r, old_target);
  SDL_SetRenderLogicalPresentation(r, lw, lh, mode);
  SDL_SetTextureScaleMode(picture, scale);
  SDL_SetTextureBlendMode(picture, blend);
}

// CRT++: the picture through the trails and the bleed into g_out (see Psy).
void RunPsy(SDL_Renderer* r, SDL_Texture* picture) {
  const uint64_t now = SDL_GetTicksNS();
  const float dt = g_psy_last ? std::clamp(float(double(now - g_psy_last) / 1e9), 0.0f, 0.1f) : 0.02f;
  g_psy_last = now;
  SDL_Texture* old_target = SDL_GetRenderTarget(r);
  int lw = 0, lh = 0;
  SDL_RendererLogicalPresentation mode = SDL_LOGICAL_PRESENTATION_DISABLED;
  SDL_GetRenderLogicalPresentation(r, &lw, &lh, &mode);
  auto target = [r](SDL_Texture* t) {
    SDL_SetRenderTarget(r, t);
    SDL_SetRenderLogicalPresentation(r, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
  };
  // the picture at 1280 x 800 (an ST-sized one in whole pixels)
  SDL_ScaleMode scale = SDL_SCALEMODE_NEAREST;
  SDL_BlendMode blend = SDL_BLENDMODE_NONE;
  SDL_GetTextureScaleMode(picture, &scale);
  SDL_GetTextureBlendMode(picture, &blend);
  float pw = 0, ph = 0;
  SDL_GetTextureSize(picture, &pw, &ph);
  SDL_SetTextureScaleMode(picture, pw >= kPsyW ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
  SDL_SetTextureBlendMode(picture, SDL_BLENDMODE_NONE);
  target(g_cur);
  Blit(r, picture);
  SDL_SetTextureScaleMode(picture, scale);
  SDL_SetTextureBlendMode(picture, blend);

  const Psy& p = g_psy;
  struct {
    float move[4], tint[4], hue[4];
  } tu{};
  tu.move[0] = std::exp(-p.zoom * dt);
  tu.move[1] = p.twist * dt;
  tu.move[2] = p.length > 0 ? std::pow(0.5f, dt / p.length) : 0.0f;
  tu.move[3] = p.threshold;
  // the tint colour from its hue (fully saturated); its amount is given per 50 Hz frame
  const float h6 = p.hue * 6;
  auto chan = [h6](float o) { return std::clamp(std::fabs(std::fmod(h6 + o, 6.0f) - 3) - 1, 0.0f, 1.0f); };
  tu.tint[0] = chan(0), tu.tint[1] = chan(4), tu.tint[2] = chan(2);
  tu.tint[3] = 1 - std::pow(1 - std::clamp(p.tint, 0.0f, 0.999f), dt * 50);
  const float turn = 6.2831853f * p.cycle * dt;
  tu.hue[0] = std::cos(turn), tu.hue[1] = std::sin(turn), tu.hue[2] = float(kPsyW) / kPsyH * 0.92f, tu.hue[3] = p.gain;
  SDL_Texture* prev = g_trail[g_trail_at];
  g_trail_at ^= 1;
  target(g_trail[g_trail_at]);
  SDL_SetGPURenderStateFragmentUniforms(g_trail_state, 0, &tu, sizeof(tu));
  SDL_SetGPURenderState(r, g_trail_state);
  Blit(r, prev);
  SDL_SetGPURenderState(r, nullptr);

  float mu[4] = {p.trails, p.bleed * 10.0f / kPsyW, p.keep, 0};
  target(g_out);
  SDL_SetGPURenderStateFragmentUniforms(g_mix_state, 0, mu, sizeof(mu));
  SDL_SetGPURenderState(r, g_mix_state);
  Blit(r, g_trail[g_trail_at]);
  SDL_SetGPURenderState(r, nullptr);
  SDL_SetRenderTarget(r, old_target);
  SDL_SetRenderLogicalPresentation(r, lw, lh, mode);
}

}  // namespace

SDL_Renderer* CreateRenderer(SDL_Window* window) {
  SDL_PropertiesID props = SDL_CreateProperties();
  SDL_SetPointerProperty(props, SDL_PROP_RENDERER_CREATE_WINDOW_POINTER, window);
  const char* name = SDL_getenv("LLAMATRON_RENDERER");  // tests: another renderer (direct3d11, ...)
  SDL_SetStringProperty(props, SDL_PROP_RENDERER_CREATE_NAME_STRING, name ? name : "gpu");
  SDL_SetBooleanProperty(props, SDL_PROP_RENDERER_CREATE_GPU_SHADERS_DXIL_BOOLEAN, true);
  SDL_Renderer* r = SDL_CreateRendererWithProperties(props);
  SDL_DestroyProperties(props);
  if (!r) {
    LOG("crt: no GPU renderer (%s), using the default one", SDL_GetError());
    r = SDL_CreateRenderer(window, nullptr);
  }
  if (r) LOG("renderer %s", SDL_GetRendererName(r));
  return r;
}

bool Init(SDL_Renderer* renderer) {
  g_device = SDL_GetGPURendererDevice(renderer);
  if (!g_device) return false;
  if (!(SDL_GetGPUShaderFormats(g_device) & SDL_GPU_SHADERFORMAT_DXIL)) {
    LOG("crt: the GPU device takes no DXIL");
    return false;
  }
  SDL_GPUShaderCreateInfo info{};
  info.code = k_crt_dxil;
  info.code_size = sizeof(k_crt_dxil);
  info.entrypoint = "main";
  info.format = SDL_GPU_SHADERFORMAT_DXIL;
  info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
  info.num_samplers = 1 + (kLevels - 1);
  info.num_uniform_buffers = 1;
  g_shader = SDL_CreateGPUShader(g_device, &info);
  if (!g_shader) {
    LOG("crt: shader: %s", SDL_GetError());
    return false;
  }
  if (!MakeGlow(renderer)) {
    LOG("crt: glow textures: %s", SDL_GetError());
    FreeGlow();
    SDL_ReleaseGPUShader(g_device, g_shader), g_shader = nullptr;
    return false;
  }
  if (!MakePsy(renderer)) {
    LOG("crt: CRT++ unavailable: %s", SDL_GetError());
    FreePsy();
  }
  SDL_GPUTextureSamplerBinding bind[kLevels - 1];
  for (int i = 1; i < kLevels; ++i) {
    bind[i - 1].texture = static_cast<SDL_GPUTexture*>(
        SDL_GetPointerProperty(SDL_GetTextureProperties(g_level[i]), SDL_PROP_TEXTURE_GPU_TEXTURE_POINTER, nullptr));
    bind[i - 1].sampler = g_sampler;
  }
  SDL_GPURenderStateCreateInfo state{};
  state.fragment_shader = g_shader;
  state.num_sampler_bindings = kLevels - 1;
  state.sampler_bindings = bind;
  g_state = SDL_CreateGPURenderState(renderer, &state);
  if (!g_state) {
    LOG("crt: render state: %s", SDL_GetError());
    FreeGlow();
    SDL_ReleaseGPUShader(g_device, g_shader), g_shader = nullptr;
    return false;
  }
  LOG("crt: glow levels %s", SDL_GetTextureProperties(g_level[1]) &&
                                     SDL_GetNumberProperty(SDL_GetTextureProperties(g_level[1]),
                                                           SDL_PROP_TEXTURE_FORMAT_NUMBER, 0) == SDL_PIXELFORMAT_RGBA64_FLOAT
                                 ? "in float"
                                 : "in 8 bits");
  LoadSettings();
  LOG("crt shader ready, %s", g_mode == 2 ? "CRT++" : g_mode ? "on" : "off");
  return true;
}

void Shutdown() {
  if (g_state) SDL_DestroyGPURenderState(g_state), g_state = nullptr;
  FreePsy();
  FreeGlow();
  if (g_shader) SDL_ReleaseGPUShader(g_device, g_shader), g_shader = nullptr;
}

bool Available() { return g_state != nullptr; }
bool Enabled() { return g_state && g_mode; }
int Mode() { return !g_state ? 0 : g_mode == 2 && !g_mix_state ? 1 : g_mode; }

void Toggle() {
  g_mode = (g_mode + 1) % (g_mix_state ? 3 : 2);
  settings::Set(g_game.empty() ? std::string("crt") : g_game + "_crt", g_mode);
  settings::Save();
  LOG("crt %s", g_mode == 2 ? "CRT++" : g_mode ? "on" : "off");
}

// Defaults from the owner's ShaderGlass profile (newgridbigger.sgp, crt-yah) where it has a matching number, then
// the owner's changes (2026-10-22): no curvature (the picture keeps its size), no vignette, true blacks with more
// contrast, halation 90% less; version 3 = the owner's own tuning; version 4 (2026-11-03) = the owner's tuning of
// the new halation (a smooth glow from blurred levels, a long faint tail). crt_defaults = the version of these
// defaults: older files get them once.
void LoadSettings() {
  constexpr int kDefaults = 4;
  bool reset = settings::Get("crt_defaults", 1) < kDefaults;
  auto get = [reset](const char* key, double def) {
    if (reset) settings::Set(key, def);
    return float(settings::Get(key, def));
  };
  g_mode = std::clamp(int(settings::Get(GameKey("crt"), 1)), 0, 2);
  Params& p = g_params;
  p.beam[0] = get("crt_scanlines", 0.45);
  p.beam[1] = get("crt_beam_min", 0.35);
  p.beam[2] = get("crt_beam_max", 1.0);
  p.beam[3] = get("crt_blur", 0.4);
  p.glow[0] = get("crt_mask", 0.35);
  p.glow[1] = get("crt_mask_bleed", 0.5);
  p.glow[2] = get("crt_halation", 0.04);
  p.glow[3] = get("crt_halation_size", 0.98);
  p.shape[0] = get("crt_curvature", 0.02);
  p.shape[2] = get("crt_overflow", 0.85);
  p.shape[3] = get("crt_brightness", 0.01);
  p.colour[0] = get("crt_saturation", 0.95);
  p.colour[1] = get("crt_contrast", 0.35);
  p.colour[2] = get("crt_temperature", 0.3);
  if (reset) {
    settings::Erase("crt_vignette");  // gone (version 2)
    settings::Set("crt_defaults", kDefaults);
    settings::Save();
    LOG("crt settings: new defaults (version %d)", kDefaults);
  }
  // CRT++: its own defaults version (and its own reset in the panel)
  constexpr int kPsyDefaults = 1;
  const bool psy_reset = settings::Get("crt_psy_defaults", 0) < kPsyDefaults;
  auto psy = [psy_reset](const char* key, double def) {
    if (psy_reset) settings::Set(key, def);
    return float(settings::Get(GameKey(key), def));
  };
  Psy& q = g_psy;
  q.length = psy("crt_psy_length", 0.4);
  q.trails = psy("crt_psy_trails", 0.9);
  q.zoom = psy("crt_psy_zoom", 0.25);
  q.twist = psy("crt_psy_twist", 0);
  q.hue = psy("crt_psy_hue", 0.66);
  q.tint = psy("crt_psy_tint", 0.3);
  q.cycle = psy("crt_psy_cycle", 0);
  q.threshold = psy("crt_psy_threshold", 0.1);
  q.gain = psy("crt_psy_gain", 1);
  q.glow = psy("crt_psy_glow", 4);
  q.bleed = psy("crt_psy_bleed", 0.5);
  q.saturation = psy("crt_psy_saturation", 1.3);
  q.keep = psy("crt_psy_keep", 1);
  if (psy_reset) {
    settings::Set("crt_psy_defaults", kPsyDefaults);
    settings::Save();
  }
}

void SetGame(const std::string& id) {
  if (id == g_game) return;
  g_game = id;
  LoadSettings();
  LOG("crt: %s look: %s", id.empty() ? "the shared" : id.c_str(),
      g_mode == 2 ? "CRT++" : g_mode ? "CRT+" : "off");
}

void DrawPicture(SDL_Renderer* renderer, SDL_Texture* texture, const SDL_FRect& view, float src_w, float src_h) {
  if (!Enabled()) {
    SDL_RenderTexture(renderer, texture, nullptr, &view);
    return;
  }
  SDL_FRect out{};  // the picture's size on the screen, in pixels
  SDL_GetRenderLogicalPresentationRect(renderer, &out);
  float tw = 0, th = 0;
  SDL_GetTextureSize(texture, &tw, &th);
  const bool psy = Mode() == 2;
  if (psy) {
    // the trails and the bleed into g_out, which the CRT draws as a supersampled picture of the ST's size
    if (src_w <= 0 || src_h <= 0) src_w = tw, src_h = th;
    RunPsy(renderer, texture);
    texture = g_out, tw = kPsyW, th = kPsyH;
  }
  Params& p0 = g_params;
  p0.colour[3] = 0;
  if (src_w > 0 && src_h > 0 && (src_w != tw || src_h != th)) tw = src_w, th = src_h, p0.colour[3] = 1;
  Params& p = g_params;
  p.src[0] = tw, p.src[1] = th, p.src[2] = 1 / tw, p.src[3] = 1 / th;
  p.dst[0] = out.w, p.dst[1] = out.h;
  Params sent = p;
  if (psy) sent.glow[2] *= g_psy.glow, sent.colour[0] *= g_psy.saturation;
  if (sent.glow[2] > 0) BuildGlow(renderer, texture);
  SDL_SetGPURenderStateFragmentUniforms(g_state, 0, &sent, sizeof(sent));
  SDL_SetGPURenderState(renderer, g_state);
  SDL_RenderTexture(renderer, texture, nullptr, &view);
  SDL_SetGPURenderState(renderer, nullptr);
}

}  // namespace crt
