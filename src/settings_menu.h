// The settings panel (owner 2026-11-03): every setting worth changing, in the 8x8 system font, drawn on the left
// half of the screen so the picture to the right shows what a change does. Opened by the Settings disk on the
// carousel (every section) or in a game by Back / F1 (the emulation is suspended; general, CRT and that game's
// section). Up / down pick, left / right change (held: repeats), A toggles, X back to the default, LB / RB jump
// a section; Back / F1 resume, B / Esc = in a game quit to the disks, on the Settings disk back to them.
// Changes are saved at once (settings.txt) and applied through the callback.
#pragma once

#include <SDL3/SDL.h>

#include <functional>
#include <string>

namespace menu {

enum class Result { kNone, kClose, kQuit };

// Rereads every module's settings after a change.
void SetApply(std::function<void()> apply);
// For the fullscreen row.
void SetWindow(SDL_Window* window);

// game_id empty: the Settings disk (all sections); else that game's (general, CRT, its own).
void Open(const std::string& game_id, const std::string& title);
void Close();
bool IsOpen();

Result HandleEvent(const SDL_Event& e);
// Once per displayed frame: the pad's d-pad and stick (held = repeat).
void Update(SDL_Gamepad* pad, double dt);
// Over whatever is drawn, at the output's resolution (the logical presentation is set aside meanwhile).
void Render(SDL_Renderer* r);

}  // namespace menu
