// Trip-A-Tron (TRIP.PRG, Jeff Minter 1988; shareware 1992): a light synthesiser, played with the mouse (where
// the light source is; left button held = pattern generation) and the keyboard (letters = effects, the cursor
// cluster = symmetry, the numeric pad = palettes / strobes, the number row = palettes (demo 1) or lasers (demo
// 2), Space = panic, Esc = the Control Panel, F10 = the other demo, F1-F6 = the plot routine switches).
// On a pad, while performing (state $108B0 = 0) - an instrument: a base layer, plus hold-to-choose wheels and
// a layer on Y, each drawn as an overlay while held (DrawOverlay):
//   left stick        the light (relative, through a curve; trip_flow adds inertia for long glides)
//   right stick       "spirograph": the stick's position is the screen position (trip_spiro_size)
//   RT (analog)       paint: the squeeze sets the density (light = a pattern every few frames, full = every frame:
//                     the Global Parameters' ON / OFF time played live; trip_pressure 0 = on / off)
//   LT                the right mouse button (Global Parameters' "button 2": VECTOR / CENTRE / KML #128)
//   R3                latch painting on / off          L3 (or F12)  music: off / pulse / full
//   d-pad left/right  previous / next effect (the Q, A and Z keyboard rows)
//   d-pad up/down     previous / next symmetry (the cursor cluster)
//   LB / RB tap       previous / next palette (numeric pad)
//   LB held           effect wheel: right stick picks a key of a row, d-pad left / right changes row, release
//   RB held           symmetry wheel: the 8 symmetries round the stick; Y as well = the pre-symmetry
//   Y tap             the Control Panel (Esc)
//   Y held            the plot routine layer: LB / A / X / B / Start / RB = F1-F6 (pre-sym, remap, cube, shear,
//                     resize, post-sym); d-pad up / down = the pre-symmetry (Delete + a symmetry key)
//   A                 surprise me (effect + symmetry + palette)     X  panic (Space)
//   B                 next number-row key (1 ... 0 - =)             Start  the other demo (F10)
//   Back              the settings panel (main.cpp; leaving to the disks is there)
//   gyro              (trip_gyro) turning the pad moves the light / pointer, a pen-like control
// On the title, the Control Panel and the edit screens (state != 0) the pad is a mouse: either stick moves the
// pointer (LB held: slowly), A / RT = left button, X / LT = right button, Y = Esc; on the Control Panel the d-pad
// jumps between its icons and the hint names each with what A and X do there; in the edit screens it nudges the
// pointer a pixel. B = back towards the Control Panel (an edit screen's own exit button clicked, an alert's
// button, a side panel's OK / GROOVY; on the bare panel: put it away). The keyboard and PC mouse work throughout
// (F1 is the ST's: no settings key here).
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
// The settings panel closed: buttons held now make no presses.
void Resync();
// The wheels / the Y layer over the picture (ST logical space: 320 x view_h), after the hint line.
void DrawOverlay(SDL_Renderer* r, float view_h);
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
