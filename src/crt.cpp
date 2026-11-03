#include "crt.h"

#include "log.h"
#include "settings.h"

namespace crt {
namespace {

#include "shaders/crt.dxil.h"
#include "shaders/glow.dxil.h"

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
bool g_on = true;
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

SDL_Texture* GlowTexture(SDL_Renderer* r, int w, int h) {
  SDL_Texture* t = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA64_FLOAT, SDL_TEXTUREACCESS_TARGET, w, h);
  if (!t) t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
  if (t) SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR), SDL_SetTextureBlendMode(t, SDL_BLENDMODE_NONE);
  return t;
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
  LOG("crt shader ready, %s", g_on ? "on" : "off");
  return true;
}

void Shutdown() {
  if (g_state) SDL_DestroyGPURenderState(g_state), g_state = nullptr;
  FreeGlow();
  if (g_shader) SDL_ReleaseGPUShader(g_device, g_shader), g_shader = nullptr;
}

bool Available() { return g_state != nullptr; }
bool Enabled() { return g_state && g_on; }

void Toggle() {
  g_on = !g_on;
  settings::Set("crt", g_on ? 1 : 0);
  settings::Save();
  LOG("crt %s", g_on ? "on" : "off");
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
  g_on = settings::Get("crt", 1) != 0;
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
  Params& p0 = g_params;
  p0.colour[3] = 0;
  if (src_w > 0 && src_h > 0 && (src_w != tw || src_h != th)) tw = src_w, th = src_h, p0.colour[3] = 1;
  Params& p = g_params;
  p.src[0] = tw, p.src[1] = th, p.src[2] = 1 / tw, p.src[3] = 1 / th;
  p.dst[0] = out.w, p.dst[1] = out.h;
  if (p.glow[2] > 0) BuildGlow(renderer, texture);
  SDL_SetGPURenderStateFragmentUniforms(g_state, 0, &p, sizeof(p));
  SDL_SetGPURenderState(renderer, g_state);
  SDL_RenderTexture(renderer, texture, nullptr, &view);
  SDL_SetGPURenderState(renderer, nullptr);
}

}  // namespace crt
