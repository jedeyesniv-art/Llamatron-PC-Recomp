#version 450
// CRT++, step 1: the feedback trails (at 4x the source). Every frame the last trail (TrailFeedback) is zoomed and
// turned about the centre, faded, tinted and hue-turned, and this frame's bright parts go in on top - Jeff Minter's
// feedback loop. Every rate is per second: FrameTimeDelta where the host gives it (RetroArch), else LL_PSY_FPS.

#include "params-crt.inc"
#include "params-psy.inc"

layout(std140, set = 0, binding = 0) uniform UBO {
  mat4 MVP;
  vec4 OutputSize;
  vec4 OriginalSize;
  vec4 FinalViewportSize;
  uint FrameTimeDelta;
  float LL_PSY_LENGTH, LL_PSY_ZOOM, LL_PSY_TWIST, LL_PSY_CENTRE_X, LL_PSY_CENTRE_Y, LL_PSY_HUE, LL_PSY_TINT;
  float LL_PSY_CYCLE, LL_PSY_THRESHOLD, LL_PSY_GAIN, LL_PSY_FPS;
} global;

#pragma format R16G16B16A16_SFLOAT

#pragma stage vertex
layout(location = 0) in vec4 Position;
layout(location = 1) in vec2 TexCoord;
layout(location = 0) out vec2 vTexCoord;
void main() {
  gl_Position = global.MVP * Position;
  vTexCoord = TexCoord;
}

#pragma stage fragment
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;
layout(set = 0, binding = 2) uniform sampler2D Original;
layout(set = 0, binding = 3) uniform sampler2D TrailFeedback;

// The source picture as if scaled up in whole pixels to this pass's size, then read bilinearly.
vec3 Picture(vec2 uv) {
  vec2 grid = global.OutputSize.xy, src = global.OriginalSize.xy;
  vec2 t = uv * grid - 0.5, f = fract(t), i0 = floor(t);
  ivec2 hi = ivec2(src) - 1;
  vec3 c00 = texelFetch(Original, clamp(ivec2(floor((i0 + vec2(0.5, 0.5)) * src / grid)), ivec2(0), hi), 0).rgb;
  vec3 c10 = texelFetch(Original, clamp(ivec2(floor((i0 + vec2(1.5, 0.5)) * src / grid)), ivec2(0), hi), 0).rgb;
  vec3 c01 = texelFetch(Original, clamp(ivec2(floor((i0 + vec2(0.5, 1.5)) * src / grid)), ivec2(0), hi), 0).rgb;
  vec3 c11 = texelFetch(Original, clamp(ivec2(floor((i0 + vec2(1.5, 1.5)) * src / grid)), ivec2(0), hi), 0).rgb;
  return mix(mix(c00, c10, f.x), mix(c01, c11, f.x), f.y);
}

float Channel(float h6, float o) { return clamp(abs(mod(h6 + o, 6.0) - 3.0) - 1.0, 0.0, 1.0); }
vec3 HueColour(float h) { return vec3(Channel(h * 6.0, 0.0), Channel(h * 6.0, 4.0), Channel(h * 6.0, 2.0)); }

vec3 HueTurn(vec3 c, float cs, float sn) {  // rotation about the grey axis
  const vec3 k = vec3(0.57735);
  return c * cs + cross(k, c) * sn + k * dot(k, c) * (1.0 - cs);
}

void main() {
  float dt = global.FrameTimeDelta > 0u ? float(global.FrameTimeDelta) * 1e-6 : 1.0 / max(global.LL_PSY_FPS, 1.0);
  dt = clamp(dt, 0.0, 0.1);
  float zoom = exp(-global.LL_PSY_ZOOM * dt), turn = global.LL_PSY_TWIST * dt;
  float fade = global.LL_PSY_LENGTH > 0.0 ? pow(0.5, dt / global.LL_PSY_LENGTH) : 0.0;
  float aspect = global.FinalViewportSize.x > 0.0 ? global.FinalViewportSize.x / global.FinalViewportSize.y
                                                  : global.OutputSize.x / global.OutputSize.y;

  // The tint colour (fully saturated) and how the trail's brightness is measured for it: weights w with
  // w . colour = the default blue's luminance (every colour fades as blue does) and w . white = 1 (a white flash as
  // bright in every colour) - the luminance weights plus the smallest change across the colour.
  vec3 tint = HueColour(global.LL_PSY_HUE);
  const vec3 lw = vec3(0.3, 0.59, 0.11);
  float ref = dot(lw, HueColour(0.66));
  float lum = dot(lw, tint), sum = tint.r + tint.g + tint.b;
  float spread = dot(tint, tint) - sum * sum / 3.0;
  float k = spread > 1e-4 ? (ref - lum) / spread : 0.0;
  vec3 weigh = lw + k * (tint - sum / 3.0);
  float amount = 1.0 - pow(1.0 - clamp(global.LL_PSY_TINT, 0.0, 0.999), dt * 50.0);
  float cycle = 6.2831853 * global.LL_PSY_CYCLE * dt;

  vec2 centre = vec2(global.LL_PSY_CENTRE_X, global.LL_PSY_CENTRE_Y);
  vec2 d = vTexCoord - centre;
  d.x *= aspect;
  float cs = cos(turn), sn = sin(turn);
  d = vec2(d.x * cs + d.y * sn, -d.x * sn + d.y * cs) * zoom;
  d.x /= aspect;
  vec2 uv = d + centre;
  vec3 trail = vec3(0.0);
  if (all(greaterThanEqual(uv, vec2(0.0))) && all(lessThanEqual(uv, vec2(1.0))))
    trail = textureLod(TrailFeedback, uv, 0.0).rgb;
  trail = max(HueTurn(trail, cos(cycle), sin(cycle)), 0.0) * fade;
  float l = max(dot(trail, weigh), 0.0);
  trail = mix(trail, l * tint * 2.2, amount);
  vec3 pic = Picture(vTexCoord);
  float pl = max(pic.r, max(pic.g, pic.b));
  vec3 feed = pic * global.LL_PSY_GAIN * smoothstep(global.LL_PSY_THRESHOLD, global.LL_PSY_THRESHOLD + 0.1, pl);
  FragColor = vec4(min(max(feed, trail), vec3(4.0)), 1.0);
}
