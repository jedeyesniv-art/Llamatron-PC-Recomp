// Pad glyphs in the 8 x 8 debug text (glyphs.h).
#include "glyphs.h"

#include <algorithm>

namespace glyphs {
namespace {

struct Glyph {
  char code;
  const char* rows[8];
  SDL_Color colour;  // alpha 0: the text's colour
};

// PlayStation's symbols in their colours (a little lighter, to read on black).
const Glyph kGlyphs[] = {
    {kCross, {"........", "#.....#.", ".#...#..", "..#.#...", "...#....", "..#.#...", ".#...#..", "#.....#."},
     {120, 170, 255, 255}},
    {kCircle, {"........", "..###...", ".#...#..", "#.....#.", "#.....#.", "#.....#.", ".#...#..", "..###..."},
     {255, 105, 110, 255}},
    {kSquare, {"........", "........", "######..", "#....#..", "#....#..", "#....#..", "#....#..", "######.."},
     {235, 140, 220, 255}},
    {kTriangle, {"........", "...#....", "..#.#...", "..#.#...", ".#...#..", ".#...#..", "#.....#.", "#######."},
     {70, 225, 170, 255}},
    {kOptions, {"........", "........", ".######.", "........", ".######.", "........", ".######.", "........"},
     {0, 0, 0, 0}},
    {kShare, {"...#....", "..###...", ".#.#.#..", "...#....", "...#....", "...#....", "........", "........"},
     {0, 0, 0, 0}},
};

const Glyph* Find(char c) {
  for (const Glyph& g : kGlyphs)
    if (g.code == c) return &g;
  return nullptr;
}

}  // namespace

void Text(SDL_Renderer* r, float x, float y, const std::string& text) {
  size_t run = 0;
  for (size_t i = 0; i <= text.size(); ++i) {
    const Glyph* g = i < text.size() ? Find(text[i]) : nullptr;
    if (i < text.size() && !g) continue;
    if (i > run) SDL_RenderDebugText(r, x + float(run) * 8, y, text.substr(run, i - run).c_str());
    run = i + 1;
    if (!g) continue;
    Uint8 cr, cg, cb, ca;
    SDL_GetRenderDrawColor(r, &cr, &cg, &cb, &ca);
    if (g->colour.a) {  // the pad's colour, as bright as the text (dimmed lines stay dim)
      float k = float(std::max({cr, cg, cb})) / 255.0f;
      SDL_SetRenderDrawColor(r, Uint8(g->colour.r * k), Uint8(g->colour.g * k), Uint8(g->colour.b * k), ca);
    }
    const float gx = x + float(i) * 8;
    for (int row = 0; row < 8; ++row)
      for (int col = 0; col < 8; ++col)
        if (g->rows[row][col] == '#') {
          SDL_FRect px{gx + float(col), y + float(row), 1, 1};
          SDL_RenderFillRect(r, &px);
        }
    SDL_SetRenderDrawColor(r, cr, cg, cb, ca);
  }
}

}  // namespace glyphs
