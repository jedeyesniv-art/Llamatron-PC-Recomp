// Trip-A-Tron (TRIP.PRG, Jeff Minter 1992): a light synthesiser, played with the mouse (where the light source
// is; left button held = pattern generation) and the keyboard (letters = effects, the cursor cluster = symmetry,
// the numeric pad = palettes, Space = stop, Esc = the Control Panel, F10 = the starfield demos, number keys =
// waveforms there).
// On a pad, while performing (state $108B0 = 0):
//   left stick        moves the light source (relative, through a curve: settings trip_stick_*)
//   right stick       "spirograph": the stick's position is the screen position (centre = middle of the
//                     screen; trip_spiro_size = how much of the screen full deflection reaches)
//   RT (hold)         generate patterns              LT   latch generation on / off
//   d-pad left/right  previous / next effect (A-Z)   d-pad up/down   previous / next symmetry
//   LB / RB           previous / next palette        A    random effect + symmetry + palette
//   X                 stop all patterns (Space)      Y    the Control Panel (Esc)
//   Start             the starfield demos (F10)      Back next waveform (number keys)
//   B twice           back to the launcher
//   L3 (or F12)       music: off / pulse / full - reacts to whatever the PC plays, as our Neon port does
// On the title, the Control Panel and the edit screens (state != 0) the pad is a mouse: either stick moves the
// pointer, A / RT = left button, X / LT = right button, Y = Esc. The keyboard and PC mouse work throughout.
#pragma once

#include <SDL3/SDL.h>

#include <cstdint>

#include "machine.h"

namespace trip {

// The program's state ($108B0): 0 performing, 1 title, 2 the Control Panel, others edit screens.
int State(st::Machine& m);

// Rereads the trip_* settings (on start and when settings.txt changes).
void LoadSettings();

// Once per frame: pad 1 (may be null) and the host mouse's movement and buttons (bit 1 left, bit 0 right).
void Update(st::Machine* m, SDL_Gamepad* pad, int mouse_dx, int mouse_dy, uint8_t mouse_buttons);
const char* Hint(st::Machine* m, bool have_pad);
bool QuitRequested();
// Music modes (music.h): 0 off, 1 pulse (beats pulse pattern generation, the source orbits to the bass and mids
// around a centre the player moves), 2 full (also palette / symmetry / effect changes on accents, a clear screen
// when the music stops). L3 or F12 cycles them (saved: trip_music).
void MusicKey();
int MusicMode();
void SetMusicMode(int mode);  // for this run (tests), not saved
void Reset();

// Tests: a scripted pad for the next Update (sticks -1..1, triggers 0..1, buttons: bit = SDL_GamepadButton).
struct TestPad {
  float lx = 0, ly = 0, rx = 0, ry = 0, lt = 0, rt = 0;
  uint32_t buttons = 0;
};
void SetTestPad(const TestPad& t);

}  // namespace trip
