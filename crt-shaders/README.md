# CRT+ and CRT++ shaders

The two CRT looks from *Llamasoft on the ST*, as RetroArch slang presets for any retro game, in **ShaderGlass**
or **RetroArch**.

- **`crt-plus.slangp`** (CRT+): a soft beam with scanlines that get thicker on bright colours, a phosphor mask
  with colour bleed, a smooth halation glow, colour that spills into white when it's very bright, warmth, and an
  S-curve contrast that keeps blacks black.
- **`crt-plus-plus.slangp`** (CRT++): CRT+ plus Jeff Minter-style feedback trails (bright things leave tinted
  trails that zoom, twist and colour-cycle as they fade), a composite TV's colour bleed, and a stronger glow and
  saturation.

The defaults are the looks as tuned in the game. Every number is a shader parameter, so you can change it live
in both programs.

## ShaderGlass

1. **Shader → Import custom...** and pick `crt-plus.slangp` or `crt-plus-plus.slangp` (keep the `shaders`
   folder next to it).
2. **Input → Pixel Size**: set it to the game's own scale (a 320 x 200 game shown 4x = x4), so the shader sees
   the original pixels. Check with the *none* shader that the picture is crisp pixels first.
3. **Shader → Parameters** has every setting.

ShaderGlass forgets imported shaders when it closes: re-import them from **Recent imports**, or save a profile
(**File → Save Profile**). ShaderGlass doesn't tell shaders the frame time, so CRT++ assumes the rate in
*CRT++: frame rate when the host doesn't say* (60). Set it to ShaderGlass's own output rate (shown in its title
bar) so the trails last as long as they should.

## RetroArch

1. Copy this folder into RetroArch's `shaders\shaders_slang\` folder.
2. **Quick Menu → Shaders → Load** (Load Shader Preset), choose one of the presets.
3. **Shader Parameters** has every setting. **Save Preset** keeps your changes.

Video driver: Vulkan, Direct3D 11 / 12 or OpenGL core (the slang drivers). Tested on Vulkan, D3D11 and glcore
(RetroArch 1.21).

## Parameters

| Group | What it does |
|---|---|
| Beam: scanline strength | how dark the gaps between the lines are |
| Beam: line thickness, dark / bright colours | bright colours make thicker lines that fill the gaps |
| Beam: horizontal softness | 0 = sharp pixels |
| Beam: source lines per scanline | 2 for hi-res or interlaced pictures (480i, Amiga hi-res...) |
| Beam: vertical scanlines | for a picture that is rotated on screen (vertical arcade games in ShaderGlass) |
| Mask: strength, stripes blending | the phosphor mask and how much its colours blend |
| Mask: type | 0 aperture grille (Trinitron), 1 slot mask, 2 shadow mask |
| Mask: screen pixels per phosphor | 1 on 1080p; 2-3 on 4K for a visible mask |
| Glow: halation, size | light spilling round bright things; size 0 = tight halo, 1 = spread over all levels |
| Glow: tail | how much each wider blur level keeps: 0.2 = the game's look, 1 = a long, wide haze |
| Tube: curvature, vignette | a curved screen, darker corners (both off or slight by default) |
| Colour: brightness, contrast, saturation, warmth, bloom, output gamma | the picture's colour |
| CRT++: trail strength, length | how strong the trails are, seconds for them to fade to half |
| CRT++: zoom, twist, centre | trails stream outwards (+) or inwards (-) and swirl round a centre you can move |
| CRT++: trail colour, tint, colour cycle | the colour trails turn, how fast, and a rainbow cycle as they age |
| CRT++: threshold, feed | how bright something must be to leave a trail, and how bright it enters |
| CRT++: keep colours | 1 = trails only show where the picture is dark |
| CRT++: colour bleed | sideways colour smear |
| CRT++: glow / saturation boost | multiplies the CRT+ halation and saturation |

## Performance

CRT++ runs its trails at 4x the source's size (`scale0 = 4.0` in the preset). For big sources (640 x 480 and
up), `scale0 = 2.0` is lighter; then also set `scale2 = 0.5`, which must bring the picture back to the source's
size.

## Files

`shaders/crt.inc` is the CRT pass (CRT+ and CRT++), `linearize.slang` and `glow-down.slang` build the glow's
blur levels, `psy-trail.slang` and `psy-mix.slang` are CRT++'s trails and colour bleed, and `params-*.inc` list
the parameters.
