// The CRT pass (crt-plus.slang / crt-plus-plus.slang include this; PSY defined = CRT++): soft horizontal beam,
// scanlines that thicken with brightness, a phosphor mask with colour bleed, halation from the blurred glow levels,
// colour overflow, warmth, saturation and an S-curve contrast. A port of "Llamasoft on the ST"'s crt.frag.hlsl.
// CRT+ reads the source picture (Original) pixel by pixel. CRT++ reads the trails pass's 4x picture (PsyOut)
// "supersampled": the line grid is still the source's, but x is continuous and each line is averaged over its
// height, so anything between pixels stays between them.

layout(std140, set = 0, binding = 0) uniform UBO {
  mat4 MVP;
  vec4 OutputSize;
  vec4 OriginalSize;
  vec4 GlowL1Size;
  vec4 GlowL2Size;
  vec4 GlowL3Size;
  vec4 GlowL4Size;
  vec4 GlowL5Size;
  float LL_SCANLINES, LL_BEAM_MIN, LL_BEAM_MAX, LL_BLUR, LL_LINES, LL_VERTICAL;
  float LL_MASK, LL_MASK_BLEED, LL_MASK_TYPE, LL_MASK_SIZE;
  float LL_HALATION, LL_HALATION_SIZE, LL_CURVATURE, LL_VIGNETTE;
  float LL_BRIGHTNESS, LL_CONTRAST, LL_SATURATION, LL_TEMPERATURE, LL_OVERFLOW, LL_GAMMA;
#ifdef PSY
  float LL_PSY_GLOW, LL_PSY_SATURATION;
#endif
} global;

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
#ifdef PSY
layout(set = 0, binding = 3) uniform sampler2D PsyOut;
#endif
layout(set = 0, binding = 4) uniform sampler2D GlowL1;
layout(set = 0, binding = 5) uniform sampler2D GlowL2;
layout(set = 0, binding = 6) uniform sampler2D GlowL3;
layout(set = 0, binding = 7) uniform sampler2D GlowL4;
layout(set = 0, binding = 8) uniform sampler2D GlowL5;

vec3 Linear(vec3 c) { return c * c * (0.8 + 0.2 * c); }  // ~gamma 2.2 to linear, cheaply
float Luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

// "Beam space": x along the scanlines, y across them (swapped for vertical scanlines), y in scanlines (a scanline
// = LL_LINES source lines).
bool Vertical() { return global.LL_VERTICAL > 0.5; }
int LinesPer() { return int(clamp(global.LL_LINES, 1.0, 4.0) + 0.5); }
vec2 Grid() { return Vertical() ? global.OriginalSize.yx : global.OriginalSize.xy; }  // source pixels, beam space

#ifndef PSY
vec3 FetchPixel(int x, int y) {  // beam space, source pixels
  ivec2 t = Vertical() ? ivec2(y, x) : ivec2(x, y);
  t = clamp(t, ivec2(0), ivec2(global.OriginalSize.xy) - 1);
  return Linear(texelFetch(Original, t, 0).rgb);
}
// One scanline's pixel: the average of its source lines.
vec3 Fetch(int x, int line) {
  int k = LinesPer();
  line = clamp(line, 0, int(Grid().y) / k - 1);
  vec3 s = vec3(0.0);
  for (int j = 0; j < k; ++j) s += FetchPixel(x, line * k + j);
  return s / float(k);
}
#else
// Supersampled: x continuous (source pixels), the scanline averaged over its height (bilinear taps at every half
// source line).
vec3 FetchSmooth(float x, int line) {
  int k = LinesPer();
  vec2 g = Grid();
  line = clamp(line, 0, int(g.y) / k - 1);
  float u = clamp(x, 0.0, g.x) / g.x;
  vec3 s = vec3(0.0);
  for (int j = 0; j < 2 * k; ++j) {
    float v = (float(line * k) + (float(j) + 0.5) * 0.5) / g.y;
    s += textureLod(PsyOut, Vertical() ? vec2(v, u) : vec2(u, v), 0.0).rgb;
  }
  s /= float(2 * k);
  return Linear(s);
}
#endif

// One scanline through a soft horizontal beam (Gaussian taps; blur 0 = sharp, 1 = very soft).
vec3 Line(float px, int line) {
  float sigma = 0.25 + 0.5 * global.LL_BLUR;
#ifdef PSY
  // taps at fixed distances from px (motion stays smooth), a narrower beam (the taps see the pixels' whole width)
  vec3 s = vec3(0.0);
  float ws = 0.0;
  float ss = max(0.12, sigma * 0.6);
  for (int i = -3; i <= 3; ++i) {
    float d = float(i) * 0.25;
    float w = exp(-0.5 * d * d / (ss * ss));
    s += FetchSmooth(px + d, line) * w;
    ws += w;
  }
  return s / ws;
#else
  int x0 = int(floor(px - 0.5));
  vec3 sum = vec3(0.0);
  float wsum = 0.0;
  for (int i = -1; i <= 2; ++i) {
    float d = (float(x0 + i) + 0.5) - px;
    float w = exp(-0.5 * d * d / (sigma * sigma));
    sum += Fetch(x0 + i, line) * w;
    wsum += w;
  }
  return sum / wsum;
#endif
}

// The beam's profile across the line: thin for dark colours, wide for bright ones (they bloom into the gap).
vec3 Beam(vec3 c, float d) {
  vec3 width = mix(vec3(global.LL_BEAM_MIN), vec3(global.LL_BEAM_MAX), sqrt(clamp(c, 0.0, 1.0)));
  vec3 sigma = max(0.5 * width, vec3(0.02));
  return c * exp(-0.5 * d * d / (sigma * sigma)) / (sigma * 2.5066);  // integrates to c over a line
}

// A glow level read smoothly: four bilinear taps half a texel apart (a tent), so its texels never show.
vec3 Tent(sampler2D t, vec2 uv, vec2 size) {
  vec2 h = 0.5 / size;
  return 0.25 * (textureLod(t, uv + vec2(-h.x, -h.y), 0.0).rgb + textureLod(t, uv + vec2(h.x, -h.y), 0.0).rgb +
                 textureLod(t, uv + vec2(-h.x, h.y), 0.0).rgb + textureLod(t, uv + vec2(h.x, h.y), 0.0).rgb);
}

// The phosphor mask at a screen pixel (beam space): R, G, B phosphors LL_MASK_SIZE pixels each.
vec3 Mask(vec2 mp) {
  float size = max(floor(global.LL_MASK_SIZE + 0.5), 1.0);
  int type = int(global.LL_MASK_TYPE + 0.5);
  float x = mp.x, gate = 1.0;
  if (type == 1) {  // slot mask: the grille cut into slots, every other triad offset by half a slot
    float triad = floor(x / (3.0 * size));
    float row = floor(mp.y / size) + mod(triad, 2.0) * 2.0;
    gate = mod(row, 4.0) > 2.5 ? 0.0 : 4.0 / 3.0;  // (the gaps' light given to the slots: same brightness)
  } else if (type == 2) {  // shadow mask: dot triads, every other row shifted by half a triad
    x += mod(floor(mp.y / size), 2.0) * floor(1.5 * size + 0.5);
  }
  int k = int(mod(floor(x / size), 3.0));
  vec3 stripe = k == 0 ? vec3(1.0, 0.0, 0.0) : k == 1 ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 0.0, 1.0);
  stripe = mix(stripe, vec3(1.0 / 3.0) + stripe * (2.0 / 3.0), global.LL_MASK_BLEED) * gate;
  return mix(vec3(1.0), stripe * 3.0, global.LL_MASK * 0.5);
}

void main() {
  vec2 uv = vTexCoord;
  vec2 pix = uv * global.OutputSize.xy;  // this pixel on the screen, for the mask

  // Curvature (barrel): the middles of the edges stay put.
  vec2 d = uv - 0.5;
  uv = 0.5 + d * (1.0 + global.LL_CURVATURE * 2.0 * (dot(d, d) - 0.25));
  vec2 edge = clamp((0.5 - abs(uv - 0.5)) * global.OutputSize.xy * 0.5, 0.0, 1.0);  // a 2-pixel soft border
  if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
    FragColor = vec4(0.0, 0.0, 0.0, 1.0);
    return;
  }

  vec2 p = uv * global.OriginalSize.xy;
  if (Vertical()) p = p.yx;
  p.y /= float(LinesPer());
  int y0 = int(floor(p.y - 0.5));
  float dy = p.y - (float(y0) + 0.5);  // 0..1 from scanline y0 to y0 + 1
  vec3 a = Line(p.x, y0), b = Line(p.x, y0 + 1);

  // Scanlines mixed with the plain picture by their strength.
  vec3 lines = Beam(a, dy) + Beam(b, 1.0 - dy);
  vec3 flat_ = mix(a, b, smoothstep(0.0, 1.0, dy));
  vec3 c = mix(flat_, lines, global.LL_SCANLINES);

  if (global.LL_MASK > 0.0) c *= Mask(Vertical() ? pix.yx : pix);

  float halation = global.LL_HALATION, saturation = global.LL_SATURATION;
#ifdef PSY
  halation *= global.LL_PSY_GLOW;
  saturation *= global.LL_PSY_SATURATION;
#endif
  vec3 warm = vec3(1.0 + 0.08 * global.LL_TEMPERATURE, 1.0, 1.0 - 0.12 * global.LL_TEMPERATURE);

  // Halation: light scattered in the tube's glass, over the phosphors (no mask in it), from the blurred levels -
  // their weights spread out with the size (0: mostly the tight ones; 1: even over all five, a long tail). Added
  // after the colour and contrast curves, as light (the S-curve would steepen its faint edge into a rim).
  vec3 halo = vec3(0.0);
  if (halation > 0.0) {
    float t = clamp(global.LL_HALATION_SIZE, 0.0, 1.0);
    float w1 = mix(0.40, 0.14, t), w2 = mix(0.27, 0.17, t), w3 = mix(0.17, 0.20, t), w4 = mix(0.10, 0.23, t),
          w5 = mix(0.06, 0.26, t);
    halo = Tent(GlowL1, uv, global.GlowL1Size.xy) * w1 + Tent(GlowL2, uv, global.GlowL2Size.xy) * w2 +
           Tent(GlowL3, uv, global.GlowL3Size.xy) * w3 + Tent(GlowL4, uv, global.GlowL4Size.xy) * w4 +
           Tent(GlowL5, uv, global.GlowL5Size.xy) * w5;
    halo *= halation * 2.0 * warm;
  }

  // Colour: overflow (a channel past full spills into the others, towards white), warmth, saturation, contrast
  // (an S-curve that keeps black black and white white) and brightness.
  vec3 over = max(c - 1.0, 0.0);
  c += (vec3(over.r + over.g + over.b) - over) * 0.5 * global.LL_OVERFLOW;
  c *= warm;
  float l = Luma(c);
  c = clamp(mix(vec3(l), c, saturation), 0.0, 1.0);
  float gamma = max(global.LL_GAMMA, 0.1);
  vec3 g = clamp(pow(c, vec3(1.0 / gamma)), 0.0, 1.0);
  g = mix(g, g * g * (3.0 - 2.0 * g), clamp(global.LL_CONTRAST, 0.0, 1.0)) * (1.0 + global.LL_BRIGHTNESS);
  if (halation > 0.0) g = pow(pow(clamp(g, 0.0, 1.0), vec3(gamma)) + halo, vec3(1.0 / gamma));
  if (global.LL_VIGNETTE > 0.0) {
    vec2 w = uv * (1.0 - uv);
    g *= mix(1.0, pow(clamp(16.0 * w.x * w.y, 0.0, 1.0), 0.3), global.LL_VIGNETTE);
  }
  if (global.LL_CURVATURE > 0.0) g *= edge.x * edge.y;  // the tube's soft edge
  FragColor = vec4(clamp(g, 0.0, 1.0), 1.0);
}
