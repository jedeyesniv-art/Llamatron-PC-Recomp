#version 450
// Glow, steps 2-6: each level half the size of the one before, a smooth blur of it (four bilinear taps half an
// output pixel apart: a 4 x 4 tent), times the falloff. The game's falloff, 0.2035 = 4 x the sRGB-to-linear of 0.25:
// its blur drew four copies at a colour mod of 0.25 into float targets, which SDL3 converts to linear light.

#include "params-crt.inc"

layout(std140, set = 0, binding = 0) uniform UBO {
  mat4 MVP;
  vec4 OutputSize;
  float LL_HALATION_FALLOFF;
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
layout(set = 0, binding = 2) uniform sampler2D Source;

void main() {
  vec2 h = 0.5 * global.OutputSize.zw;
  vec3 c = textureLod(Source, vTexCoord + vec2(-h.x, -h.y), 0.0).rgb +
           textureLod(Source, vTexCoord + vec2(h.x, -h.y), 0.0).rgb +
           textureLod(Source, vTexCoord + vec2(-h.x, h.y), 0.0).rgb +
           textureLod(Source, vTexCoord + vec2(h.x, h.y), 0.0).rgb;
  FragColor = vec4(c * 0.25 * global.LL_HALATION_FALLOFF, 1.0);
}
