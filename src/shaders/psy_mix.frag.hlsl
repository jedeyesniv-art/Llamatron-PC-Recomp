// CRT++ (crt.cpp), step 2: this frame's picture with its colour smeared sideways (luma kept sharp, chroma
// blurred: a composite TV's bleed), the trails screened over it. The result goes on to the CRT shader.

cbuffer Params : register(b0, space3) {
  float4 mix;     // trail strength, bleed (in uv), keep colours (0..1: trails kept off the picture), 0
};

Texture2D u_texture : register(t0, space2);  // the trail
SamplerState u_sampler : register(s0, space2);
Texture2D u_picture : register(t1, space2);  // this frame's picture, 1280 x 800
SamplerState u_picture_sampler : register(s1, space2);

struct PSInput {
  float4 v_color : TEXCOORD0;
  float2 v_uv : TEXCOORD1;
};

struct PSOutput {
  float4 o_color : SV_Target;
};

PSOutput main(PSInput input) {
  PSOutput output;
  const float3 lw = float3(0.3, 0.59, 0.11);
  float3 c = u_picture.SampleLevel(u_picture_sampler, input.v_uv, 0).rgb;
  float3 blur = c * 0.2;
  [unroll] for (int i = 1; i <= 4; ++i) {
    float o = mix.y * i / 4.0;
    blur += (u_picture.SampleLevel(u_picture_sampler, input.v_uv + float2(o, 0), 0).rgb +
             u_picture.SampleLevel(u_picture_sampler, input.v_uv - float2(o, 0), 0).rgb) * 0.1;
  }
  // the blur's colour at this pixel's brightness, never darker than the sharp picture's brightest channel allows
  float3 bled = max(c, dot(c, lw) + (blur - dot(blur, lw)) * 1.5);
  // the trails show where the picture is dark: things keep their own colour, the trails stream out round them
  float cover = smoothstep(0.04, 0.3, max(c.r, max(c.g, c.b))) * mix.z;
  float3 t = u_texture.SampleLevel(u_sampler, input.v_uv, 0).rgb * mix.x * (1 - cover);
  float3 o = 1 - (1 - saturate(bled)) * (1 - saturate(t));  // screen
  output.o_color = float4(o + max(t - 1, 0) * 0.5, 1.0);   // the trail's excess still adds a little
  return output;
}
