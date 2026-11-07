// The launcher's live previews (owner 2026-11-07: "projected or dreamlike, around 40% opacity, subtle and
// strange"): the picked disk's game runs unseen behind the carousel - booted, walked past its crack / shareware
// screens with scripted input, then left to its own attract mode - and its picture is shown faintly behind the
// disks: soft, with trails, breathing, its edges lost in the dark. No sound. Each game's warmed-up machine is
// kept (a snapshot) so coming back to a disk shows it at once; spare time warms the others.
#pragma once

#include <SDL3/SDL.h>

#include <filesystem>
#include <string>
#include <vector>

#include "games.h"

namespace preview {

void Init(SDL_Renderer* renderer, const std::filesystem::path& userdata);
void SetGames(const std::vector<games::Game>& games);  // all of them (warmed up in spare time, one by one)
// The disk in the middle, settled (nullptr: none - turning, launching, the Settings disk...).
void Select(const games::Game* game);
// Once per launcher frame: warm-up slices (a few ms) and the shown game's frames at its own 50 / 60 Hz.
void Update(double dt);
// The layer behind the disks (after the clear): w x h = the output size.
void Draw(int w, int h);
// Leaving the launcher for a game: the running copy stops (the snapshots stay, for coming back).
void Pause();
void Shutdown();

// Tests: warms `id` up and saves its raw frames every `step` frames for `count` shots to <dir>/<id>_<n>.bmp.
bool Test(const std::vector<games::Game>& games, const std::string& id, int step, int count, const std::string& dir);

}  // namespace preview
