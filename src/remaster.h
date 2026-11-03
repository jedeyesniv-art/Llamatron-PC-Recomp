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
#include <functional>
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
  int16_t clip_left = 0, clip_right = 319;  // and columns
  // Matching between frames: within a key group (the key without Add's count), each item takes the nearest one
  // of the frame before. By default by x / y; `world`: by wx / wy (where the game keeps it, before scrolling;
  // x wraps at wrap_x if not 0), so a scrolling landscape of alike tiles matches whatever the scroll speed.
  bool world = false;
  float wx = 0, wy = 0;
  uint16_t wrap_x = 0;
  // Shown only (ComposeSoftware ignores them): where it really is between the game's whole pixels, for things the
  // game moves less than a pixel a frame (it steps every few frames).
  float fine_x = 0, fine_y = 0;
  // A repeating pattern (image a whole number of periods wide): x jumps back by `period` when it wraps, so it slides
  // the short way round.
  uint16_t period = 0;
};

// A sprite image: colour indices (0 = transparent, kBlack = colour 0 drawn), ox / oy from where the game was told to
// draw it.
constexpr uint8_t kBlack = 16;
struct Image {
  int w = 0, h = 0, ox = 0, oy = 0;
  std::vector<uint8_t> pixels;
  bool opaque = false;  // SetImage: every 0 becomes kBlack (the game writes whole words, black included)
};

// On / off (setting "remaster", F8 in a game). Off: the hooks let the game draw as always.
bool Enabled();  // on, and there is a renderer (SetRenderer)
void SetRenderer(bool have);
void Toggle();
void LoadSettings();
// Run-ahead (setting remaster_ahead 0..3, F7 cycles): how many frames earlier than the ST picture the objects
// are shown. 0: the list drawn into the screen shown (in step with the ST picture); 1: the list the game
// finished last (the ST shows it at the next VBL); 2 / 3: plus 1 / 2 frames run in a copy of the machine with
// the controls held, captured on their own (the game side's C++ state saved / restored around it).
void CycleAhead();
int Ahead();
void SetAhead(int frames);
void SetAheadState(std::function<void()> save, std::function<void()> restore);
// After each RunFrame, before FrameShown.
void RunAhead(st::Machine* m);
// A line to show for a moment after F7 / F8 (nullptr: none).
const char* Message();
void SetProbe(bool on);  // tests: log the rotated item's angle each frame shown

// Capture (from hooks, inside the CPU's run).
void BeginFrame(uint32_t screen);
void EndFrame(uint32_t screen);  // the draw pass for `screen` is complete
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
  // The routine clears what is under it (a mask over a rectangle, not the shape): run it twice, onto colour 0 and
  // colour 15, and keep what it wrote, black (kBlack) included.
  bool opaque_black = false;
};
bool DrawInSandbox(st::Machine* m, const SandboxCall& call, Image* out);
// Helpers for the capture hooks (shared by the games).
// A sprite seen for the first time: its image drawn in the sandbox between frames (DrawQueued, from the game's
// Update, never inside the CPU's run: Musashi isn't re-entrant). Queued once per id.
void QueueImage(uint64_t id, const SandboxCall& call);
void DrawQueued(st::Machine* m, const char* game);
// The machine is the sprite decoder's sandbox (not a run-ahead copy): hooks do only the original's work there.
inline bool Decoding(const st::Machine& m) { return m.sandbox && !m.ahead; }
// An item key from the drawing code (a return address) and the object (0: none).
inline uint64_t Key(uint32_t caller, uint32_t object) { return uint64_t(caller & 0xFFFFFF) << 32 | (object & 0xFFFFFF); }
// Hooks: return from the routine (the 68000's rts).
void Return(st::Machine& m);

// Colour indices of a 4-plane block in RAM (ST interleaved, `groups` 16-pixel groups wide, `rows` high).
Image DecodePlanes(const uint8_t* data, int groups, int rows);

// After every RunFrame: the screen the machine showed this frame (Machine::FrameBase).
void FrameShown(uint32_t screen);
// Something to draw this frame (capturing, on, a frame shown).
bool Active();
// Draws the picture into `view` (logical coordinates) through crt::DrawPicture: the ST frame `st_texture` at 4x
// with the items over it, positions `alpha` (0..1) of the way from the previous frame shown to this one. The items
// take the colours of the lines they are on (`line_palettes`: Machine::LinePalettes, 200 x 16): an image using a
// colour that rasters change within the frame is drawn row by row.
void Draw(SDL_Renderer* renderer, SDL_Texture* st_texture, const SDL_FRect& view, float alpha,
          const uint16_t* line_palettes, bool ste_colours);
void Shutdown();
// Colour 0 as the machine shows it, when that isn't the register (Machine::CalmBackground: flashes filtered): used
// for every line's colour 0 (drawn black). Before Draw / ComposeSoftware.
void SetShownColour0(bool on, uint32_t argb);
// Tests: this frame composed in software at 1x, whole pixels, no rotation (alpha 1) over the ST frame `st`
// (320 x 200 ARGB): what the game itself would have drawn, if the capture is right.
void LogFrame();  // tests: this frame's items
void ComposeSoftware(const uint32_t* st, const uint16_t* line_palettes, bool ste_colours, std::vector<uint32_t>* out);

}  // namespace remaster
