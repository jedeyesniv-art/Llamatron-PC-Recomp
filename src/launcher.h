// The start screen: Atari ST floppy disks (low-poly, drawn with SDL_RenderGeometry) on a carousel - the picked
// one in the middle under a pool of light, its neighbours shrinking away to the sides - hand-labelled. Left /
// right turns the carousel, A plays the picked disk, Y shows how to play it (and its options: trainers, a user
// disk's program; up / down picks one, left / right changes it), B (twice) quits.
#pragma once

#include <SDL3/SDL.h>

#include <vector>

#include "games.h"

namespace launcher {

enum class Action { kNone, kPlay, kQuit };

class Launcher {
 public:
  Launcher(SDL_Renderer* renderer, const std::vector<games::Game>* games);
  ~Launcher();

  // Shows the launcher (again) with this disk picked.
  void Enter(int game);
  int Selected() const { return selected_; }

  Action HandleEvent(const SDL_Event& e);
  // Once per displayed frame: the pad's stick, animation. Returns kPlay once the "insert disk" animation is done.
  Action Update(SDL_Gamepad* pad, double dt);
  void Render();

 private:
  int Count() const { return int(games_->size()); }
  void Step(int dir);  // turn the carousel one disk
  void BuildLabels();
  SDL_Texture* StandInLabel(const games::Game& g);
  void DrawDisk(int game, float w, float h);
  void DrawText(const char* s, float x, float y, float scale, SDL_Color c, bool centred);
  float Offset(int game) const;  // where a disk is on the carousel: 0 middle, -1 / +1 its neighbours...
  void ChangeOption(int dir);

  SDL_Renderer* r_;
  const std::vector<games::Game>* games_;
  SDL_Texture* white_add_ = nullptr;  // 1x1 white, additive (light)
  SDL_Texture* white_ = nullptr;      // 1x1 white, alpha blended
  std::vector<SDL_Texture*> labels_;
  int selected_ = 0;
  float scroll_ = 0;        // the carousel's position (follows selected_, in disks; may run past the ends)
  float target_ = 0;        // where it's going (selected_ plus whole turns)
  double time_ = 0;
  double fade_in_ = 0;      // 0..1 at the start
  double launching_ = -1;   // >= 0: the "insert disk" animation runs (seconds)
  bool info_ = false;       // the how-to-play page is up
  int option_row_ = 0;      // the option picked on it
  uint64_t quit_armed_ms_ = 0;
  int stick_latch_ = 0;     // stick held left / right: one move per push
  int stick_latch_y_ = 0;
};

}  // namespace launcher
