#version 450
// CRT++, step 2: the picture with its colour smeared sideways (luma sharp, chroma blurred: a composite TV's bleed),
// the trails screened over it where the picture is dark. The result (PsyOut) goes on to the glow and the CRT pass.

#include "params-crt.inc"
#include "params-psy.inc"

layout(std140, set = 0, binding = 0) uniform UBO {
  mat4 MVP;
  vec4 OutputSize;
  vec4 OriginalSize;
  float LL_PSY_TRAILS, LL_PSY_BLEED, LL_PSY_KEEP;
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
layout(set = 0, binding = 2) uniform sampler2D Source;  // the trails
layout(set = 0, binding = 3) uniform sampler2D Original;

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

void main() {
  const vec3 lw = vec3(0.3, 0.59, 0.11);
  vec2 uv = vTexCoord;
  vec3 c = Picture(uv);
  float reach = global.LL_PSY_BLEED * 2.5 * global.OriginalSize.z;  // (2.5 source pixels a unit)
  vec3 blur = c * 0.2;
  for (int i = 1; i <= 4; ++i) {
    float o = reach * float(i) / 4.0;
    blur += (Picture(uv + vec2(o, 0.0)) + Picture(uv - vec2(o, 0.0))) * 0.1;
  }
  // the blur's colour at this pixel's brightness, never darker than the sharp picture's brightest channel allows
  vec3 bled = max(c, dot(c, lw) + (blur - dot(blur, lw)) * 1.5);
  // the trails show where the picture is dark: things keep their own colour, the trails stream out round them
  float cover = smoothstep(0.04, 0.3, max(c.r, max(c.g, c.b))) * global.LL_PSY_KEEP;
  vec3 t = textureLod(Source, uv, 0.0).rgb * global.LL_PSY_TRAILS * (1.0 - cover);
  vec3 o = 1.0 - (1.0 - clamp(bled, 0.0, 1.0)) * (1.0 - clamp(t, 0.0, 1.0));  // screen
  FragColor = vec4(o + max(t - 1.0, 0.0) * 0.5, 1.0);  // the trail's excess still adds a little
}
