// Rumble on the big moments (owner 2026-11-07): losing a life (a long, heavy one) and smart bombs (a short,
// sharp one), read from each game's RAM every frame. Pad 1. Setting rumble (1; the panel's GENERAL). Trip-A-Tron
// has its own (music beats, tripatron.cpp).
#pragma once

#include <SDL3/SDL.h>

#include <string>

#include "machine.h"

namespace rumble {

void Start(const std::string& game_id);  // a game starts (its id; others: nothing)
void Frame(const st::Machine& m, SDL_Gamepad* pad);  // after each frame the real machine ran

}  // namespace rumble
