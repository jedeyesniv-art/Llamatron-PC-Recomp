#version 450
// Glow, step 1: the picture as linear light at the source's size (the blur that follows spreads light as the
// tube's glass scatters it). From a 4x picture (CRT++) each output pixel is the average of its 4 x 4 block.

#include "params-crt.inc"

layout(std140, set = 0, binding = 0) uniform UBO {
  mat4 MVP;
  vec4 SourceSize;
  vec4 OutputSize;
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
  vec3 c;
  if (global.SourceSize.x * global.OutputSize.z > 3.5) {  // 4x down: four bilinear taps, each a 2 x 2 average
    vec2 h = global.SourceSize.zw;
    c = 0.25 * (textureLod(Source, vTexCoord + vec2(-h.x, -h.y), 0.0).rgb +
                textureLod(Source, vTexCoord + vec2(h.x, -h.y), 0.0).rgb +
                textureLod(Source, vTexCoord + vec2(-h.x, h.y), 0.0).rgb +
                textureLod(Source, vTexCoord + vec2(h.x, h.y), 0.0).rgb);
  } else {
    c = textureLod(Source, vTexCoord, 0.0).rgb;
  }
  c = clamp(c, 0.0, 1.0);
  FragColor = vec4(c * c * (0.8 + 0.2 * c), 1.0);  // ~gamma 2.2 to linear, cheaply
}
