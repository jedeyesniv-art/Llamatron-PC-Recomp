// CRT look for the ST picture, drawn straight to the screen at its full resolution (SDL GPU renderer custom
// render state; Tools/build_shaders.py compiles it to crt.dxil.h).
// Our own shader, tuned to the owner's ShaderGlass profile (crt-yah): soft horizontal beam, scanlines that
// thicken with brightness, halation glow, an aperture-grille mask with colour bleed, slight curvature and
// vignette, warm colour. All the numbers come from settings.txt (crt_*), see crt.cpp.

cbuffer Params : register(b0, space3) {
  float4 src;      // source texture w, h, 1/w, 1/h
  float4 dst;      // the picture on the screen in pixels: w, h, 0, 0
  float4 beam;     // scanline strength, beam width min, beam width max, horizontal blur
  float4 glow;     // mask strength, mask colour bleed, halation intensity, halation size
  float4 shape;    // curvature, vignette, colour overflow, brightness
  float4 colour;   // saturation, contrast, temperature, 0
};

Texture2D u_texture : register(t0, space2);
SamplerState u_sampler : register(s0, space2);

// SDL's own shaders are compiled with SDL_shadercross, which names every vertex output TEXCOORDn: the inputs
// have to match those names or D3D12 rejects the pipeline.
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
  float3 c = u_texture.Load(int3(x, y, 0)).rgb;
  return c * c * (0.8 + 0.2 * c);  // ~gamma 2.2 to linear, cheaply
}

// One source line through a soft horizontal beam (Gaussian taps; blur 0 = sharp, 1 = very soft).
float3 Line(float px, int y) {
  float sigma = 0.25 + 0.5 * beam.w;
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

  // Curvature (barrel) and the rounded edge of the tube.
  float2 d = uv - 0.5;
  uv = 0.5 + d * (1.0 + shape.x * dot(d, d) * 2.0) / (1.0 + shape.x * 0.5);
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

  // Halation: a wide, soft glow of the picture around it (a 5 x 5 grid of taps).
  if (glow.z > 0) {
    float r = 1.0 + 4.0 * glow.w;
    float3 h = 0;
    float hw = 0;
    [unroll] for (int j = -2; j <= 2; ++j) {
      [unroll] for (int i = -2; i <= 2; ++i) {
        float w = exp(-0.5 * (i * i + j * j));
        h += Fetch(int(floor(p.x + i * r)), int(floor(p.y + j * r))) * w;
        hw += w;
      }
    }
    c = c * (1.0 - 0.5 * glow.z) + h / hw * glow.z;
  }

  // Aperture grille: R, G, B stripes one screen pixel each; bleed spreads each into its neighbours.
  if (glow.x > 0) {
    int k = int(fmod(floor(pix.x), 3.0));
    float3 stripe = k == 0 ? float3(1, 0, 0) : k == 1 ? float3(0, 1, 0) : float3(0, 0, 1);
    stripe = lerp(stripe, float3(1, 1, 1) * (1.0 / 3.0) + stripe * (2.0 / 3.0), glow.y);
    float3 mask = lerp(float3(1, 1, 1), stripe * 3.0, glow.x * 0.5);
    c *= mask;
  }

  // Colour: overflow (a channel past full spills into the others, towards white), temperature, saturation,
  // contrast and brightness.
  float3 over = max(c - 1.0, 0.0);
  c += (over.r + over.g + over.b - over) * 0.5 * shape.z;
  c *= float3(1.0 + 0.08 * colour.z, 1.0, 1.0 - 0.12 * colour.z);
  float l = Luma(c);
  c = lerp(float3(l, l, l), c, colour.x);
  c = saturate(c);
  float3 g = pow(c, 1.0 / 2.2);  // back to gamma space
  g = ((g - 0.5) * (1.0 + 0.25 * colour.y) + 0.5) * (1.0 + shape.w);

  // Vignette and the tube's edge.
  float2 v = uv * (1.0 - uv);
  float vig = pow(saturate(v.x * v.y * 16.0), 0.25);
  g *= lerp(1.0, vig, shape.y) * edge.x * edge.y;

  output.o_color = float4(saturate(g), 1.0) * float4(input.v_color.rgb, 1.0);
  return output;
}
