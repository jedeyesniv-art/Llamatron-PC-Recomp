// Llamatron (Atari ST, Jeff Minter / Llamasoft 1991) - native PC port.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "cpu.h"
#include "crash.h"
#include "disk.h"
#include "game.h"
#include "games.h"
#include "gridrunner.h"
#include "pad.h"
#include "stgames.h"
#include "tripatron.h"
#include "music.h"
#include "settings.h"
#include "input.h"
#include "crt.h"
#include "launcher.h"
#include "log.h"
#include "machine.h"

namespace fs = std::filesystem;

namespace {

struct Options {
  std::string disk;        // --disk=<.msa/.st>
  std::string log;         // --log=<file>
  std::string userdata;    // --userdata=<dir>: saves, settings and log there (tests: keep the player's own)
  bool trace_tos = false;  // --trace-tos
  bool fullscreen = true;  // --window / --fullscreen
  bool match_refresh = true;  // --refresh=desktop: keep the desktop's refresh rate in fullscreen
  game::Options game;       // --flashes: keep the original screen flashes
  bool entry_lane = false;  // --entry-lane: Gridrunner shows the edges of enemies in the entry lane (original)
  int aim = -1;             // --aim=8|analog for this run (else the saved setting, RB at the title)
  int cpu_speed = 8;        // --cpu=N: CPU speed multiple during play (1 = the ST's 8 MHz)
  int cpu_all = 0;          // --cpu-all=N: this CPU speed all the time (tests: is a game timed by its VBL?)
  int cpu_all_from = 0;     // --cpu-all-from=F: ... from frame F only
  int watch_step = 10;      // --watch-step=N: log the --watch words every N frames
  // --aspect=square (320x200 with square pixels: 16:10) or pal (as on a UK TV: an ST pixel is 8 MHz wide on a
  // line where square pixels are 14.75 MHz, and two TV lines tall -> pixel aspect 0.92, picture ~1.48:1)
  bool pal_aspect = true;
  std::string program = "TRON1MEG.PRG";  // --program=<file on the disk>: GRIDRUNR.PRG = the bonus Gridrunner
  bool program_given = false;  // --program: straight into it, no launcher
  // The launcher (tests): --lpress=N:<pad button> at launcher frame N, --lshot=N:<file.bmp>, --lframes=N (quit)
  struct LaunchPress {
    int frame;
    std::string what;
  };
  std::vector<LaunchPress> lpresses;
  std::map<int, std::string> lshots;
  int lframes = 0;
  bool headless = false;   // --headless: no window or audio (tests)
  int frames = 0;          // --frames=N: quit after N frames
  std::string wav;         // --wav=<file>: also write the audio to a WAV file (tests)
  std::string music;       // --music=<file>: Trip-A-Tron hears this file instead of the PC (tests)
  int trip_music = -1;     // --trip-music=0|1|2: Trip-A-Tron's music mode for this run
  std::map<int, std::string> shots;  // --shot=N:<file.bmp>
  std::map<int, std::string> rshots;  // --rshot=N:<file.bmp>: the screen as shown (CRT look included)
  // --press=N:<what>[:frames]: from frame N hold a key (SDL scancode name), "j0"/"j1" + fire|up|down|left|right,
  // or "pad" + an SDL gamepad button name (pady, padback...: button events only)
  struct Press {
    int frame, length;
    std::string what;
  };
  std::vector<Press> presses;
  // --poke=N:<hex text offset>:<hex word>: write a word into the game's memory at frame N (tests)
  struct Poke {
    int frame;
    uint32_t offset;
    uint16_t value;
  };
  std::vector<Poke> pokes;
  std::vector<uint32_t> watches;
  // --profile=N:M:<file>: count instructions per 16-byte block of the game's text, split by interrupt mask
  // (main loop = mask < 4, VBL/MFP handlers >= 4), over frames N..M; written to <file>.
  int profile_from = -1, profile_to = -1;
  int palette_from = -1, palette_to = -1;  // --palette-log=N:M: log the colour registers every frame (tests)
  int dump_frame = -1;     // --dump=N:<file>: write the 1 MB of RAM at frame N (tests, analysis)
  std::string dump_file;
  std::string profile_file;  // --watch=<hex text offset>[,...]: log these words every 10 frames (tests)
};

bool Arg(const std::string& a, const char* name, std::string* value) {
  std::string prefix = std::string("--") + name + "=";
  if (a.rfind(prefix, 0) != 0) return false;
  *value = a.substr(prefix.size());
  return true;
}

Options ParseArgs(int argc, char** argv) {
  Options o;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i], v;
    if (Arg(a, "disk", &v)) o.disk = v;
    else if (Arg(a, "log", &v)) o.log = v;
    else if (Arg(a, "userdata", &v)) o.userdata = v;
    else if (Arg(a, "frames", &v)) o.frames = std::atoi(v.c_str());
    else if (Arg(a, "wav", &v)) o.wav = v;
    else if (Arg(a, "shot", &v)) o.shots[std::atoi(v.c_str())] = v.substr(v.find(':') + 1);
    else if (Arg(a, "rshot", &v)) o.rshots[std::atoi(v.c_str())] = v.substr(v.find(':') + 1);
    else if (Arg(a, "press", &v)) {
      size_t c1 = v.find(':'), c2 = v.find(':', c1 + 1);
      o.presses.push_back({std::atoi(v.c_str()), c2 == std::string::npos ? 3 : std::atoi(v.c_str() + c2 + 1),
                           v.substr(c1 + 1, c2 == std::string::npos ? std::string::npos : c2 - c1 - 1)});
    }
    else if (Arg(a, "poke", &v)) {
      size_t c1 = v.find(':'), c2 = v.find(':', c1 + 1);
      o.pokes.push_back({std::atoi(v.c_str()), uint32_t(std::strtoul(v.c_str() + c1 + 1, nullptr, 16)),
                         uint16_t(std::strtoul(v.c_str() + c2 + 1, nullptr, 16))});
    }
    else if (Arg(a, "cpu-all-from", &v)) o.cpu_all_from = std::atoi(v.c_str());
    else if (Arg(a, "watch-step", &v)) o.watch_step = std::max(1, std::atoi(v.c_str()));
    else if (Arg(a, "cpu-all", &v)) o.cpu_all = std::atoi(v.c_str());
    else if (Arg(a, "palette-log", &v)) {
      o.palette_from = std::atoi(v.c_str());
      o.palette_to = std::atoi(v.c_str() + v.find(':') + 1);
    }
    else if (Arg(a, "watch", &v)) {
      for (size_t p = 0; p < v.size(); p = v.find(',', p) == std::string::npos ? v.size() : v.find(',', p) + 1)
        o.watches.push_back(uint32_t(std::strtoul(v.c_str() + p, nullptr, 16)));
    }
    else if (Arg(a, "dump", &v)) {
      o.dump_frame = std::atoi(v.c_str());
      o.dump_file = v.substr(v.find(':') + 1);
    }
    else if (Arg(a, "profile", &v)) {
      size_t c1 = v.find(':'), c2 = v.find(':', c1 + 1);
      o.profile_from = std::atoi(v.c_str()), o.profile_to = std::atoi(v.c_str() + c1 + 1);
      o.profile_file = v.substr(c2 + 1);
    }
    else if (a == "--trace-tos") o.trace_tos = true;
    else if (a == "--fullscreen") o.fullscreen = true;
    else if (a == "--window") o.fullscreen = false;
    else if (Arg(a, "lpress", &v)) o.lpresses.push_back({std::atoi(v.c_str()), v.substr(v.find(':') + 1)});
    else if (Arg(a, "lshot", &v)) o.lshots[std::atoi(v.c_str())] = v.substr(v.find(':') + 1);
    else if (Arg(a, "lframes", &v)) o.lframes = std::atoi(v.c_str());
    else if (Arg(a, "music", &v)) o.music = v;
    else if (Arg(a, "trip-music", &v)) o.trip_music = std::atoi(v.c_str());
    else if (Arg(a, "program", &v)) {
      o.program_given = true;
      o.program = v;
      std::transform(o.program.begin(), o.program.end(), o.program.begin(), ::toupper);
    }
    else if (Arg(a, "aspect", &v)) o.pal_aspect = v != "square";
    else if (a == "--flashes") o.game.flashes = true;
    else if (a == "--entry-lane") o.entry_lane = true;
    else if (a == "--aim=8") o.aim = 0;
    else if (a == "--aim=analog") o.aim = 1;
    else if (Arg(a, "cpu", &v)) o.cpu_speed = std::max(1, std::atoi(v.c_str()));
    else if (Arg(a, "refresh", &v)) o.match_refresh = v != "desktop";
    else if (a == "--headless") o.headless = true;
    else LOG("unknown option %s", a.c_str());
  }
  return o;
}

fs::path ExeDir() { return games::ExeDir(); }

void SaveRenderShot(SDL_Renderer* r, const std::string& path) {
  if (SDL_Surface* s = SDL_RenderReadPixels(r, nullptr)) {
    if (SDL_SaveBMP(s, path.c_str())) LOG("screenshot %s", path.c_str());
    SDL_DestroySurface(s);
  }
}

void SaveShot(const st::Machine& m, const std::string& path) {
  SDL_Surface* s = SDL_CreateSurfaceFrom(st::kScreenW, st::kScreenH, SDL_PIXELFORMAT_ARGB8888,
                                         const_cast<uint32_t*>(m.Frame()), st::kScreenW * 4);
  if (s && SDL_SaveBMP(s, path.c_str())) LOG("screenshot %s", path.c_str());
  SDL_DestroySurface(s);
}

void SaveWav(const std::string& path, const std::vector<float>& samples, int rate) {
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return;
  auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
  auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
  uint32_t bytes = uint32_t(samples.size() * 2);
  std::fwrite("RIFF", 1, 4, f), u32(36 + bytes), std::fwrite("WAVEfmt ", 1, 8, f);
  u32(16), u16(1), u16(1), u32(uint32_t(rate)), u32(uint32_t(rate * 2)), u16(2), u16(16);
  std::fwrite("data", 1, 4, f), u32(bytes);
  for (float x : samples) u16(uint16_t(int16_t(std::clamp(x, -1.0f, 1.0f) * 32767)));
  std::fclose(f);
  LOG("wrote %s (%zu samples)", path.c_str(), samples.size());
}

void LogDisplayModes() {
  SDL_DisplayID display = SDL_GetPrimaryDisplay();
  if (const SDL_DisplayMode* d = SDL_GetDesktopDisplayMode(display))
    LOG("display: desktop %dx%d @ %.3f Hz", d->w, d->h, d->refresh_rate);
  int n = 0;
  const SDL_DisplayMode* desk = SDL_GetDesktopDisplayMode(display);
  if (SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(display, &n); modes && desk) {
    std::string rates;  // the refresh rates at the desktop resolution
    for (int i = 0; i < n; ++i)
      if (modes[i]->w == desk->w && modes[i]->h == desk->h)
        rates += std::to_string(modes[i]->refresh_rate).substr(0, 6) + " ";
    LOG("display refresh rates at %dx%d: %s", desk->w, desk->h, rates.c_str());
    SDL_free(modes);
  }
}

// Fullscreen in a mode whose refresh rate is a whole multiple of the game's (100 Hz, 50 Hz...) at the desktop
// resolution: then every game frame is shown the same number of times - smooth motion at the original speed.
void ChooseFullscreenMode(SDL_Window* window, double game_hz) {
  SDL_DisplayID display = SDL_GetDisplayForWindow(window);
  const SDL_DisplayMode* desk = SDL_GetDesktopDisplayMode(display);
  if (!desk) return;
  int n = 0;
  SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(display, &n);
  const SDL_DisplayMode* best = nullptr;
  for (int i = 0; i < n; ++i) {
    const SDL_DisplayMode* m = modes[i];
    if (m->w != desk->w || m->h != desk->h || m->refresh_rate > desk->refresh_rate + 0.5f) continue;
    int k = int(std::lround(m->refresh_rate / game_hz));
    if (k < 1 || std::fabs(m->refresh_rate / k - game_hz) > game_hz * 0.005) continue;
    if (!best || m->refresh_rate > best->refresh_rate) best = m;  // the fastest such mode (least latency)
  }
  if (best) {
    SDL_SetWindowFullscreenMode(window, best);
    LOG("fullscreen mode %dx%d @ %.3f Hz (%d refreshes per game frame)", best->w, best->h, best->refresh_rate,
        int(std::lround(best->refresh_rate / game_hz)));
  } else {
    LOG("no display mode at a multiple of %.2f Hz: fullscreen at the desktop rate", game_hz);
  }
  SDL_free(modes);
}

float DisplayRefresh(SDL_Window* window) {
  const SDL_DisplayMode* m = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window));
  return m ? m->refresh_rate : 0.0f;
}

// --profile: instruction counts per 16-byte block of the text, [0] = main loop, [1] = interrupts.
std::map<uint32_t, std::array<uint64_t, 2>> g_profile_counts;
uint32_t g_profile_base = 0;
void ProfileHook(uint32_t pc, int ipl) {
  uint32_t off = (pc & 0xFFFFFF) - g_profile_base;
  if (off < 0x60000) ++g_profile_counts[off >> 4][ipl >= 4];
}

void WriteProfile(const std::string& path) {
  FILE* f = std::fopen(path.c_str(), "w");
  if (!f) return;
  for (const auto& [block, n] : g_profile_counts)
    std::fprintf(f, "%06X %llu %llu\n", block << 4, (unsigned long long)n[0], (unsigned long long)n[1]);
  std::fclose(f);
  LOG("profile written to %s", path.c_str());
}

uint8_t PressBits(const std::string& what, int* port) {
  *port = what.size() > 1 && what[1] == '0' ? 0 : 1;
  std::string b = what.substr(2);
  return b == "fire" ? st::kJoyFire : b == "up" ? st::kJoyUp : b == "down" ? st::kJoyDown
         : b == "left" ? st::kJoyLeft : b == "right" ? st::kJoyRight : 0;
}

// What the launcher and the games share: the options, the disk, the window, the audio.
struct Context {
  Options* opt = nullptr;
  fs::path save_dir;
  std::string disk_path;
  std::map<std::string, disk::Bytes> disk_files;  // as on the disk
  SDL_Window* window = nullptr;
  SDL_Renderer* renderer = nullptr;
  SDL_Texture* texture = nullptr;
  SDL_AudioStream* audio = nullptr;
  int view_h = st::kScreenH;
  std::vector<float> wav;
  std::vector<games::Game> games;  // what the launcher offers
  const games::Game* game = nullptr;  // the launcher's game being run (null: --program of an unknown one)
  std::string type_ahead;  // keys for the program's first console prompts (a crack's trainer questions)
};

constexpr int kRate = 48000;

// The disk's files with the saved ones (userdata\: high scores the games wrote) over them.
std::map<std::string, disk::Bytes> FilesWithSaves(const Context& c) {
  std::map<std::string, disk::Bytes> files = c.disk_files;
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(c.save_dir, ec)) {
    std::string name = e.path().filename().string();
    std::transform(name.begin(), name.end(), name.begin(), ::toupper);
    std::string ext = name.size() > 4 ? name.substr(name.size() - 4) : "";
    if (ext == ".LOG" || ext == ".TXT") continue;  // our log and settings, not game files
    disk::Bytes d;
    if (FILE* f = std::fopen(e.path().string().c_str(), "rb")) {
      d.resize(size_t(fs::file_size(e.path(), ec)));
      d.resize(std::fread(d.data(), 1, d.size(), f));
      std::fclose(f);
      files[name] = std::move(d);
      LOG("using saved %s", name.c_str());
    }
  }
  return files;
}

enum class GameEnd { kQuitApp, kToLauncher, kError };

// The faster CPU's rule for a game from the registry: its play state in RAM.
bool PlayRule(const st::Machine& m, const games::Game& g) {
  const uint8_t* p = m.Ram() + g.play_at;
  uint32_t v = g.play_word ? uint32_t(p[0] << 8 | p[1]) : uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
  if (g.play_vbl && uint32_t(m.Ram()[0x70] << 24 | m.Ram()[0x71] << 16 | m.Ram()[0x72] << 8 | m.Ram()[0x73]) != g.play_vbl)
    return false;
  return v == g.play_value || (g.play_value2 && v == g.play_value2) || (g.play_value3 && v == g.play_value3);
}

// Runs one game (a program on the disk) until the player leaves it.
GameEnd RunGame(Context& c, const std::string& program_name, bool direct, input::Mode mode) {
  Options& opt = *c.opt;
  std::string error;
  disk::Program program;
  const bool llamatron = mode == input::Mode::kLlamatron;
  const bool gridrunner = mode == input::Mode::kGridrunner;
  const bool tripatron = mode == input::Mode::kTripATron;
  std::map<std::string, disk::Bytes> files = FilesWithSaves(c);  // saved files too (an installer's output)
  auto it = files.find(program_name);
  bool loaded = it != files.end() && (llamatron ? disk::UnpackLlamatron(it->second, &program, &error)
                                                      : disk::LoadPrg(it->second, &program, &error));
  if (!loaded) {
    if (error.empty()) error = program_name + " not found on the disk";
    std::string msg = c.disk_path + ":\n" + error;
    LOG("%s", msg.c_str());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Llamatron", msg.c_str(), c.window);
    return GameEnd::kError;
  }
  LOG("starting %s: %zu bytes, %zu relocations", program_name.c_str(), program.text.size(), program.relocs.size());

  auto machine_owner = std::make_unique<st::Machine>();
  st::Machine& machine = *machine_owner;
  machine.trace_tos = opt.trace_tos;
  if (!machine.Load(program, std::move(files), c.save_dir.string(), &error)) {
    LOG("load failed: %s", error.c_str());
    return GameEnd::kError;
  }
  game::Reset();
  input::Reset();
  grid::Reset();
  game::g_llamatron = llamatron;
  if (llamatron) game::ApplyPatches(machine, opt.game);
  input::SetMode(mode);
  trip::Reset();
  pad::Reset();
  if (mode == input::Mode::kRmc) stgames::Start(stgames::Game::kRmc);
  if (mode == input::Mode::kAndes) stgames::Start(stgames::Game::kAndes);
  if (mode == input::Mode::kDefender2) stgames::Start(stgames::Game::kDefender2);
  if (!opt.music.empty()) music::StartTestFile(opt.music);
  if (opt.trip_music >= 0) trip::SetMusicMode(opt.trip_music);
  grid::SetHideEntryLane(!opt.entry_lane);
  machine.SetSampleRate(kRate);
  machine.TypeAhead(c.type_ahead);
  LOG("analog aim %s, cpu speed during play x%d", game::AnalogAim() ? "on" : "off", opt.cpu_speed);

  SDL_Window* window = c.window;
  SDL_Renderer* renderer = c.renderer;
  SDL_Texture* texture = c.texture;
  SDL_AudioStream* audio = c.audio;
  const int view_h = c.view_h;
  if (renderer) SDL_SetRenderLogicalPresentation(renderer, st::kScreenW, view_h, SDL_LOGICAL_PRESENTATION_LETTERBOX);
  if (audio) SDL_ClearAudioStream(audio);

  GameEnd end = GameEnd::kQuitApp;
  uint64_t start = SDL_GetTicksNS();
  double frames_due = 0;
  int frame = 0, vsyncs = 0, lock_k = -1;
  // Timing stats, logged every 10 s: game frames and presents per second, longest gap between presents.
  uint64_t stats_start = SDL_GetTicksNS(), last_present = 0, worst_gap = 0;
  int stats_frames = 0, stats_presents = 0;
  // Screen swaps (frames the game finished drawing) per second of ST time, counting only seconds spent playing.
  // Every 10 s: average and the worst second.
  int swaps_second = 0, swaps_total = 0, swaps_worst = 1 << 30, swaps_seconds = 0;
  int vbls_second = 0, vbls_missed = 0;  // VBL interrupts the program didn't take (while playing)
  bool playing_second = true;
  bool running = true, at_title = false;
  uint64_t last_mouse_ms = 0;  // the game doesn't use the mouse: its cursor is hidden
  float mouse_x = 0, mouse_y = 0;  // ST mouse movement not sent yet (other programs)
  if (window && !llamatron) SDL_SetWindowRelativeMouseMode(window, true);
  while (running) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_EVENT_QUIT) running = false, end = GameEnd::kQuitApp;
      if (!llamatron && e.type == SDL_EVENT_MOUSE_MOTION) {  // other programs (Gridrunner): the ST mouse
        mouse_x += e.motion.xrel * 0.25f, mouse_y += e.motion.yrel * 0.25f;
        continue;
      }
      if (e.type == SDL_EVENT_MOUSE_MOTION) {  // the cursor shows while the mouse moves (windowed), then hides
        last_mouse_ms = SDL_GetTicks();
        if (!(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN)) SDL_ShowCursor();
      }
      if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat &&
          (e.key.scancode == SDL_SCANCODE_F11 ||
           (e.key.scancode == SDL_SCANCODE_RETURN && (e.key.mod & SDL_KMOD_ALT)))) {
        SDL_SetWindowFullscreen(window, !(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN));
        continue;
      }
      if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && e.key.scancode == SDL_SCANCODE_F9) {
        crt::Toggle();
        continue;
      }
      input::HandleEvent(e, &machine);
    }
    if (input::QuitRequested()) running = false, end = direct ? GameEnd::kQuitApp : GameEnd::kToLauncher;
    if (frame % 50 == 0 && settings::Reload()) {  // edited while running
      grid::LoadSettings();
      trip::LoadSettings();
      crt::LoadSettings();
      if (opt.aim < 0) game::SetAnalogAim(settings::Get("analog_aim", 0) != 0);
    }
    if (window && SDL_CursorVisible() &&
        ((SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) || SDL_GetTicks() - last_mouse_ms > 2000))
      SDL_HideCursor();

    // Pacing. If the display's refresh is a whole multiple k of the game's frame rate, one game frame every k
    // vsyncs (presents block on vsync): even motion. Otherwise game frames are due by the wall clock (at most a
    // few at once after a stall), shown on the next refresh.
    int run = 1;
    if (!opt.headless) {
      double hz = machine.FrameHz(), refresh = DisplayRefresh(window);
      int k = refresh > 0 ? int(std::lround(refresh / hz)) : 0;
      if (k < 1 || std::fabs(refresh / k - hz) > hz * 0.005) k = 0;
      double t = double(SDL_GetTicksNS() - start) / 1e9;
      if (k != lock_k) {
        LOG("pacing: %s (display %.3f Hz, game %.3f Hz)", k ? "locked to vsync" : "by the clock", refresh, hz);
        lock_k = k;
        vsyncs = 0;
        frames_due = t * hz;
      }
      if (k) {
        run = vsyncs++ % k == 0 ? 1 : 0;
      } else {
        if (t * hz - frames_due > 6) frames_due = t * hz;  // resync after a stall
        run = std::max(0, std::min(int(t * hz - frames_due), 3));
        // Present only new frames, and wait for the next one in between: on a variable refresh display
        // (G-Sync / FreeSync) the screen then refreshes at the game's 50 Hz - smooth. On a fixed 60 Hz one each
        // frame waits for the next refresh as before.
        if (run == 0) {
          double wait = (frames_due + 1 - t * hz) / hz;
          if (wait > 0) SDL_DelayPrecise(uint64_t(std::min(wait, 0.02) * 1e9));
          continue;
        }
      }
    }
    for (int i = 0; i < run && running; ++i) {
      frames_due += 1;
      input::Update(&machine);
      if (!llamatron) {
        SDL_MouseButtonFlags mb = opt.headless ? 0 : SDL_GetMouseState(nullptr, nullptr);
        uint8_t buttons = uint8_t((mb & SDL_BUTTON_LMASK ? 2 : 0) | (mb & SDL_BUTTON_RMASK ? 1 : 0));
        // Scripted mouse and pad (tests): mleft, mright, mx<dx>, my<dy> (per frame); pad: gl<degrees>@<deflection>
        // (left stick), gry<value> (right stick y), glt, grt (triggers), gx, gy, glb, grb (buttons).
        grid::TestPad tp;
        bool test_pad = false;
        for (const auto& p : opt.presses) {
          if (frame < p.frame || frame >= p.frame + p.length) continue;
          if (p.what.rfind("gl", 0) == 0 && p.what.size() > 2 && p.what[2] != 't') {
            double deg = std::atof(p.what.c_str() + 2), mag = std::atof(p.what.c_str() + p.what.find('@') + 1);
            tp.lx = float(std::cos(deg * 3.14159265 / 180) * mag), tp.ly = float(std::sin(deg * 3.14159265 / 180) * mag);
            test_pad = true;
          }
          if (p.what.rfind("gry", 0) == 0) tp.ry = float(std::atof(p.what.c_str() + 3)), test_pad = true;
          if (p.what.rfind("gr", 0) == 0 && p.what.find('@') != std::string::npos) {  // gr<degrees>@<deflection>
            double deg = std::atof(p.what.c_str() + 2), mag = std::atof(p.what.c_str() + p.what.find('@') + 1);
            tp.rx = float(std::cos(deg * 3.14159265 / 180) * mag), tp.ry = float(std::sin(deg * 3.14159265 / 180) * mag);
            test_pad = true;
          }
          if (p.what == "ga") tp.buttons |= 1u << SDL_GAMEPAD_BUTTON_SOUTH, test_pad = true;
          if (p.what == "gb") tp.buttons |= 1u << SDL_GAMEPAD_BUTTON_EAST, test_pad = true;
          if (p.what == "gstart") tp.buttons |= 1u << SDL_GAMEPAD_BUTTON_START, test_pad = true;
          if (p.what == "gback") tp.buttons |= 1u << SDL_GAMEPAD_BUTTON_BACK, test_pad = true;
          if (p.what == "gup") tp.buttons |= 1u << SDL_GAMEPAD_BUTTON_DPAD_UP, test_pad = true;
          if (p.what == "gdown") tp.buttons |= 1u << SDL_GAMEPAD_BUTTON_DPAD_DOWN, test_pad = true;
          if (p.what == "gleft") tp.buttons |= 1u << SDL_GAMEPAD_BUTTON_DPAD_LEFT, test_pad = true;
          if (p.what == "gright") tp.buttons |= 1u << SDL_GAMEPAD_BUTTON_DPAD_RIGHT, test_pad = true;
          if (p.what == "glt") tp.lt = 1, test_pad = true;
          if (p.what == "grt") tp.rt = 1, test_pad = true;
          if (p.what == "gx") tp.buttons |= 1u << SDL_GAMEPAD_BUTTON_WEST, test_pad = true;
          if (p.what == "gy") tp.buttons |= 1u << SDL_GAMEPAD_BUTTON_NORTH, test_pad = true;
          if (p.what == "glb") tp.buttons |= 1u << SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, test_pad = true;
          if (p.what == "grb") tp.buttons |= 1u << SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, test_pad = true;
          if (p.what == "mleft") buttons |= 2;
          if (p.what == "mright") buttons |= 1;
          if (p.what.rfind("mx", 0) == 0) mouse_x += float(std::atof(p.what.c_str() + 2));
          if (p.what.rfind("my", 0) == 0) mouse_y += float(std::atof(p.what.c_str() + 2));
        }
        if (test_pad) grid::SetTestPad(tp), pad::SetTest({true, tp.lx, tp.ly, tp.rx, tp.ry, tp.lt, tp.rt, tp.buttons});
        pad::Poll(input::FirstPad());
        int dx = int(mouse_x), dy = int(mouse_y);
        mouse_x -= float(dx), mouse_y -= float(dy);
        if (gridrunner) {
          grid::Update(&machine, input::FirstPad(), dx, dy, buttons);
        } else if (tripatron) {
          if (test_pad) trip::SetTestPad({tp.lx, tp.ly, tp.rx, tp.ry, tp.lt, tp.rt, tp.buttons});
          trip::Update(&machine, input::FirstPad(), dx, dy, buttons);
        } else if (mode == input::Mode::kRmc || mode == input::Mode::kAndes || mode == input::Mode::kDefender2) {
          stgames::Update(&machine, dx, dy, buttons);
          machine.SetCalmBackground(stgames::Calm(machine));
        } else {
          input::UpdateGeneric(&machine, dx, dy, buttons);
        }
      }
      for (const auto& p : opt.presses) {  // scripted input (tests)
        if (frame < p.frame || frame >= p.frame + p.length) continue;
        if (p.what.rfind("aim", 0) == 0) {  // aim<degrees>: the aiming stick at an angle (0 = right, 90 = down)
          double rad = std::atof(p.what.c_str() + 3) * 3.14159265358979 / 180;
          float x = float(std::cos(rad)), y = float(std::sin(rad));
          game::SetAim(true, x, y);
          uint8_t bits = 0;
          if (x > 0.38f) bits |= st::kJoyRight;
          if (x < -0.38f) bits |= st::kJoyLeft;
          if (y > 0.38f) bits |= st::kJoyDown;
          if (y < -0.38f) bits |= st::kJoyUp;
          machine.SetJoystick(0, bits);
        } else if (p.what[0] == 'j') {
          int port;
          uint8_t bits = PressBits(p.what, &port);
          machine.SetJoystick(port, bits);
        } else if (p.what.rfind("pad", 0) == 0) {  // pad<button>: SDL gamepad button name (a, b, y, back...)
          if (frame != p.frame && frame != p.frame + p.length - 1) continue;
          SDL_Event be{};
          be.type = frame == p.frame ? SDL_EVENT_GAMEPAD_BUTTON_DOWN : SDL_EVENT_GAMEPAD_BUTTON_UP;
          be.gbutton.button = uint8_t(SDL_GetGamepadButtonFromString(p.what.c_str() + 3));
          input::HandleEvent(be, &machine);
        } else if (frame == p.frame || frame == p.frame + p.length - 1) {
          SDL_Scancode sc = SDL_GetScancodeFromName(p.what.c_str());
          SDL_Event ke{};
          ke.type = frame == p.frame ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
          ke.key.scancode = sc;
          input::HandleEvent(ke, &machine);
        }
      }
      for (const auto& p : opt.pokes) {
        if (p.frame != frame) continue;
        uint8_t* at = machine.Ram() + machine.TextBase() + p.offset;
        at[0] = uint8_t(p.value >> 8), at[1] = uint8_t(p.value);
      }
      if (frame == opt.dump_frame) {
        if (FILE* f = std::fopen(opt.dump_file.c_str(), "wb")) {
          std::fwrite(machine.Ram(), 1, st::kRamSize, f);
          std::fclose(f);
          LOG("RAM dumped to %s", opt.dump_file.c_str());
        }
      }
      if (frame == opt.profile_from) g_profile_base = machine.TextBase(), cpu::g_profile = ProfileHook;
      if (frame == opt.profile_to) cpu::g_profile = nullptr, WriteProfile(opt.profile_file);
      // The faster CPU only while playing: the drawing keeps up with the sprites (the game logic is in the 50 Hz
      // VBL either way); other screens are timed by CPU speed and keep the original.
      bool playing = llamatron    ? game::Playing(machine)
                     : gridrunner ? grid::GameState(machine) == grid::kPlay
                     : c.game && c.game->play_at ? PlayRule(machine, *c.game)
                                                 : false;
      const bool forced = opt.cpu_all && frame >= opt.cpu_all_from;
      if (forced) playing = true;
      machine.SetCpuSpeed(forced ? opt.cpu_all : playing ? opt.cpu_speed : 1);
      playing_second &= playing;
      if (stgames::HostPaused() && (mode == input::Mode::kDefender2)) {
        machine.Audio().clear();  // paused: the picture stays, no sound, no frames
      } else if (!machine.RunFrame()) {
        running = false, end = direct ? GameEnd::kQuitApp : GameEnd::kToLauncher;
      }
      ++frame;
      ++stats_frames;
      swaps_second += machine.TakeSwaps();
      vbls_second += machine.TakeVbls();
      if (frame >= opt.palette_from && frame <= opt.palette_to) {
        std::string line;
        for (int i = 0; i < 16; ++i) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), " %03X", machine.Palette()[i]);
          line += buf;
        }
        if (machine.CalmBackground()) {
          char buf[48];
          const float* c = machine.CalmColour();
          std::snprintf(buf, sizeof(buf), "  shown 0: %.2f %.2f %.2f", c[0], c[1], c[2]);
          line += buf;
        }
        LOG("frame %d palette:%s", frame, line.c_str());
      }
      if (!opt.watches.empty() && frame % opt.watch_step == 0) {
        std::string line;
        for (uint32_t w : opt.watches) {
          const uint8_t* at = machine.Ram() + machine.TextBase() + w;
          char buf[32];
          std::snprintf(buf, sizeof(buf), " %X=%d", w, int16_t(at[0] << 8 | at[1]));
          line += buf;
        }
        LOG("frame %d:%s", frame, line.c_str());
      }
      if (frame % 50 == 0) {
        if (playing_second) {
          vbls_missed += std::max(0, 50 - vbls_second);
          swaps_total += swaps_second;
          swaps_worst = std::min(swaps_worst, swaps_second);
          ++swaps_seconds;
        }
        swaps_second = vbls_second = 0;
        playing_second = true;
        if (frame % 500 == 0 && swaps_seconds) {
          LOG("frame %d: drawn %.1f frames/s while playing, worst second %d (%d s), VBLs missed %d", frame,
              double(swaps_total) / swaps_seconds, swaps_worst, swaps_seconds, vbls_missed);
          swaps_total = swaps_seconds = vbls_missed = 0, swaps_worst = 1 << 30;
        }
      }
      bool title = game::AtTitle(machine);
      if (title != at_title) {
        if (title) LOG("frame %d: title screen", frame);
        else LOG("frame %d: left the title (%s)", frame, game::TwinStick(machine) ? "twin-stick" : "one stick");
        at_title = title;
      }
      auto shot = opt.shots.find(frame);
      if (shot != opt.shots.end()) SaveShot(machine, shot->second);
      if (opt.frames && frame >= opt.frames) running = false, end = GameEnd::kQuitApp;
    }

    std::vector<float>& samples = machine.Audio();
    if (!opt.wav.empty()) c.wav.insert(c.wav.end(), samples.begin(), samples.end());
    if (audio && !samples.empty()) {
      // Keep about 50 ms queued: play up to 0.5% faster or slower (the vsync-locked game runs at the display's
      // rate, not exactly the ST's), and drop audio if we got far ahead (after a stall).
      int queued = SDL_GetAudioStreamQueued(audio) / int(sizeof(float));
      if (queued > kRate / 5) SDL_ClearAudioStream(audio), queued = 0;
      const double target = kRate * 0.05;
      SDL_SetAudioStreamFrequencyRatio(audio, float(std::clamp(1.0 + (queued - target) / target * 0.005, 0.995, 1.005)));
      SDL_PutAudioStreamData(audio, samples.data(), int(samples.size() * sizeof(float)));
    }
    samples.clear();

    if (renderer) {
      SDL_UpdateTexture(texture, nullptr, machine.Frame(), st::kScreenW * 4);
      SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
      SDL_RenderClear(renderer);
      SDL_FRect view{0, 0, float(st::kScreenW), float(view_h)};
      crt::DrawPicture(renderer, texture, view);
      if (const char* hint = input::Hint(&machine)) {  // 8x8 debug font in the logical space, lines at the bottom
        std::vector<std::string> lines;
        for (const char* p = hint; *p;) {
          const char* nl = std::strchr(p, '\n');
          lines.emplace_back(p, nl ? nl : p + std::strlen(p));
          p = nl ? nl + 1 : p + std::strlen(p);
        }
        float y = input::HintAtTop() ? 4.0f : float(view_h - 12 - 10 * int(lines.size()));
        for (const std::string& line : lines) {
          float w = float(line.size() * 8), x = (st::kScreenW - w) / 2;
          SDL_FRect box{x - 4, y - 1, w + 8, 10};
          SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
          SDL_RenderFillRect(renderer, &box);
          SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
          SDL_RenderDebugText(renderer, x, y, line.c_str());
          y += 10;
        }
      }
      auto rshot = opt.rshots.find(frame);
      if (rshot != opt.rshots.end()) SaveRenderShot(renderer, rshot->second);
      SDL_RenderPresent(renderer);
      uint64_t now = SDL_GetTicksNS();
      if (last_present) worst_gap = std::max(worst_gap, now - last_present);
      last_present = now;
      ++stats_presents;
      if (now - stats_start >= 10'000'000'000ull) {
        double secs = double(now - stats_start) / 1e9;
        LOG("timing: %.2f game frames/s, %.2f presents/s, longest present gap %.1f ms", stats_frames / secs,
            stats_presents / secs, double(worst_gap) / 1e6);
        stats_start = now, stats_frames = stats_presents = 0, worst_gap = 0;
      }
    }
  }
  if (window) SDL_SetWindowRelativeMouseMode(window, false);
  if (audio) SDL_ClearAudioStream(audio);
  LOG("%s ended after %d frames", program_name.c_str(), frame);
  return end;
}

// An installer (Trip-A-Tron's TRIP1.TOS) writes the program and its data files to a blank disk: here that's the
// game's save folder. Run once, unseen and fast, until the program is written (the last file; then it waits).
bool Install(Context& c, const games::Game& g, const std::map<std::string, disk::Bytes>& files) {
  disk::Program program;
  std::string error;
  auto it = files.find(g.install);
  if (it == files.end() || !disk::LoadPrg(it->second, &program, &error)) return false;
  LOG("installing %s into %s", g.name.c_str(), c.save_dir.string().c_str());
  auto machine = std::make_unique<st::Machine>();
  if (!machine->Load(program, files, c.save_dir.string(), &error)) return false;
  machine->SetCpuSpeed(8);
  machine->SetSampleRate(kRate);
  for (int frame = 0; frame < 30000; ++frame) {
    if (!machine->RunFrame()) break;
    machine->Audio().clear();
    if (frame % 25) continue;
    std::error_code ec;
    if (fs::exists(c.save_dir / g.program, ec)) {
      LOG("installed after %d frames", frame);
      return true;
    }
    SDL_Event e;
    while (SDL_PollEvent(&e))
      if (e.type == SDL_EVENT_QUIT) return false;
    if (c.renderer) {  // a word while it works
      SDL_SetRenderLogicalPresentation(c.renderer, 320, 200, SDL_LOGICAL_PRESENTATION_LETTERBOX);
      SDL_SetRenderDrawColor(c.renderer, 0, 0, 0, 255);
      SDL_RenderClear(c.renderer);
      SDL_SetRenderDrawColor(c.renderer, 200, 200, 210, 255);
      std::string msg = "INSTALLING " + g.name;
      SDL_RenderDebugText(c.renderer, 160 - 4 * float(msg.size()), 96,
                          (msg + std::string(size_t((frame / 25) % 4), '.')).c_str());
      SDL_RenderPresent(c.renderer);
    }
  }
  return false;
}

// A game from the launcher's list: its disk (or folder), its save folder, installed there first if it needs it.
GameEnd RunEntry(Context& c, const games::Game& g, bool direct) {
  auto fail = [&](const std::string& msg) {
    LOG("%s", msg.c_str());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, g.name.c_str(), msg.c_str(), c.window);
    return GameEnd::kError;
  };
  std::string error;
  std::map<std::string, disk::Bytes> files = games::ReadSource(g.source, &error);
  if (files.empty()) return fail(g.source.string() + ":\n" + (error.empty() ? "no files" : error));
  fs::path root_save = c.save_dir;
  c.save_dir = games::SaveDir(g, root_save);
  std::error_code ec;
  fs::create_directories(c.save_dir, ec);
  c.disk_files = files;
  c.disk_path = g.source.string();
  GameEnd end;
  if (!g.install.empty() && !fs::exists(c.save_dir / g.program, ec) && !Install(c, g, files)) {
    end = fail(g.name + " couldn't be installed from " + g.source.string());
  } else {
    c.type_ahead = games::ConsoleAnswers(g);
    c.game = &g;
    end = RunGame(c, games::ProgramToRun(g), direct, g.mode);
    c.type_ahead.clear();
    c.game = nullptr;
  }
  c.save_dir = root_save;
  return end;
}

// The start screen until the player picks a game (kPlay) or quits.
bool RunLauncher(Context& c, launcher::Launcher& l) {
  Options& opt = *c.opt;
  SDL_SetRenderLogicalPresentation(c.renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
  uint64_t last = SDL_GetTicksNS();
  for (int frame = 0;; ++frame) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_EVENT_QUIT) return false;
      if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat &&
          (e.key.scancode == SDL_SCANCODE_F11 ||
           (e.key.scancode == SDL_SCANCODE_RETURN && (e.key.mod & SDL_KMOD_ALT)))) {
        SDL_SetWindowFullscreen(c.window, !(SDL_GetWindowFlags(c.window) & SDL_WINDOW_FULLSCREEN));
        continue;
      }
      input::HandleEvent(e, nullptr);  // pads connecting
      if (l.HandleEvent(e) == launcher::Action::kQuit) return false;
    }
    for (const auto& p : opt.lpresses) {  // scripted (tests): pad buttons at launcher frames
      if (p.frame != frame) continue;
      SDL_Event be{};
      be.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
      be.gbutton.button = uint8_t(SDL_GetGamepadButtonFromString(p.what.c_str()));
      if (l.HandleEvent(be) == launcher::Action::kQuit) return false;
    }
    uint64_t now = SDL_GetTicksNS();
    double dt = std::min(0.1, double(now - last) / 1e9);
    last = now;
    if (l.Update(input::FirstPad(), dt) == launcher::Action::kPlay) return true;
    l.Render();
    auto shot = opt.lshots.find(frame);
    if (shot != opt.lshots.end()) SaveRenderShot(c.renderer, shot->second);
    SDL_RenderPresent(c.renderer);
    if (opt.lframes && frame >= opt.lframes) return false;
  }
}

// --program: a game we know runs as from the launcher (its controls and save folder); anything else from --disk
// (or the first disk found with it on) with generic controls, saving in userdata\.
void RunProgram(Context& c) {
  const Options& opt = *c.opt;
  for (const games::Game& g : c.games)
    if (!g.user && g.program == opt.program && (opt.disk.empty() || g.source == fs::path(opt.disk))) {
      RunEntry(c, g, true);
      return;
    }
  std::vector<fs::path> sources;
  if (!opt.disk.empty()) sources.push_back(opt.disk);
  for (const games::Game& g : c.games) sources.push_back(g.source);
  for (const fs::path& src : sources) {
    std::string error;
    std::map<std::string, disk::Bytes> files = games::ReadSource(src, &error);
    if (!files.count(opt.program) && src != sources.front()) continue;
    c.disk_files = std::move(files);
    c.disk_path = src.string();
    RunGame(c, opt.program, true, opt.program == "TRON1MEG.PRG"   ? input::Mode::kLlamatron
                                  : opt.program == "GRIDRUNR.PRG" ? input::Mode::kGridrunner
                                  : opt.program == "TRIP.PRG"     ? input::Mode::kTripATron
                                                                  : input::Mode::kGeneric);
    return;
  }
  LOG("%s: no disk found", opt.program.c_str());
}

}  // namespace

int main(int argc, char** argv) {
  Options opt = ParseArgs(argc, argv);
  Context c;
  c.opt = &opt;
  // The log: --log, else userdata\llamatron.log next to the exe.
  std::error_code ec;
  c.save_dir = opt.userdata.empty() ? ExeDir() / "userdata" : fs::path(opt.userdata);
  fs::create_directories(c.save_dir, ec);
  logging::Open(opt.log.empty() ? (c.save_dir / "llamatron.log").string().c_str() : opt.log.c_str());
  InstallCrashHandler();

  // Settings (userdata\settings.txt, key=value; reread while running when it changes).
  settings::Load((c.save_dir / "settings.txt").string());
  game::SetAnalogAim(settings::Get("analog_aim", 0) != 0);
  grid::LoadSettings();
  trip::LoadSettings();
  c.games = games::Discover(opt.disk);
  settings::Save();  // so every setting is in the file, with its default
  if (opt.aim >= 0) game::SetAnalogAim(opt.aim == 1);

  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
  SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON, "1");  // the window/taskbar icon: our resource 1
  SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON_SMALL, "1");
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
    LOG("SDL_Init: %s", SDL_GetError());
    return 1;
  }
  input::Init();
  LogDisplayModes();

  // Picture height in a 320-wide space: PAL pixel aspect (14.75 / 8) / 2 = 0.92 -> 200 / 0.92 = 217.
  c.view_h = opt.pal_aspect ? int(std::lround(st::kScreenH * 2.0 * 8.0 / 14.75)) : st::kScreenH;
  if (!opt.headless) {
    c.window = SDL_CreateWindow("Llamatron", st::kScreenW * 4, st::kScreenH * 4,
                                SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    c.renderer = crt::CreateRenderer(c.window);
    if (c.renderer) crt::Init(c.renderer);
    SDL_SetRenderVSync(c.renderer, 1);
    if (opt.match_refresh) ChooseFullscreenMode(c.window, 50.0);
    if (opt.fullscreen) SDL_SetWindowFullscreen(c.window, true);
    c.texture = SDL_CreateTexture(c.renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, st::kScreenW,
                                  st::kScreenH);
    SDL_SetTextureScaleMode(c.texture, SDL_SCALEMODE_PIXELART);  // sharp, and even at non-integer scales
    SDL_AudioSpec spec{SDL_AUDIO_F32, 1, kRate};
    c.audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (c.audio) SDL_ResumeAudioStreamDevice(c.audio);
    else LOG("no audio: %s", SDL_GetError());
  }

  // --program (or tests): straight into that game, and leaving it quits. Otherwise the launcher.
  if (opt.program_given || opt.headless || !c.renderer) {
    RunProgram(c);
  } else {
    launcher::Launcher l(c.renderer, &c.games);
    l.Enter(0);
    while (RunLauncher(c, l)) {
      int game = l.Selected();
      GameEnd end = RunEntry(c, c.games[size_t(game)], false);
      if (end == GameEnd::kQuitApp) break;
      l.Enter(game);
    }
  }

  if (!opt.wav.empty()) SaveWav(opt.wav, c.wav, kRate);
  input::Shutdown();
  if (c.audio) SDL_DestroyAudioStream(c.audio);
  if (c.texture) SDL_DestroyTexture(c.texture);
  crt::Shutdown();
  if (c.renderer) SDL_DestroyRenderer(c.renderer);
  if (c.window) SDL_DestroyWindow(c.window);
  SDL_Quit();
  return 0;
}
