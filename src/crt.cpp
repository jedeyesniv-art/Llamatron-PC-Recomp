#include "crt.h"

#include "log.h"
#include "settings.h"

namespace crt {
namespace {

#include "shaders/crt.dxil.h"

// The shader's cbuffer (six float4s, see crt.frag.hlsl).
struct Params {
  float src[4];
  float dst[4];
  float beam[4];    // scanline strength, beam width min, max, horizontal blur
  float glow[4];    // mask strength, mask bleed, halation, halation size
  float shape[4];   // curvature, vignette, colour overflow, brightness
  float colour[4];  // saturation, contrast, temperature
};

SDL_GPUShader* g_shader = nullptr;
SDL_GPURenderState* g_state = nullptr;
SDL_GPUDevice* g_device = nullptr;
bool g_on = true;
Params g_params{};

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
  info.num_samplers = 1;
  info.num_uniform_buffers = 1;
  g_shader = SDL_CreateGPUShader(g_device, &info);
  if (!g_shader) {
    LOG("crt: shader: %s", SDL_GetError());
    return false;
  }
  SDL_GPURenderStateCreateInfo state{};
  state.fragment_shader = g_shader;
  g_state = SDL_CreateGPURenderState(renderer, &state);
  if (!g_state) {
    LOG("crt: render state: %s", SDL_GetError());
    SDL_ReleaseGPUShader(g_device, g_shader), g_shader = nullptr;
    return false;
  }
  LoadSettings();
  LOG("crt shader ready, %s", g_on ? "on" : "off");
  return true;
}

void Shutdown() {
  if (g_state) SDL_DestroyGPURenderState(g_state), g_state = nullptr;
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

// Defaults from the owner's ShaderGlass profile (newgridbigger.sgp, crt-yah) where it has a matching number.
void LoadSettings() {
  auto get = [](const char* key, double def) { return float(settings::Get(key, def)); };
  g_on = get("crt", 1) != 0;
  Params& p = g_params;
  p.beam[0] = get("crt_scanlines", 0.35);
  p.beam[1] = get("crt_beam_min", 0.35);
  p.beam[2] = get("crt_beam_max", 1.0);
  p.beam[3] = get("crt_blur", 0.4);
  p.glow[0] = get("crt_mask", 0.35);
  p.glow[1] = get("crt_mask_bleed", 0.5);
  p.glow[2] = get("crt_halation", 0.2);
  p.glow[3] = get("crt_halation_size", 0.4);
  p.shape[0] = get("crt_curvature", 0.05);
  p.shape[1] = get("crt_vignette", 0.15);
  p.shape[2] = get("crt_overflow", 0.75);
  p.shape[3] = get("crt_brightness", -0.05);
  p.colour[0] = get("crt_saturation", 0.95);
  p.colour[1] = get("crt_contrast", -0.25);
  p.colour[2] = get("crt_temperature", 0.3);
}

void DrawPicture(SDL_Renderer* renderer, SDL_Texture* texture, const SDL_FRect& view) {
  if (!Enabled()) {
    SDL_RenderTexture(renderer, texture, nullptr, &view);
    return;
  }
  SDL_FRect out{};  // the picture's size on the screen, in pixels
  SDL_GetRenderLogicalPresentationRect(renderer, &out);
  float tw = 0, th = 0;
  SDL_GetTextureSize(texture, &tw, &th);
  Params& p = g_params;
  p.src[0] = tw, p.src[1] = th, p.src[2] = 1 / tw, p.src[3] = 1 / th;
  p.dst[0] = out.w, p.dst[1] = out.h;
  SDL_SetGPURenderStateFragmentUniforms(g_state, 0, &p, sizeof(p));
  SDL_SetGPURenderState(renderer, g_state);
  SDL_RenderTexture(renderer, texture, nullptr, &view);
  SDL_SetGPURenderState(renderer, nullptr);
}

}  // namespace crt
