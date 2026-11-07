// Per-game button remapping (owner 2026-11-07, PLAN-CONTROLS.md). Each game has a list of its own functions,
// each the game's own (logical) buttons: FIRE = "whatever the game does with A". The player attaches physical
// buttons (face, shoulders, triggers, d-pad, stick clicks; never Start / Back / Guide) to functions in the
// settings panel; underneath, a physical button then reads as the function's first default button, in every
// screen of that game. A default button attached to no function does nothing; a button no function lists is left
// as it is. Only pad 1, only in a game (the launcher and the panel see the physical buttons). Scripted test input
// bypasses it unless --remap-test.
// Saved per game (and control scheme for Andes / Defender 2): <prefix>_controls_<function> = a bit mask of
// physical inputs (bit = SDL_GamepadButton, kLt / kRt for the triggers); no keys = the defaults.
#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <string>
#include <vector>

#include "input.h"

namespace remap {

constexpr int kLt = 30, kRt = 31;  // the triggers as inputs (bits of a mask; buttons are SDL_GamepadButton)

struct Function {
  const char* key;    // settings key part
  const char* label;  // the panel's row
  std::vector<int> defaults;  // the game's own buttons for it (the first: what attached buttons act as)
  const char* help;
  uint32_t DefaultMask() const;
};

// The game being played (in-game only; "" = none: no remapping). Mode tells user disks (generic controls).
void SetGame(const std::string& id, input::Mode mode);
const std::string& Game();
// Rereads the saved map (after a settings change: the control scheme may have changed too).
void Reload();

// The current game's functions (for its control scheme) and what's attached to each.
const std::vector<Function>& Functions();
uint32_t Attached(size_t fn);
// The physical input attached to fn, or taken off it if it was there (a button belongs to one function).
void Toggle(size_t fn, int input);
void ResetFunction(size_t fn);  // back to its defaults (taken from other functions)
void ResetAll();
bool Changed();                       // the current game's map isn't the default
bool Changed(const std::string& id, input::Mode mode);  // (the how-to-play page)
// Can it be attached (not Start / Back / Guide)? Its short name ("A", "LB", "LT", "D-UP"...).
bool Attachable(int input);
std::string InputName(int input);
std::string MaskNames(uint32_t mask);

// Physical -> logical, for pad 1 while a game is set. Logical state of the raw pad: buttons mask + triggers.
void Apply(uint32_t* buttons, float* lt, float* rt);
bool Active();  // a game set with a map that isn't the default
bool IsFirstPad(SDL_Gamepad* p);
// The logical button / axis of a pad (pad 1 remapped; others, and sticks, as they are).
bool Button(SDL_Gamepad* p, SDL_GamepadButton b);
float Axis(SDL_Gamepad* p, SDL_GamepadAxis a);  // -1..1 (triggers 0..1)

// Button events: while Active, pad 1's real button events are dropped (Start / Back / Guide kept) and the
// logical ones made from the state each frame instead (no duplicate downs when several buttons share one).
bool DropEvent(const SDL_Event& e);
std::vector<SDL_Event> Events(SDL_Gamepad* first);
void SyncEvents(SDL_Gamepad* first);  // no edges from what's held now (the panel closed)

// Tests: --remap-test sends the scripted pad through the map too.
void SetTestThrough(bool on);
bool TestThrough();
// A scripted pad<button> event: its button made the logical one (with --remap-test). False: dropped.
bool TranslateTest(SDL_Event* e);

}  // namespace remap
