// CRT+ parameters (both presets). Defaults = the CRT look of "Llamasoft on the ST" as its owner tuned it.
#pragma parameter LL_SCANLINES "Beam: scanline strength" 0.45 0.0 1.0 0.05
#pragma parameter LL_BEAM_MIN "Beam: line thickness, dark colours" 0.35 0.05 1.5 0.05
#pragma parameter LL_BEAM_MAX "Beam: line thickness, bright colours" 1.0 0.05 1.5 0.05
#pragma parameter LL_BLUR "Beam: horizontal softness (0 = sharp pixels)" 0.4 0.0 1.0 0.05
#pragma parameter LL_LINES "Beam: source lines per scanline (2 = hi-res / interlaced)" 1.0 1.0 4.0 1.0
#pragma parameter LL_VERTICAL "Beam: vertical scanlines (a rotated / TATE picture)" 0.0 0.0 1.0 1.0
#pragma parameter LL_MASK "Mask: phosphor mask strength" 0.35 0.0 1.0 0.05
#pragma parameter LL_MASK_BLEED "Mask: stripes blending together" 0.5 0.0 1.0 0.05
#pragma parameter LL_MASK_TYPE "Mask: type (0 aperture grille, 1 slot mask, 2 shadow mask)" 0.0 0.0 2.0 1.0
#pragma parameter LL_MASK_SIZE "Mask: screen pixels per phosphor (2+ on 4K)" 1.0 1.0 6.0 1.0
#pragma parameter LL_HALATION "Glow: halation (light spilling round bright things)" 0.04 0.0 0.3 0.01
#pragma parameter LL_HALATION_SIZE "Glow: size (0 tight halo .. 1 long soft tail)" 0.98 0.0 1.0 0.02
#pragma parameter LL_HALATION_FALLOFF "Glow: tail (0.2 = as in the game, 1 = long)" 0.2035 0.05 1.0 0.01
#pragma parameter LL_CURVATURE "Tube: curvature (0 = flat)" 0.02 0.0 0.3 0.01
#pragma parameter LL_VIGNETTE "Tube: vignette (darker corners)" 0.0 0.0 1.0 0.05
#pragma parameter LL_BRIGHTNESS "Colour: brightness" 0.01 -0.3 0.5 0.01
#pragma parameter LL_CONTRAST "Colour: contrast (deeper blacks, brighter whites)" 0.35 0.0 1.0 0.05
#pragma parameter LL_SATURATION "Colour: saturation" 0.95 0.0 2.0 0.05
#pragma parameter LL_TEMPERATURE "Colour: warmth (-1 cool blue .. 1 warm)" 0.3 -1.0 1.0 0.05
#pragma parameter LL_OVERFLOW "Colour: bloom (very bright colours spill into white)" 0.85 0.0 1.0 0.05
#pragma parameter LL_GAMMA "Colour: output gamma" 2.2 1.6 3.0 0.05
