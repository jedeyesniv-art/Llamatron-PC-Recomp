// The CRT look (owner's request, tuned to their ShaderGlass crt-yah profile): a pixel shader on SDL's GPU
// renderer (src/shaders/crt.frag.hlsl, DXIL -> D3D12). On / off from the launcher (X) or F9 in a game, saved as
// crt=1 in settings.txt with the crt_* numbers. Without the GPU renderer the picture is drawn plain.
#pragma once

#include <SDL3/SDL.h>

namespace crt {

// Asks for the GPU renderer that can take our DXIL shader; falls back to SDL's default renderer.
SDL_Renderer* CreateRenderer(SDL_Window* window);
// After CreateRenderer: builds the shader (false if this renderer can't run it).
bool Init(SDL_Renderer* renderer);
void Shutdown();

bool Available();
bool Enabled();  // on and available
void Toggle();   // and saves the setting
void LoadSettings();

// Draws the ST picture into view (logical coordinates covering the logical presentation) with the CRT look
// when it's on, else as it is.
void DrawPicture(SDL_Renderer* renderer, SDL_Texture* texture, const SDL_FRect& view);

}  // namespace crt
