// Llamatron (Atari ST, Jeff Minter / Llamasoft 1991) - native PC port.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
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
  bool fullscreen = false; // --fullscreen
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

  SDL_Window* window = nullptr;
  SDL_Renderer* renderer = nullptr;
  SDL_Texture* texture = nullptr;
  SDL_AudioStream* audio = nullptr;
  const int kRate = 48000;
  machine.SetSampleRate(kRate);
  if (!opt.headless) {
    window = SDL_CreateWindow("Llamatron", st::kScreenW * 4, st::kScreenH * 4,
                              SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                                  (opt.fullscreen ? SDL_WINDOW_FULLSCREEN : 0));
    renderer = SDL_CreateRenderer(window, nullptr);
    SDL_SetRenderVSync(renderer, 1);
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
  int frame = 0;
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

    // Frames due by the wall clock at the ST's frame rate (at most a few at once after a stall).
    int run = 1;
    if (!opt.headless) {
      double t = double(SDL_GetTicksNS() - start) / 1e9;
      run = std::min(int(t * machine.FrameHz() - frames_due), 3);
      if (run < 0) run = 0;
      if (t * machine.FrameHz() - frames_due > 6) frames_due = t * machine.FrameHz();  // resync after a stall
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
      // Keep the queue short: drop audio if we got ahead (after a stall).
      if (SDL_GetAudioStreamQueued(audio) > int(kRate * sizeof(float) / 5)) SDL_ClearAudioStream(audio);
      SDL_PutAudioStreamData(audio, samples.data(), int(samples.size() * sizeof(float)));
    }
    samples.clear();

    if (renderer) {
      SDL_UpdateTexture(texture, nullptr, machine.Frame(), st::kScreenW * 4);
      SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
      SDL_RenderClear(renderer);
      SDL_RenderTexture(renderer, texture, nullptr, nullptr);
      SDL_RenderPresent(renderer);
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
