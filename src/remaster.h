// The native "remaster" renderer (PLAN-NEXT-LEVEL.md step 2; pilot: Photon Storm). The game still runs as it is,
// but its draw calls for the moving things (sprites, dots, the ship) are caught by hooks on its draw routines:
// recorded here instead of drawn into the ST screen. We draw them over the ST picture (backgrounds, HUD, text:
// everything not caught) at 4x, moved smoothly between the game's 50 Hz ticks at the display's rate, and the
// ship at its exact angle.
//
// Per game (the capture side, e.g. stgames.cpp for Photon Storm): BeginFrame(screen) when the game starts
// drawing a screen, Add() for each object it would have drawn there. Each item has a key (the drawing code and
// the object it belongs to): the same key in the previous frame shown = the same thing, moved.
// Sprite images are the game's own: drawn once by its routine in a sandbox copy of the machine (DrawInSandbox)
// and kept as colour indices (the palette is applied when drawn).
#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <vector>

#include "machine.h"

namespace remaster {

enum Kind : uint8_t { kSprite, kDot };

struct Item {
  uint64_t key = 0;  // identity between frames (the capture side makes it unique within a frame: Add counts)
  Kind kind = kSprite;
  float x = 0, y = 0;  // sprites: where the game put the image's corner (the image's own offset is added);
                       // dots: the top-left pixel
  uint64_t image = 0;  // sprites: the image id
  uint8_t colour = 0;  // dots: the colour index
  uint8_t w = 1, h = 1;  // dots: size
  uint8_t snap = 1;      // the game puts it at x rounded down to this (ComposeSoftware; we draw at x)
  bool rotate = false;  // the image turned by angle - angle_base (radians, clockwise on the screen) about its
  float angle = 0, angle_base = 0;  // middle: angle is what's interpolated, angle_base the image's own
  int16_t clip_top = 0, clip_bottom = 200;  // the game's clipping (lines it may draw into)
};

// A sprite image: colour indices (0 = transparent), ox / oy from where the game was told to draw it.
struct Image {
  int w = 0, h = 0, ox = 0, oy = 0;
  std::vector<uint8_t> pixels;
};

// On / off (setting "remaster", F8 in a game). Off: the hooks let the game draw as always.
bool Enabled();  // on, and there is a renderer (SetRenderer)
void SetRenderer(bool have);
void Toggle();
void LoadSettings();

// Capture (from hooks, inside the CPU's run).
void BeginFrame(uint32_t screen);
void Add(uint32_t screen, Item item);
bool HaveImage(uint64_t id);
void SetImage(uint64_t id, Image image);
// Not capturing (another screen than play): the next frames are shown as the ST drew them.
void Stop();
// Not capturing, but the game keeps its last picture (paused): the last frame's items stay, still.
void Hold();

// A call of a game routine in a throwaway copy of the machine (outside the CPU's run: between frames): registers
// as given (a1 = the screen to draw into, cleared first; a7 = a stack below the given one), words poked first.
// Returns the non-zero pixels it drew, relative to (at_x, at_y).
struct SandboxCall {
  uint32_t routine = 0;
  uint32_t regs[16] = {};  // d0-d7, a0-a7
  std::vector<std::pair<uint32_t, uint16_t>> pokes;
  int at_x = 0, at_y = 0;
};
bool DrawInSandbox(st::Machine* m, const SandboxCall& call, Image* out);
// Colour indices of a 4-plane block in RAM (ST interleaved, `groups` 16-pixel groups wide, `rows` high).
Image DecodePlanes(const uint8_t* data, int groups, int rows);

// After every RunFrame: the screen the machine showed this frame (Machine::FrameBase).
void FrameShown(uint32_t screen);
// Something to draw this frame (capturing, on, a frame shown).
bool Active();
// Draws the picture into `view` (logical coordinates) through crt::DrawPicture: the ST frame `st_texture` at 4x
// with the items over it, positions `alpha` (0..1) of the way from the previous frame shown to this one.
void Draw(SDL_Renderer* renderer, SDL_Texture* st_texture, const SDL_FRect& view, float alpha, const uint16_t* palette,
          bool ste_colours);
void Shutdown();
// Tests: this frame composed in software at 1x, whole pixels, no rotation (alpha 1) over the ST frame `st`
// (320 x 200 ARGB): what the game itself would have drawn, if the capture is right.
void LogFrame();  // tests: this frame's items
void ComposeSoftware(const uint32_t* st, const uint16_t* palette, bool ste_colours, std::vector<uint32_t>* out);

}  // namespace remaster
