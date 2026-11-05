// CRT++ (crt.cpp), step 1: the feedback trails. Drawn into the other of two trail textures every frame shown: the
// previous trail (the texture drawn) zoomed / turned about the centre, faded, tinted and hue-turned, with this
// frame's bright parts (u_picture) on top - Minter's feedback loop.

cbuffer Params : register(b0, space3) {
  float4 move;    // 1 / zoom (this frame), turn (radians, this frame), fade (this frame), threshold
  float4 tint;    // tint colour rgb, tint amount (this frame)
  float4 hue;     // cos, sin of the hue turn (this frame), picture aspect (w / h), feed gain
};

Texture2D u_texture : register(t0, space2);  // the previous trail
SamplerState u_sampler : register(s0, space2);
Texture2D u_picture : register(t1, space2);  // this frame's picture, 1280 x 800
SamplerState u_picture_sampler : register(s1, space2);

// SDL_shadercross names every vertex output TEXCOORDn (see crt.frag.hlsl).
struct PSInput {
  float4 v_color : TEXCOORD0;
  float2 v_uv : TEXCOORD1;
};

struct PSOutput {
  float4 o_color : SV_Target;
};

float3 HueTurn(float3 c, float cs, float sn) {
  // rotation about the grey axis
  const float3 k = float3(0.57735, 0.57735, 0.57735);
  return c * cs + cross(k, c) * sn + k * dot(k, c) * (1 - cs);
}

PSOutput main(PSInput input) {
  PSOutput output;
  float2 d = input.v_uv - 0.5;
  d.x *= hue.z;
  float cs = cos(move.y), sn = sin(move.y);
  d = float2(d.x * cs + d.y * sn, -d.x * sn + d.y * cs) * move.x;
  d.x /= hue.z;
  float2 uv = d + 0.5;
  float3 trail = 0;
  if (all(uv >= 0) && all(uv <= 1)) trail = u_texture.SampleLevel(u_sampler, uv, 0).rgb;
  trail = max(HueTurn(trail, hue.x, hue.y), 0) * move.z;
  float l = dot(trail, float3(0.3, 0.59, 0.11));
  trail = lerp(trail, l * tint.rgb * 2.2, tint.w);
  float3 pic = u_picture.SampleLevel(u_picture_sampler, input.v_uv, 0).rgb;
  float pl = max(pic.r, max(pic.g, pic.b));
  float3 feed = pic * hue.w * smoothstep(move.w, move.w + 0.1, pl);
  output.o_color = float4(min(max(feed, trail), 4.0), 1.0);
  return output;
}
