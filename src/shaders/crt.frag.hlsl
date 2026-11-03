// CRT look for the ST picture, drawn straight to the screen at its full resolution (SDL GPU renderer custom
// render state; Tools/build_shaders.py compiles it to crt.dxil.h).
// Our own shader, tuned to the owner's ShaderGlass profile (crt-yah): soft horizontal beam, scanlines that
// thicken with brightness, a faint halation glow, an aperture-grille mask with colour bleed, warm colour, deep
// blacks. No vignette or glare (owner). All the numbers come from settings.txt (crt_*), see crt.cpp.

cbuffer Params : register(b0, space3) {
  float4 src;      // source texture w, h, 1/w, 1/h
  float4 dst;      // the picture on the screen in pixels: w, h, 0, 0
  float4 beam;     // scanline strength, beam width min, beam width max, horizontal blur
  float4 glow;     // mask strength, mask colour bleed, halation intensity, halation size
  float4 shape;    // curvature, 0, colour overflow, brightness
  float4 colour;   // saturation, contrast, temperature, supersampled (the remaster's 4x picture: 1, else 0)
};

Texture2D u_texture : register(t0, space2);
SamplerState u_sampler : register(s0, space2);
// The halation's glow (crt.cpp BuildGlow, every frame): the picture as linear light blurred at 160 x 100, 80 x 50,
// 40 x 25, 20 x 13, 10 x 7.
Texture2D u_glow1 : register(t1, space2);
SamplerState u_glow1_sampler : register(s1, space2);
Texture2D u_glow2 : register(t2, space2);
SamplerState u_glow2_sampler : register(s2, space2);
Texture2D u_glow3 : register(t3, space2);
SamplerState u_glow3_sampler : register(s3, space2);
Texture2D u_glow4 : register(t4, space2);
SamplerState u_glow4_sampler : register(s4, space2);
Texture2D u_glow5 : register(t5, space2);
SamplerState u_glow5_sampler : register(s5, space2);

// SDL's own shaders are compiled with SDL_shadercross, which names every vertex output TEXCOORDn: the inputs
// have to match those names or D3D12 rejects the pipeline.
static const float kGlowGain = 2.0;

struct PSInput {
  float4 v_color : TEXCOORD0;
  float2 v_uv : TEXCOORD1;
};

struct PSOutput {
  float4 o_color : SV_Target;
};

float3 Fetch(int x, int y) {
  x = clamp(x, 0, int(src.x) - 1);
  y = clamp(y, 0, int(src.y) - 1);
  if (colour.w > 0) {  // supersampled: the middle of the pixel's block
    float3 m = u_texture.SampleLevel(u_sampler, float2((x + 0.5) * src.z, (y + 0.5) * src.w), 0).rgb;
    return m * m * (0.8 + 0.2 * m);
  }
  float3 c = u_texture.Load(int3(x, y, 0)).rgb;
  return c * c * (0.8 + 0.2 * c);  // ~gamma 2.2 to linear, cheaply
}

// A supersampled source (src = the ST's size, the texture bigger): x continuous (source pixels), line y as the
// average of its upper and lower half (bilinear taps): things between pixels stay between them.
float3 FetchSmooth(float x, int y) {
  y = clamp(y, 0, int(src.y) - 1);
  float u = clamp(x, 0.0, src.x) * src.z;
  float3 c = u_texture.SampleLevel(u_sampler, float2(u, (y + 0.25) * src.w), 0).rgb +
             u_texture.SampleLevel(u_sampler, float2(u, (y + 0.75) * src.w), 0).rgb;
  c *= 0.5;
  return c * c * (0.8 + 0.2 * c);
}

// One source line through a soft horizontal beam (Gaussian taps; blur 0 = sharp, 1 = very soft).
float3 Line(float px, int y) {
  float sigma = 0.25 + 0.5 * beam.w;
  if (colour.w > 0) {  // supersampled: taps at fixed distances from px (motion stays smooth), a narrower
    float3 s = 0;      // beam (the taps see the pixels' whole width, not their middles)
    float ws = 0;
    float ss = max(0.12, sigma * 0.6);
    [unroll] for (int i = -3; i <= 3; ++i) {
      float d = i * 0.25;
      float w = exp(-0.5 * d * d / (ss * ss));
      s += FetchSmooth(px + d, y) * w;
      ws += w;
    }
    return s / ws;
  }
  int x0 = int(floor(px - 0.5));
  float3 sum = 0;
  float wsum = 0;
  [unroll] for (int i = -1; i <= 2; ++i) {
    float d = (x0 + i + 0.5) - px;
    float w = exp(-0.5 * d * d / (sigma * sigma));
    sum += Fetch(x0 + i, y) * w;
    wsum += w;
  }
  return sum / wsum;
}

float Luma(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }

float3 Linear(float3 c) { return c * c * (0.8 + 0.2 * c); }

// A glow level read smoothly: four bilinear taps half a texel apart (a tent), so its texels never show.
float3 Tent(Texture2D t, SamplerState s, float2 uv, float2 size) {
  float2 h = 0.5 / size;
  float3 c = t.SampleLevel(s, uv + float2(-h.x, -h.y), 0).rgb + t.SampleLevel(s, uv + float2(h.x, -h.y), 0).rgb +
             t.SampleLevel(s, uv + float2(-h.x, h.y), 0).rgb + t.SampleLevel(s, uv + float2(h.x, h.y), 0).rgb;
  return c * 0.25;  // (the levels hold linear light already)
}

// The beam's vertical profile: thin for dark lines, wide for bright ones (they bloom into the gap).
float3 Beam(float3 c, float d) {
  float3 width = lerp(beam.y, beam.z, sqrt(saturate(c)));
  float3 sigma = 0.5 * width;
  return c * exp(-0.5 * d * d / (sigma * sigma)) / (sigma * 2.5066);  // integrates to c over a line
}

PSOutput main(PSInput input) {
  PSOutput output;
  float2 uv = input.v_uv;
  float2 pix = uv * dst.xy;  // this pixel on the screen, for the mask

  // Curvature (barrel; off by default, owner: the picture keeps its size): the middles of the edges stay put.
  float2 d = uv - 0.5;
  uv = 0.5 + d * (1.0 + shape.x * 2.0 * (dot(d, d) - 0.25));
  float2 edge = saturate((0.5 - abs(uv - 0.5)) * dst.xy * 0.5);  // a 2-pixel soft border
  if (uv.x < 0 || uv.x > 1 || uv.y < 0 || uv.y > 1) {
    output.o_color = float4(0, 0, 0, 1);
    return output;
  }

  float2 p = uv * src.xy;
  int y0 = int(floor(p.y - 0.5));
  float dy = p.y - (y0 + 0.5);  // 0..1 from line y0 to y0 + 1
  float3 a = Line(p.x, y0), b = Line(p.x, y0 + 1);

  // Scanlines mixed with the plain picture by their strength.
  float3 lines = Beam(a, dy) + Beam(b, 1.0 - dy);
  float3 flat = lerp(a, b, smoothstep(0.0, 1.0, dy));
  float3 c = lerp(flat, lines, beam.x);

  // Aperture grille: R, G, B stripes one screen pixel each; bleed spreads each into its neighbours.
  if (glow.x > 0) {
    int k = int(fmod(floor(pix.x), 3.0));
    float3 stripe = k == 0 ? float3(1, 0, 0) : k == 1 ? float3(0, 1, 0) : float3(0, 0, 1);
    stripe = lerp(stripe, float3(1, 1, 1) * (1.0 / 3.0) + stripe * (2.0 / 3.0), glow.y);
    float3 mask = lerp(float3(1, 1, 1), stripe * 3.0, glow.x * 0.5);
    c *= mask;
  }

  // Halation: light scattered in the tube's glass - a smooth glow around bright things, over the phosphors (no
  // mask stripes in it), from the blurred levels: each level a wider blur, their weights spread out with the size
  // (0: mostly the tight ones; 1: even over all five - a long tail). Added after the colour and contrast curves
  // (below), as light: the S-curve would steepen its faint edge into a visible rim.
  float3 halo = 0;
  if (glow.z > 0) {
    float t = saturate(glow.w);
    float w1 = lerp(0.40, 0.14, t), w2 = lerp(0.27, 0.17, t), w3 = lerp(0.17, 0.20, t), w4 = lerp(0.10, 0.23, t),
          w5 = lerp(0.06, 0.26, t);
    halo = Tent(u_glow1, u_glow1_sampler, uv, float2(160, 100)) * w1 +
           Tent(u_glow2, u_glow2_sampler, uv, float2(80, 50)) * w2 +
           Tent(u_glow3, u_glow3_sampler, uv, float2(40, 25)) * w3 +
           Tent(u_glow4, u_glow4_sampler, uv, float2(20, 13)) * w4 +
           Tent(u_glow5, u_glow5_sampler, uv, float2(10, 7)) * w5;
    halo *= glow.z * kGlowGain * float3(1.0 + 0.08 * colour.z, 1.0, 1.0 - 0.12 * colour.z);  // warm, as the picture
  }

  // Colour: overflow (a channel past full spills into the others, towards white), temperature, saturation,
  // contrast (an S-curve that keeps black black and white white) and brightness.
  float3 over = max(c - 1.0, 0.0);
  c += (over.r + over.g + over.b - over) * 0.5 * shape.z;
  c *= float3(1.0 + 0.08 * colour.z, 1.0, 1.0 - 0.12 * colour.z);
  float l = Luma(c);
  c = lerp(float3(l, l, l), c, colour.x);
  c = saturate(c);
  float3 g = pow(c, 1.0 / 2.2);  // back to gamma space
  g = saturate(g);
  g = lerp(g, g * g * (3.0 - 2.0 * g), saturate(colour.y)) * (1.0 + shape.w);
  if (glow.z > 0) g = pow(pow(saturate(g), 2.2) + halo, 1.0 / 2.2);  // the halation, as light
  if (shape.x > 0) g *= edge.x * edge.y;  // the tube's soft edge, with curvature

  output.o_color = float4(saturate(g), 1.0) * float4(input.v_color.rgb, 1.0);
  return output;
}
