#include "games.h"

#include <algorithm>
#include <cstdint>
#include <set>

#include "log.h"
#include "settings.h"

namespace fs = std::filesystem;

namespace games {
namespace {

std::string Upper(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::toupper(c)); });
  return s;
}

const char* const kGenericHelp[] = {
    "LEFT STICK / D-PAD   joystick",
    "A                    fire",
    "RIGHT STICK          mouse   (RT left button, LT right)",
    "START  Space         BACK  Esc",
    "B TWICE              back to the disks", "",
    "Keyboard and mouse: as on the ST.",
};

struct Known {
  Game game;
  std::string identify;  // a file on its disk (or in its folder) that tells it's this game
  bool folder;           // loose files in a folder, not a disk image
};

std::vector<Known> KnownGames() {
  std::vector<Known> k;
  auto add = [&](Game g, std::string identify, bool folder = false) { k.push_back({std::move(g), identify, folder}); };
  Game g;

  g = {};
  g.id = "llamatron", g.name = "LLAMATRON", g.program = "TRON1MEG.PRG", g.shared_saves = true;
  g.mode = input::Mode::kLlamatron, g.body = {0.12f, 0.24f, 0.62f, 1}, g.band = {0.95f, 0.55f, 0.12f, 1};
  g.help = {"LLAMATRON  -  JEFF MINTER, 1991", "",
            "Rescue the beasties. Kill everything else.", "",
            "LEFT STICK / D-PAD   move",
            "RIGHT STICK          aim (twin-stick game)",
            "A AT THE TITLE       start a twin-stick game",
            "X AT THE TITLE       start a one-stick game",
            "A IN PLAY            fire / lock the fire angle",
            "RB AT THE TITLE      analog aim on / off",
            "BACK                 pause    (Y: the tentacle)",
            "B ON THE PAUSE SCREEN  quit to the title",
            "B TWICE AT THE TITLE   back to the disks", "",
            "Keyboard: WASD + Space, cursors + Enter, P pause"};
  add(g, "TRON1MEG.PRG");

  g = {};
  g.id = "gridrunner", g.name = "SUPER GRIDRUNNER", g.program = "GRIDRUNR.PRG", g.shared_saves = true;
  g.mode = input::Mode::kGridrunner, g.body = {0.60f, 0.10f, 0.10f, 1}, g.band = {0.62f, 0.25f, 0.85f, 1};
  g.help = {"SUPER GRIDRUNNER  -  JEFF MINTER", "",
            "Shoot the snakes. Mind the zappers.", "",
            "LEFT STICK           move the ship (d-pad: slowly)",
            "RT OR A              fire",
            "LT (HOLD)            nosecone back to the ship",
            "RIGHT STICK FLICK    nosecone shot direction",
            "RB / LB              nosecone shot up / down",
            "Y                    voluntary martyrdom (a life!)",
            "BACK                 pause",
            "B ON THE PAUSE SCREEN  quit to the title",
            "TITLE: X OPTIONS   Y GAME SECRETS   LB MORE INFO",
            "B TWICE AT THE TITLE   back to the disks", "",
            "The mouse works too: the original controls."};
  add(g, "GRIDRUNR.PRG");

  g = {};
  g.id = "tripatron", g.name = "TRIP-A-TRON", g.program = "TRIP.PRG", g.install = "TRIP1.TOS";
  g.mode = input::Mode::kTripATron, g.body = {0.10f, 0.42f, 0.22f, 1}, g.band = {0.95f, 0.30f, 0.75f, 1};
  g.help = {"TRIP-A-TRON  -  JEFF MINTER, 1992", "",
            "A light synthesiser: play light to your music.", "",
            "LEFT STICK      move the light source",
            "RIGHT STICK     spirograph: stick = position",
            "RT (HOLD) / LT  make patterns / latch them on",
            "D-PAD L/R  effect          D-PAD U/D  symmetry",
            "LB / RB    palette         A  surprise me",
            "X  stop    Y  control panel    START  starfields",
            "BACK            waveforms (starfield demos)",
            "ON THE PANEL    sticks = mouse, A / X = buttons",
            "B TWICE         back to the disks", "",
            "L3 / F12   music off / pulse / full (hears the PC)",
            "Yak's README.DOC is in userdata\\tripatron."};
  g.docs_text = "README.DOC";  // written by the installer
  add(g, "TRIP1.TOS", true);

  // Being worked on (PLAN-ST-GAMES.md): generic controls for now.
  auto wip = [&](const char* id, const char* name, const char* program, const char* title, const char* blurb,
                 SDL_FColor body, SDL_FColor band) {
    Game w;
    w.id = id, w.name = name, w.program = program, w.body = body, w.band = band;
    w.help = {title, "", blurb, ""};
    w.help.insert(w.help.end(), std::begin(kGenericHelp), std::end(kGenericHelp));
    add(w, program);
  };
  // Revenge, 1 MB STE version (REVSTE1M.TOS in a folder on a Pasti .stx disk): more sprite data, so a
  // smoother camel (it animates every 1-2 frames, the 512K one every 8). Needs DMA sound and STE colours.
  // Same code as the 512K one at other addresses: hook $3DD3E, play $111DE, pause $1056C, bonus $112A2,
  // name $194A8. Docs: README.TXT on the disk (Yak's notes). Preferred: the first of an id found wins.
  wip("rmc", "MUTANT CAMELS", "REVSTE1M.TOS", "REVENGE OF THE MUTANT CAMELS  -  JEFF MINTER (1MB STE)",
      "Giant camels against the forces of evil.", {0.62f, 0.40f, 0.10f, 1}, {0.20f, 0.65f, 0.90f, 1});
  k.back().game.play_at = 0x3DD3E, k.back().game.play_value = 0x111DE;
  k.back().game.play_value2 = 0x112A2, k.back().game.play_value3 = 0x194A8;
  k.back().game.mode = input::Mode::kRmc;
  k.back().game.docs_text = "README.TXT";
  k.back().game.ste = true;
  wip("rmc", "MUTANT CAMELS", "REVENGE.TOS", "REVENGE OF THE MUTANT CAMELS  -  JEFF MINTER",
      "Giant camels against the forces of evil.", {0.62f, 0.40f, 0.10f, 1}, {0.20f, 0.65f, 0.90f, 1});
  // VBL $1B4B6 calls the hook at $42E18: $1118E play, $11252 the end-of-wave bonus, $193F0 the high score name
  // (all VBL-timed: the name's cursor ends on the same letter at x1 and x8).
  k.back().game.play_at = 0x42E18, k.back().game.play_value = 0x1118E;
  k.back().game.play_value2 = 0x11252, k.back().game.play_value3 = 0x193F0;
  k.back().game.mode = input::Mode::kRmc;
  k.back().game.docs_program = "DOX.TOS";  // D-Bug's document displayer: Yak's notes
  {  // the STE build (listed just before): the same page
    Game& ste = k[k.size() - 2].game;
    ste.help = k.back().game.help;
    ste.help[0] = "REVENGE OF THE MUTANT CAMELS  -  1 MB STE";
  }
  k.back().game.help = {"REVENGE OF THE MUTANT CAMELS  -  JEFF MINTER", "",
                        "Giant camels against the forces of evil.", "",
                        "LEFT STICK / D-PAD   move the camel",
                        "A, X, RT             fire",
                        "BACK                 pause",
                        "B ON THE PAUSE SCREEN  back to the menu",
                        "B TWICE ELSEWHERE    back to the disks", "",
                        "Keyboard: cursor keys + Space, P pause, O resume"};
  wip("defender2", "DEFENDER 2", "DEFENDR2.PRG", "DEFENDER 2  -  JEFF MINTER, ARCADIA 1990",
      "Defender, Stargate and Defender II.", {0.16f, 0.16f, 0.18f, 1}, {0.30f, 0.85f, 0.30f, 1});
  // The VBL ($4AF1A) dispatches on $4B086 through the table at $212A2: 0 play, 2 title, 8 player select (and
  // other screens). 25 frames/s by design; the faster CPU in play lets stgames.cpp draw at 50 (d2_50fps).
  k.back().game.play_at = 0x4B086, k.back().game.play_value = 0, k.back().game.play_word = true;
  k.back().game.play_vbl = 0x4AF1A;
  k.back().game.mode = input::Mode::kDefender2;
  // At boot it reads $FF820A ($1EC04): 60 Hz -> "60 hz game speeds adjusted" ($2043F = 1). The arcade ran at
  // 60; owner 2026-11-01: 60 Hz by default (setting defender2_60hz).
  k.back().game.boot_60hz = true;
  k.back().game.docs_program = "READ_ME.PP";  // The Alien's DOC displayer: the crack's notes, the controls
  k.back().game.help = {"DEFENDER 2  -  JEFF MINTER, ARCADIA 1990", "",
                        "Defender, Defender II and Stargate.", "",
                        "TITLE: D-PAD + A    pick a game; then A = 1 player",
                        "LEFT STICK UP/DOWN  climb / dive",
                        "LEFT STICK L/R      thrust (turns round first)",
                        "A OR RT             fire",
                        "Y                   smart bomb",
                        "X / LT              reverse / thrust",
                        "LB                  inviso (Stargate)",
                        "BACK                pause",
                        "INITIALS            d-pad letter, A fix, X delete",
                        "B TWICE             back to the disks", "",
                        "60 Hz, as the arcade (defender2_60hz=0: 50).",
                        "Keyboard: Ctrl up, LShift down, Return thrust,",
                        "Alt reverse, # fire, RShift smart bomb."};
  // The crack's trainer (The Alien): "T for trainer", then two Y/N questions.
  k.back().game.options = {{"invincible", "INVINCIBLE", {"OFF", "ON"}},
                           {"friendly", "FRIENDLY SPRITES", {"OFF", "ON"}}};
  wip("andes", "ANDES ATTACK", "ANDES.BOY", "ANDES ATTACK  -  JEFF MINTER",
      "Save the llamas from the invaders.", {0.42f, 0.16f, 0.52f, 1}, {0.95f, 0.85f, 0.20f, 1});
  k.back().game.play_at = 0x12EEE, k.back().game.play_value = 1, k.back().game.play_word = true;  // state: 1 play
  k.back().game.mode = input::Mode::kAndes;
  k.back().game.help = {"ANDES ATTACK  -  JEFF MINTER, 1988", "",
                        "Save the llamas from the Zzyaxian landers.", "",
                        "LEFT STICK UP/DOWN   altitude",
                        "LEFT STICK L/R       thrust (turns round first)",
                        "A OR RT              fire",
                        "Y                    smart bomb",
                        "X / LT               reverse / thrust",
                        "BACK                 pause",
                        "HIGH SCORE NAME      up/down letter, A enter it",
                        "B TWICE              back to the disks", "",
                        "Mouse: up/down, left thrust, right reverse;",
                        "Left Shift fire, Control smart bomb."};
  // Photon Storm (Arc / Llamasoft 1990), PHOTON.YAK (Automation-packed TOS program) on a compilation disk with
  // other Llamasoft .YAKs; the Andes Attack engine. State $44846 (1 play, 7 title, 10 paused).
  wip("photonstorm", "PHOTON STORM", "PHOTON.YAK", "PHOTON STORM  -  JEFF MINTER, ARC 1990",
      "Blast the alien swarms in deep space.", {0.08f, 0.10f, 0.30f, 1}, {0.95f, 0.75f, 0.20f, 1});
  k.back().game.play_at = 0x44846, k.back().game.play_value = 1, k.back().game.play_word = true;
  k.back().game.mode = input::Mode::kPhotonStorm;
  k.back().game.help = {"PHOTON STORM  -  JEFF MINTER, ARC 1990", "",
                        "Blast the alien swarms in deep space.", "",
                        "LEFT STICK           point the ship; push far to fly",
                        "A OR RT              fire",
                        "X OR LT              thrust",
                        "BACK                 pause",
                        "B ON THE PAUSE SCREEN  quit to the title",
                        "B TWICE ELSEWHERE    back to the disks",
                        "TITLE: A = 1 PLAYER, X = 2 PLAYERS", "",
                        "Mouse: where it points from the middle = the",
                        "direction; left fire, right thrust. Right Shift",
                        "pauses (Space resumes, Q quits)."};
  wip("hardcore", "HARDCORE", "HARDCORE.PRG", "HARDCORE  -  YAK, 1992 (UNFINISHED)",
      "Never released: Yak's unfinished blaster.", {0.30f, 0.06f, 0.06f, 1}, {0.85f, 0.85f, 0.85f, 1});
  // VBL $168F2 calls the hook at $338BA: $104EA title, $145D0 level intro, $10C82 play, $11242 paused.
  k.back().game.play_at = 0x338BA, k.back().game.play_value = 0x10C82;
  k.back().game.mode = input::Mode::kHardcore;
  k.back().game.docs_program = "DOC100E.PRG", k.back().game.docs_keys = "b";  // B = Yak's notes for testers
  k.back().game.help = {"HARDCORE  -  YAK, 1992 (UNFINISHED)", "",
                        "A 5-level evaluation demo, never released.",
                        "Your four guns ride the edges of the field.",
                        "The guns fire by themselves; the puck",
                        "ricochets and kills too.", "",
                        "LEFT STICK           move the guns",
                        "RIGHT STICK          put them at that spot",
                        "A OR RT              side lasers",
                        "Y                    puck intelligence on / off",
                        "BACK                 pause",
                        "B TWICE              back to the disks", "",
                        "Mouse: move, left button lasers; P pause, O resume,",
                        "S strobes (off at the start), N next level."};
  return k;
}

bool IsImage(const fs::path& p) {
  std::string ext = Upper(p.extension().string());
  return ext == ".MSA" || ext == ".ST" || ext == ".STX";
}

// The files under a root, a few folders deep (disk images and everything else, for folder games).
void Walk(const fs::path& root, std::vector<fs::path>* out) {
  std::error_code ec;
  if (!fs::is_directory(root, ec)) return;
  std::vector<fs::path> here;
  for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
       it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (ec) break;
    if (it.depth() > 3) {
      it.disable_recursion_pending();
      continue;
    }
    if (it->is_regular_file(ec)) here.push_back(it->path());
  }
  std::sort(here.begin(), here.end());
  out->insert(out->end(), here.begin(), here.end());
}

// A TOS program: the header's magic word.
bool IsProgram(const disk::Bytes& b) { return b.size() >= 28 && b[0] == 0x60 && b[1] == 0x1A; }

// A colour for a user disk from its name (the same each time).
SDL_FColor Hue(const std::string& s, float sat, float val) {
  uint32_t h = 2166136261u;
  for (char c : s) h = (h ^ uint8_t(c)) * 16777619u;
  float hue = float(h % 360) / 60, f = hue - float(int(hue));
  float p = val * (1 - sat), q = val * (1 - sat * f), t = val * (1 - sat * (1 - f));
  switch (int(hue)) {
    case 0: return {val, t, p, 1};
    case 1: return {q, val, p, 1};
    case 2: return {p, val, t, 1};
    case 3: return {p, q, val, 1};
    case 4: return {t, p, val, 1};
    default: return {val, p, q, 1};
  }
}

std::string SettingKey(const Game& g, const std::string& key) {
  std::string k = g.user ? "disk_" : "";
  for (char c : g.id) k += std::isalnum(uint8_t(c)) ? char(std::tolower(uint8_t(c))) : '_';
  return k + "_" + key;
}

const Game::Option* FindOption(const Game& g, const std::string& key) {
  for (const Game::Option& o : g.options)
    if (o.key == key) return &o;
  return nullptr;
}

}  // namespace

fs::path ExeDir() {
  const char* base = SDL_GetBasePath();
  return base ? fs::path(base) : fs::current_path();
}

std::vector<fs::path> SearchRoots() {
  std::vector<fs::path> roots = {ExeDir() / "game", ExeDir()};
  for (fs::path p = ExeDir(); !p.empty(); p = p.parent_path()) {
    roots.push_back(p / "Private");
    if (p == p.parent_path()) break;
  }
  return roots;
}

std::map<std::string, disk::Bytes> ReadSource(const fs::path& source, std::string* error) {
  std::map<std::string, disk::Bytes> files;
  std::error_code ec;
  if (fs::is_directory(source, ec)) {
    for (const auto& e : fs::directory_iterator(source, ec))
      if (e.is_regular_file()) files[Upper(e.path().filename().string())] = disk::ReadHostFile(e.path().string());
    return files;
  }
  std::string err;
  disk::Bytes image = disk::LoadImage(source.string(), &err);
  if (image.empty() && error) *error = err.empty() ? "can't read the disk image" : err;
  return disk::ReadFiles(image);
}

std::vector<Game> Discover(const std::string& disk) {
  std::vector<Known> known = KnownGames();
  std::vector<bool> found(known.size(), false);
  std::vector<Game> users;
  std::set<std::string> seen_images;  // name + size: the same image in two places counts once
  std::vector<fs::path> files;
  if (!disk.empty()) files.push_back(disk);  // --disk: first
  for (const fs::path& root : SearchRoots()) Walk(root, &files);

  for (const fs::path& f : files) {
    std::error_code ec;
    std::string upper = Upper(f.filename().string());
    // A folder game: its identifying file.
    for (size_t i = 0; i < known.size(); ++i)
      if (!found[i] && known[i].folder && upper == known[i].identify)
        found[i] = true, known[i].game.source = f.parent_path();
    if (!IsImage(f)) continue;
    std::string key = upper + ":" + std::to_string(fs::file_size(f, ec));
    if (!seen_images.insert(key).second) continue;
    std::map<std::string, disk::Bytes> on_disk = ReadSource(f, nullptr);
    bool any_known = false;
    for (size_t i = 0; i < known.size(); ++i) {
      if (known[i].folder || !on_disk.count(known[i].identify)) continue;
      any_known = true;
      if (!found[i]) found[i] = true, known[i].game.source = f;
    }
    if (any_known) continue;
    // Someone else's disk: offer the programs on it.
    Game u;
    u.user = true;
    u.source = f;
    u.id = f.stem().string();
    u.name = Upper(u.id);
    Game::Option programs{"program", "PROGRAM", {}};
    for (const auto& [name, body] : on_disk)
      if (IsProgram(body)) programs.values.push_back(name);
    if (programs.values.empty()) continue;  // a data disk
    u.options.push_back(std::move(programs));
    u.body = Hue(u.id, 0.55f, 0.45f), u.band = Hue(u.id + "*", 0.6f, 0.95f);
    fs::path png = f;
    png.replace_extension(".png");
    if (fs::exists(png, ec)) u.label_picture = png;
    u.help = {u.name, "", f.filename().string(), ""};
    u.help.insert(u.help.end(), std::begin(kGenericHelp), std::end(kGenericHelp));
    users.push_back(std::move(u));
  }

  std::vector<Game> out;
  std::set<std::string> ids;  // a game found in two versions: the first listed (the better one) wins
  for (size_t i = 0; i < known.size(); ++i) {
    bool use = found[i] && ids.insert(known[i].game.id).second;
    LOG("%s (%s): %s%s", known[i].game.name.c_str(), known[i].game.program.c_str(),
        found[i] ? known[i].game.source.string().c_str() : "not found", found[i] && !use ? " (not used)" : "");
    if (use) out.push_back(std::move(known[i].game));
  }
  for (Game& u : users) {
    LOG("another disk: %s (%zu programs)", u.source.string().c_str(), u.options[0].values.size());
    out.push_back(std::move(u));
  }
  return out;
}

int OptionValue(const Game& g, const std::string& key) {
  const Game::Option* o = FindOption(g, key);
  if (!o || o->values.empty()) return 0;
  return std::clamp(int(settings::Get(SettingKey(g, key), 0)), 0, int(o->values.size()) - 1);
}

void SetOption(const Game& g, const std::string& key, int value) {
  settings::Set(SettingKey(g, key), value);
  settings::Save();
}

std::string ConsoleAnswers(const Game& g) {
  if (g.id == "defender2") {
    int inv = OptionValue(g, "invincible"), friendly = OptionValue(g, "friendly");
    if (!inv && !friendly) return "\r";
    return std::string("t") + (inv ? "y" : "n") + (friendly ? "y" : "n");
  }
  return {};
}

std::string ProgramToRun(const Game& g) {
  const Game::Option* o = FindOption(g, "program");
  return g.user && o ? o->values[size_t(OptionValue(g, "program"))] : g.program;
}

fs::path SaveDir(const Game& g, const fs::path& userdata) {
  if (g.shared_saves) return userdata;
  if (g.user) return userdata / "disks" / g.id;
  return userdata / g.id;
}

}  // namespace games
