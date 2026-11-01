// The start screen: Atari ST floppy disks (low-poly, drawn with SDL_RenderGeometry) under a spotlight,
// hand-labelled "Llamatron", "Super Gridrunner" and "Trip-A-Tron". Left / right picks one, A plays it, Y shows
// how to play it, B (twice) quits.
#pragma once

#include <SDL3/SDL.h>

namespace launcher {

enum class Action { kNone, kPlay, kQuit };
enum Game { kLlamatron = 0, kGridrunner = 1, kTripATron = 2 };
constexpr int kGames = 3;

class Launcher {
 public:
  explicit Launcher(SDL_Renderer* renderer);
  ~Launcher();

  // Shows the launcher (again) with this disk picked.
  void Enter(int game);
  int Selected() const { return selected_; }

  Action HandleEvent(const SDL_Event& e);
  // Once per displayed frame: the pad's stick, animation. Returns kPlay once the "insert disk" animation is done.
  Action Update(SDL_Gamepad* pad, double dt);
  void Render();

 private:
  void Choose(int game);
  void BuildLabels();
  void DrawDisk(int game, float w, float h);
  void DrawText(const char* s, float x, float y, float scale, SDL_Color c, bool centred);

  SDL_Renderer* r_;
  SDL_Texture* white_add_ = nullptr;  // 1x1 white, additive (light)
  SDL_Texture* white_ = nullptr;      // 1x1 white, alpha blended
  SDL_Texture* labels_[kGames] = {};
  int selected_ = 0;
  float sel_anim_[kGames] = {1};  // 0..1, follows the selection
  double time_ = 0;
  double fade_in_ = 0;      // 0..1 at the start
  double launching_ = -1;   // >= 0: the "insert disk" animation runs (seconds)
  bool info_ = false;       // the how-to-play page is up
  uint64_t quit_armed_ms_ = 0;
  int stick_latch_ = 0;     // stick held left / right: one move per push
};

}  // namespace launcher
