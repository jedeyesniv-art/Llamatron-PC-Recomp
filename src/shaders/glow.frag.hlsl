// The CRT's halation, first step (crt.cpp BuildGlow): the picture copied into the glow's 320 x 200 float level as
// linear light, so the blur that follows spreads light the way glass scatters it (blurring gamma-encoded values
// and decoding afterwards all but erases a small bright thing's glow).

Texture2D u_texture : register(t0, space2);
SamplerState u_sampler : register(s0, space2);

// SDL_shadercross names every vertex output TEXCOORDn (see crt.frag.hlsl).
struct PSInput {
  float4 v_color : TEXCOORD0;
  float2 v_uv : TEXCOORD1;
};

struct PSOutput {
  float4 o_color : SV_Target;
};

PSOutput main(PSInput input) {
  PSOutput output;
  float3 c = u_texture.Sample(u_sampler, input.v_uv).rgb;
  output.o_color = float4(c * c * (0.8 + 0.2 * c), 1.0);  // ~gamma 2.2 to linear, as crt.frag.hlsl's Fetch
  return output;
}
