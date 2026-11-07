// SDL's 8 x 8 debug text, plus pad glyphs (owner 2026-11-07: PlayStation's names as words ran off the screen):
// one character cell each, in the text - kCross / kCircle / kSquare / kTriangle in their pad colours, kOptions (three
// lines), kShare (an arrow up) in the text's colour. remap::InputName returns them for a PlayStation pad.
#pragma once

#include <SDL3/SDL.h>

#include <string>

namespace glyphs {

constexpr char kCross = '\x01', kCircle = '\x02', kSquare = '\x03', kTriangle = '\x04', kOptions = '\x05',
               kShare = '\x06';

// As SDL_RenderDebugText (the draw colour, the render scale), the glyphs drawn where their codes are.
void Text(SDL_Renderer* r, float x, float y, const std::string& text);

}  // namespace glyphs
