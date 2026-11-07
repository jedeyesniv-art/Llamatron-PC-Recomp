// The CRT look (owner's request, tuned to their ShaderGlass crt-yah profile): a pixel shader on SDL's GPU
// renderer (src/shaders/crt.frag.hlsl, DXIL -> D3D12). On / off from the launcher (X) or F9 in a game, saved as
// crt=1 in settings.txt with the crt_* numbers. Without the GPU renderer the picture is drawn plain.
#pragma once

#include <SDL3/SDL.h>

#include <string>

namespace crt {

// Asks for the GPU renderer that can take our DXIL shader; falls back to SDL's default renderer.
SDL_Renderer* CreateRenderer(SDL_Window* window);
// After CreateRenderer: builds the shader (false if this renderer can't run it).
bool Init(SDL_Renderer* renderer);
void Shutdown();

bool Available();
bool Enabled();  // on and available
int Mode();      // 0 off (or unavailable), 1 CRT+, 2 CRT++ (psychedelic trails, colour bleed, more glow)
void Toggle();   // cycles off / CRT+ / CRT++ and saves the setting
void LoadSettings();
// The game running ("" = none: the launcher, the Settings disk). Each game has its own look (<id>_crt) and
// CRT++ sliders (<id>_crt_psy_*) once changed in it; until then, per key, the shared ones (crt, crt_psy_*).
void SetGame(const std::string& id);
// A game's built-in look for a key ("crt", "crt_psy_..."): the owner's tuning, used when the player hasn't set
// the game's own (<game>_<key>). False = none: the shared key.
bool GameDefaultValue(const std::string& game, const char* key, double* value);

// Draws the ST picture into view (logical coordinates covering the logical presentation) with the CRT look
// when it's on, else as it is. src_w / src_h: the picture's ST resolution when the texture is bigger (the
// remaster's 4x picture: the scanlines stay the ST's); 0 = the texture's size.
void DrawPicture(SDL_Renderer* renderer, SDL_Texture* texture, const SDL_FRect& view, float src_w = 0, float src_h = 0);

}  // namespace crt
