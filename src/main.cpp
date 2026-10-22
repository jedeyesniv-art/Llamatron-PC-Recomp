// Llamatron (Atari ST, Jeff Minter / Llamasoft 1991) - native PC port.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "crash.h"
#include "disk.h"
#include "game.h"
#include "input.h"
#include "log.h"
#include "machine.h"

namespace fs = std::filesystem;

namespace {

struct Options {
  std::string disk;        // --disk=<.msa/.st>
  std::string log;         // --log=<file>
  bool trace_tos = false;  // --trace-tos
  bool fullscreen = true;  // --window / --fullscreen
  bool match_refresh = true;  // --refresh=desktop: keep the desktop's refresh rate in fullscreen
  bool headless = false;   // --headless: no window or audio (tests)
  int frames = 0;          // --frames=N: quit after N frames
  std::string wav;         // --wav=<file>: also write the audio to a WAV file (tests)
  std::map<int, std::string> shots;  // --shot=N:<file.bmp>
  // --press=N:<key>[:frames]: hold a key (SDL scancode name) or "j0"/"j1"+"fire|up|down|left|right" from frame N
  struct Press {
    int frame, length;
    std::string what;
  };
  std::vector<Press> presses;
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
    else if (Arg(a, "frames", &v)) o.frames = std::atoi(v.c_str());
    else if (Arg(a, "wav", &v)) o.wav = v;
    else if (Arg(a, "shot", &v)) o.shots[std::atoi(v.c_str())] = v.substr(v.find(':') + 1);
    else if (Arg(a, "press", &v)) {
      size_t c1 = v.find(':'), c2 = v.find(':', c1 + 1);
      o.presses.push_back({std::atoi(v.c_str()), c2 == std::string::npos ? 3 : std::atoi(v.c_str() + c2 + 1),
                           v.substr(c1 + 1, c2 == std::string::npos ? std::string::npos : c2 - c1 - 1)});
    }
    else if (a == "--trace-tos") o.trace_tos = true;
    else if (a == "--fullscreen") o.fullscreen = true;
    else if (a == "--window") o.fullscreen = false;
    else if (Arg(a, "refresh", &v)) o.match_refresh = v != "desktop";
    else if (a == "--headless") o.headless = true;
    else LOG("unknown option %s", a.c_str());
  }
  return o;
}

fs::path ExeDir() {
  const char* base = SDL_GetBasePath();
  return base ? fs::path(base) : fs::current_path();
}

// The game disk: --disk, else the first .msa/.st in game\ next to the exe, the exe's folder, or the nearest
// private\ folder up the tree (the development layout).
std::string FindDisk(const Options& o) {
  if (!o.disk.empty()) return o.disk;
  std::vector<fs::path> dirs = {ExeDir() / "game", ExeDir()};
  for (fs::path p = ExeDir(); !p.empty(); p = p.parent_path()) {
    dirs.push_back(p / "Private");
    if (p == p.parent_path()) break;
  }
  for (const fs::path& d : dirs) {
    std::error_code ec;
    if (!fs::is_directory(d, ec)) continue;
    for (const auto& e : fs::directory_iterator(d, ec)) {
      std::string ext = e.path().extension().string();
      std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
      if (ext == ".msa" || ext == ".st") return e.path().string();
    }
  }
  return {};
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
  if (SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(display, &n)) {
    std::string rates;
    for (int i = 0; i < n; ++i)
      rates += std::to_string(modes[i]->w) + "x" + std::to_string(modes[i]->h) + "@" +
               std::to_string(modes[i]->refresh_rate).substr(0, 6) + " ";
    LOG("display modes: %s", rates.c_str());
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

uint8_t PressBits(const std::string& what, int* port) {
  *port = what.size() > 1 && what[1] == '0' ? 0 : 1;
  std::string b = what.substr(2);
  return b == "fire" ? st::kJoyFire : b == "up" ? st::kJoyUp : b == "down" ? st::kJoyDown
         : b == "left" ? st::kJoyLeft : b == "right" ? st::kJoyRight : 0;
}

}  // namespace

int main(int argc, char** argv) {
  Options opt = ParseArgs(argc, argv);
  // The log: --log, else userdata\llamatron.log next to the exe.
  std::error_code ec;
  fs::path save_dir = ExeDir() / "userdata";
  fs::create_directories(save_dir, ec);
  logging::Open(opt.log.empty() ? (save_dir / "llamatron.log").string().c_str() : opt.log.c_str());
  InstallCrashHandler();

  std::string disk_path = FindDisk(opt), error;
  if (disk_path.empty()) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Llamatron",
                             "Game disk not found.\nPut your Llamatron disk image (.msa or .st) in the game folder "
                             "next to the program.", nullptr);
    return 1;
  }
  disk::Bytes image = disk::LoadImage(disk_path, &error);
  auto files = disk::ReadFiles(image);
  disk::Program program;
  if (image.empty() || !files.count("TRON1MEG.PRG") || !disk::UnpackLlamatron(files["TRON1MEG.PRG"], &program, &error)) {
    if (error.empty()) error = "TRON1MEG.PRG not found on the disk";
    std::string msg = disk_path + ":\n" + error;
    LOG("%s", msg.c_str());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Llamatron", msg.c_str(), nullptr);
    return 1;
  }
  LOG("disk %s: %zu files, program %zu bytes, %zu relocations", disk_path.c_str(), files.size(), program.text.size(),
      program.relocs.size());

  // Saves: userdata\ next to the exe; files there replace the disk's.
  for (const auto& e : fs::directory_iterator(save_dir, ec)) {
    std::string name = e.path().filename().string();
    std::transform(name.begin(), name.end(), name.begin(), ::toupper);
    if (name.size() > 4 && name.substr(name.size() - 4) == ".LOG") continue;
    disk::Bytes d;
    if (FILE* f = std::fopen(e.path().string().c_str(), "rb")) {
      d.resize(size_t(fs::file_size(e.path(), ec)));
      d.resize(std::fread(d.data(), 1, d.size(), f));
      std::fclose(f);
      files[name] = std::move(d);
      LOG("using saved %s", name.c_str());
    }
  }

  static st::Machine machine;
  machine.trace_tos = opt.trace_tos;
  if (!machine.Load(program, std::move(files), save_dir.string(), &error)) {
    LOG("load failed: %s", error.c_str());
    return 1;
  }

  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
    LOG("SDL_Init: %s", SDL_GetError());
    return 1;
  }
  input::Init();
  LogDisplayModes();

  SDL_Window* window = nullptr;
  SDL_Renderer* renderer = nullptr;
  SDL_Texture* texture = nullptr;
  SDL_AudioStream* audio = nullptr;
  const int kRate = 48000;
  machine.SetSampleRate(kRate);
  if (!opt.headless) {
    window = SDL_CreateWindow("Llamatron", st::kScreenW * 4, st::kScreenH * 4,
                              SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    renderer = SDL_CreateRenderer(window, nullptr);
    SDL_SetRenderVSync(renderer, 1);
    if (opt.match_refresh) ChooseFullscreenMode(window, 50.0);
    if (opt.fullscreen) SDL_SetWindowFullscreen(window, true);
    SDL_SetRenderLogicalPresentation(renderer, st::kScreenW, st::kScreenH, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, st::kScreenW,
                                st::kScreenH);
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    SDL_AudioSpec spec{SDL_AUDIO_F32, 1, kRate};
    audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (audio) SDL_ResumeAudioStreamDevice(audio);
    else LOG("no audio: %s", SDL_GetError());
  }

  std::vector<float> wav;
  uint64_t start = SDL_GetTicksNS();
  double frames_due = 0;
  int frame = 0, vsyncs = 0, lock_k = -1;
  // Timing stats, logged every 10 s: game frames and presents per second, longest gap between presents.
  uint64_t stats_start = SDL_GetTicksNS(), last_present = 0, worst_gap = 0;
  int stats_frames = 0, stats_presents = 0;
  bool running = true, at_title = false;
  while (running) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_EVENT_QUIT) running = false;
      if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat &&
          (e.key.scancode == SDL_SCANCODE_F11 ||
           (e.key.scancode == SDL_SCANCODE_RETURN && (e.key.mod & SDL_KMOD_ALT)))) {
        SDL_SetWindowFullscreen(window, !(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN));
        continue;
      }
      input::HandleEvent(e, &machine);
    }

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
      }
    }
    for (int i = 0; i < run && running; ++i) {
      frames_due += 1;
      input::Update(&machine);
      for (const auto& p : opt.presses) {  // scripted input (tests)
        if (frame < p.frame || frame >= p.frame + p.length) continue;
        if (p.what[0] == 'j') {
          int port;
          uint8_t bits = PressBits(p.what, &port);
          machine.SetJoystick(port, bits);
        } else if (frame == p.frame || frame == p.frame + p.length - 1) {
          SDL_Scancode sc = SDL_GetScancodeFromName(p.what.c_str());
          SDL_Event ke{};
          ke.type = frame == p.frame ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
          ke.key.scancode = sc;
          input::HandleEvent(ke, &machine);
        }
      }
      if (!machine.RunFrame()) running = false;
      ++frame;
      ++stats_frames;
      bool title = game::AtTitle(machine);
      if (title != at_title) {
        if (title) LOG("frame %d: title screen", frame);
        else LOG("frame %d: left the title (%s)", frame, game::TwinStick(machine) ? "twin-stick" : "one stick");
        at_title = title;
      }
      auto shot = opt.shots.find(frame);
      if (shot != opt.shots.end()) SaveShot(machine, shot->second);
      if (opt.frames && frame >= opt.frames) running = false;
    }

    std::vector<float>& samples = machine.Audio();
    if (!opt.wav.empty()) wav.insert(wav.end(), samples.begin(), samples.end());
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
      SDL_RenderTexture(renderer, texture, nullptr, nullptr);
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

  LOG("exit after %d frames", frame);
  if (!opt.wav.empty()) SaveWav(opt.wav, wav, kRate);
  input::Shutdown();
  if (audio) SDL_DestroyAudioStream(audio);
  if (texture) SDL_DestroyTexture(texture);
  if (renderer) SDL_DestroyRenderer(renderer);
  if (window) SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
