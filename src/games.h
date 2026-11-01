// The games the launcher offers: the Llamasoft ST games we know (each with its own controls, save folder and
// how-to-play page) and any other disk image the player drops into game\ (generic controls, the program on it
// chosen on the how-to-play page). Found at startup by looking through game\ next to the exe, the exe's folder
// and the nearest Private\ up the tree (a few folders deep).
#pragma once

#include <SDL3/SDL.h>

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "disk.h"
#include "input.h"

namespace games {

struct Game {
  std::string id;       // short name: settings keys, the save folder (userdata\<id>), the built-in label
  std::string name;     // the launcher's caption
  std::string program;  // the program to run (upper case, as on the disk)
  std::string install;  // optional: an installer run first, writing `program` into the save folder
  bool shared_saves = false;  // saves in userdata\ itself (Llamatron and Gridrunner: where they always were)
  input::Mode mode = input::Mode::kGeneric;
  SDL_FColor body{0.25f, 0.25f, 0.28f, 1}, band{0.8f, 0.8f, 0.8f, 1};
  std::vector<std::string> help;  // the how-to-play page; the first line is its title
  // The faster CPU (--cpu) while this long in RAM holds this value (its VBL's play-state hook): only where the
  // game logic is timed by the VBL, so only the drawing speeds up. 0 = never.
  uint32_t play_at = 0, play_value = 0, play_value2 = 0, play_value3 = 0;  // any of these (0 = unused, but the first)
  bool play_word = false;  // play_at is a word, not a long
  bool ste = false;        // an STE program (STE colours; DMA sound is always there)
  // Boot the ST at 60 Hz when the setting <id>_60hz is 1 (its default): for games that read the sync register
  // at boot and adjust their speeds for 60 Hz (more frames, the same game speed).
  bool boot_60hz = false;
  uint32_t play_vbl = 0;   // and the VBL vector ($70) is this (the game is running, not still loading)
  // Its docs: a reader program on the disk (run like the game, docs controls; docs_keys typed into it first,
  // e.g. a menu choice), or a plain text file in the save folder that the launcher shows itself.
  std::string docs_program, docs_keys, docs_text;
  bool user = false;    // a disk the player added (not one we know); its "program" option picks what runs
  // Choices on the how-to-play page (trainers, a user disk's program), saved as settings <id>_<key>.
  struct Option {
    std::string key, label;
    std::vector<std::string> values;
  };
  std::vector<Option> options;
  // Where it was found: a disk image, or a folder of loose files.
  std::filesystem::path source;
  std::filesystem::path label_picture;  // a user disk: <image name>.png next to it, if there is one
};

// Looks for the games. `disk` (--disk) is used for the games it holds instead of searching.
std::vector<Game> Discover(const std::string& disk);

// The files of a game's disk image or folder, keyed by upper-case name (root directory only).
std::map<std::string, disk::Bytes> ReadSource(const std::filesystem::path& source, std::string* error);

// An option's value (index into its values; 0 if the game has no such option), and changing it (saved).
int OptionValue(const Game& g, const std::string& key);
void SetOption(const Game& g, const std::string& key, int value);
// Typed into the console before the program starts: answers to a crack's prompts, from the options.
std::string ConsoleAnswers(const Game& g);
// What runs: a user disk's chosen program, else the game's.
std::string ProgramToRun(const Game& g);
// Where its saves go: userdata\ itself, userdata\<id>, or userdata\disks\<image name> for a user disk.
std::filesystem::path SaveDir(const Game& g, const std::filesystem::path& userdata);

// The folders searched (game\, the exe's folder, Private\ up the tree).
std::vector<std::filesystem::path> SearchRoots();
std::filesystem::path ExeDir();

}  // namespace games
