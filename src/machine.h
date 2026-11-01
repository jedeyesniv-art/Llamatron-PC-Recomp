// The Atari ST as Llamatron and Gridrunner use it: 1 MB of RAM, the MFP 68901 (timers, the keyboard ACIA
// interrupt), the keyboard processor (IKBD: joysticks, mouse, keys), the shifter (low resolution, palette, drawn
// line by line for raster effects), the YM2149, and the TOS calls the games make (high-level: no TOS ROM).
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "disk.h"
#include "ym2149.h"

namespace st {

constexpr uint32_t kRamSize = 0x100000;  // 1 MB (the game requires it)
constexpr uint32_t kRomBase = 0xFC0000;  // our TOS stubs
constexpr uint32_t kRomSize = 0x1000;
constexpr double kCpuHz = 8021247.0;     // PAL ST
constexpr int kScreenW = 320, kScreenH = 200;
constexpr int kMaxFrameW = 640;  // medium resolution: 640 x 200, 4 colours

// Joystick bits as the IKBD reports them.
enum : uint8_t { kJoyUp = 1, kJoyDown = 2, kJoyLeft = 4, kJoyRight = 8, kJoyFire = 0x80 };

class Machine {
 public:
  // Files on the game disk; saves go to `save_dir` (files written by the game shadow the disk's).
  bool Load(const disk::Program& program, std::map<std::string, disk::Bytes> files, const std::string& save_dir,
            std::string* error);

  // CPU speed as a multiple of the ST's 8 MHz (1 = original). Everything else keeps its real-time rate, so a
  // faster CPU only means the game's work is done sooner. Can change between frames.
  void SetCpuSpeed(int k) { speed_ = std::max(1, k); }
  int CpuSpeed() const { return speed_; }
  // Calm background (anti-flash): colour 0 is shown through a peak hold over the last 16 frames, then smoothed and
  // capped, so a strobing background becomes a steady glow and a flash a soft pulse. Per frame: only for programs
  // that don't change colour 0 mid-frame (Trip-A-Tron while performing).
  // hold = frames of peak hold (strobes: 16), cap = the brightest a channel may get (0..1).
  // release = how fast it fades (per frame).
  void SetCalmBackground(bool on, int hold = 16, float cap = 0.4f, float release = 0.05f);
  bool CalmBackground() const { return calm_; }
  const float* CalmColour() const { return calm_rgb_; }  // colour 0 as shown while calm (0..1)
  // Screen swaps (the game showing a new frame) since the last call.
  int TakeSwaps() { int n = swaps_; swaps_ = 0; return n; }
  // VBL interrupts the CPU took since the last call (fewer than frames: the program missed some).
  int TakeVbls() { int n = vbls_taken_; vbls_taken_ = 0; return n; }

  // Runs one video frame (until the next VBL). Returns false if the program ended.
  bool RunFrame();

  // The last finished frame, 320x200 ARGB.
  const uint32_t* Frame() const { return frame_.data(); }
  int FrameWidth() const { return frame_w_; }  // 320 (low resolution) or 640 (medium), rows FrameWidth() apart
  const uint16_t* Palette() const { return palette_; }  // the shifter's colour registers now
  bool Is60Hz() const { return !(sync_ & 2); }
  double FrameHz() const;

  // Audio produced so far (mono float at the rate given); the caller drains it.
  void SetSampleRate(int rate) { ym_.SetSampleRate(rate), sample_rate_ = rate; }
  std::vector<float>& Audio() { return audio_; }

  // Input: joystick states (port 0 = the mouse port, 1 = the joystick port) and ST key scancodes.
  void SetJoystick(int port, uint8_t state);
  void KeyEvent(uint8_t scancode, bool down);
  // Keys waiting in TOS's console buffer before the program asks (answers to a crack's prompts): ASCII.
  void TypeAhead(const std::string& text);
  // Mouse movement (ST mouse units, y down) and buttons (bit 0 right, bit 1 left, as the IKBD reports them).
  void MouseInput(int dx, int dy, uint8_t buttons);

  // Bus (used by the CPU).
  uint32_t Read8(uint32_t a);
  uint32_t Read16(uint32_t a);
  void Write8(uint32_t a, uint8_t v);
  void Write16(uint32_t a, uint16_t v);
  uint8_t* Ram() { return ram_.data(); }
  const uint8_t* Ram() const { return ram_.data(); }

  // CPU callbacks.
  int InterruptAck(int level);
  bool IllegalOpcode(int opcode);

  // Where the program's text was loaded.
  uint32_t TextBase() const { return kTextBase; }

  // Code hooks: the game instruction at `addr` is replaced by ILLEGAL; when reached, `fn` runs instead (with the
  // machine it runs in) and must do the replaced instruction's work and set the PC to continue.
  void AddHook(uint32_t addr, std::function<void(Machine&)> fn) { hooks_[addr] = std::move(fn); }

  bool trace_tos = false;
  // A look-ahead copy (the machine is copyable; run it with cpu::SetMachine): no picture, no sound, no files.
  bool sandbox = false;

 private:
  static constexpr uint32_t kBasepage = 0x10000, kTextBase = kBasepage + 0x100;

  // Scheduling: cycles since power-on.
  uint64_t Now() const;
  void Sync();  // catches the devices up to the CPU (YM audio)
  void UpdateIrq();

  // MFP 68901.
  uint8_t MfpRead(int reg);
  void MfpWrite(int reg, uint8_t v);
  void MfpRaise(int channel);
  int TimerMode(int t) const;  // control mode: 0 stopped, 1-7 delay (prescaler), 8 event count
  void TimerReload(int t);
  void RunTimers(uint64_t until);
  void DisplayLineEnd();  // timer B event count input (display enable)

  // ACIA 6850 + IKBD.
  void IkbdCommand(uint8_t b);
  void IkbdSend(std::initializer_list<uint8_t> bytes);
  void AciaService(uint64_t until);
  void SendMouse();
  void IkbdSysHle();         // TOS's IKBD handler: packets -> Kbdvbase vectors, keys -> the console buffer
  void ConsoleKey(uint8_t scancode);

  // Shifter.
  void RenderLine(int y);

  // TOS.
  bool Gemdos(uint32_t args, uint32_t* d0);
  bool Bios(uint32_t args, uint32_t* d0);
  bool Xbios(uint32_t args, uint32_t* d0);
  void Gem(uint32_t d0, uint32_t pb);
  void LineA(int fn);
  int GetPixel(int x, int y);  // logical screen, low resolution
  void SetPixel(int x, int y, int colour);
  // TOS's VT52 console: Cconws / Cconout / Crawio / Bconout(2) text drawn into the logical screen (low
  // resolution, 40 x 25 cells of 8 x 8; the font is SDL's debug font, the public domain IBM VGA one).
  void ConsoleOut(uint8_t c);
  void ConsoleCell(int col, int row, uint8_t c);
  void ConsoleClear(int col0, int row0, int col1, int row1);  // cells, inclusive
  void ConsoleScroll(int top, int dir);                        // from row `top` down: up (1) or down (-1)
  int con_x_ = 0, con_y_ = 0, con_esc_ = 0, con_row_ = 0, con_fg_ = 15, con_bg_ = 0, con_sx_ = 0, con_sy_ = 0;
  bool con_rev_ = false, con_wrap_ = true;
  int ConsoleCols() const { return res_ == 1 ? 80 : 40; }
  void PlotMode(int x, int y, bool on, int colour, int mode);
  uint32_t logbase_ = 0;  // where TOS (Line-A) draws: Setscreen's logical screen
  std::set<int> gem_seen_;  // GEM calls logged once
  int gem_mouse_x_ = 160, gem_mouse_y_ = 100;
  void SaveFile(const std::string& name);
  std::string ReadString(uint32_t a);
  std::string FileName(uint32_t a);  // a path from the program: upper case, without folders or drive
  // Processes (Pexec): places a program at basepage `bp` (TPA up to `hitpa`), relocated, BSS cleared.
  void PlaceProgram(uint32_t bp, uint32_t hitpa, const disk::Program& program, uint32_t parent, uint32_t cmdline);
  bool Pexec(uint32_t args, uint32_t* d0, bool* started);
  void StartProcess(uint32_t bp);  // in user mode at its text, 4(sp) = basepage, returning to kRomPterm
  bool EndProcess(uint32_t code);  // back to the parent's Pexec (false: the first program ended)
  struct Process {
    uint32_t bp = 0, end = 0;  // the parent: basepage, end of its memory (Mshrink)
    uint32_t regs[16] = {}, usp = 0, ssp = 0, ret = 0;  // its Pexec trap: registers, stacks, return address
    uint16_t sr = 0;
  };
  std::vector<Process> parents_;
  uint32_t cur_bp_ = 0, cur_end_ = 0;  // the running process: basepage, end of its memory
  uint32_t malloc_top_ = 0;            // Malloc hands out memory downwards from here
  std::map<uint32_t, uint32_t> mallocs_;  // address -> size
  bool trap_wait_ = false;
  uint32_t dta_ = 0;                // Fsetdta (0: the basepage's)
  std::deque<std::string> search_;  // Fsfirst matches not returned yet  // the call has to wait (Cconin without a key): run the trap again

  std::vector<uint8_t> ram_ = std::vector<uint8_t>(kRamSize);
  std::map<uint32_t, std::function<void(Machine&)>> hooks_;
  std::array<uint8_t, kRomSize> rom_{};
  std::array<uint32_t, kMaxFrameW * kScreenH> frame_{};
  int frame_w_ = kScreenW;
  uint8_t frame_res_ = 0;  // the resolution of the frame being drawn (taken at its first line)

  int speed_ = 1;              // CPU cycles per ST cycle
  int cpu_rem_ = 0;            // CPU cycles run that don't make a whole ST cycle yet
  int swaps_ = 0, vbls_taken_ = 0;
  // Time is counted in ST cycles (8 MHz).
  uint64_t frame_start_ = 0;   // cycle of the last VBL
  uint64_t cycles_done_ = 0;   // cycles run before the current execute slice
  uint64_t audio_synced_ = 0;  // cycle the YM has been run up to
  bool ended_ = false;

  // MFP.
  uint8_t mfp_[24] = {};       // registers by index (reg = (addr - 0xFFFA01) / 2)
  struct Timer {
    uint64_t next = 0;  // delay mode: cycle of the next underflow (0 = not running)
    double period = 0;  // cycles per underflow
    double frac = 0;
    int count = 0;      // event count mode: the main counter
  } timers_[4];         // A, B, C, D
  int line_ = 0;        // next display line to draw this frame

  // VBL.
  bool vbl_pending_ = false;

  // ACIA / IKBD.
  std::deque<uint8_t> ikbd_out_;  // bytes the IKBD still has to send
  uint64_t acia_next_ = 0;        // when the next byte arrives
  uint8_t acia_data_ = 0, acia_ctrl_ = 0;
  bool acia_rx_full_ = false;
  std::vector<uint8_t> ikbd_cmd_;
  bool joy_events_ = true;   // IKBD joystick event reporting (power-on default; command $14)
  uint8_t joy_[2] = {};
  bool mouse_on_ = true;     // relative mouse reporting (command $08; off with $12 or joystick commands)
  bool mouse_y_up_ = false;  // command $0F: y = 0 at the bottom
  int mouse_thr_x_ = 1, mouse_thr_y_ = 1;
  int mouse_dx_ = 0, mouse_dy_ = 0;
  uint8_t mouse_buttons_ = 0, mouse_sent_buttons_ = 0;
  std::vector<uint8_t> packet_;  // TOS handler: the packet being received
  size_t packet_need_ = 0;
  uint8_t packet_joy_[2] = {};
  std::deque<uint32_t> console_keys_;  // TOS console buffer: scancode << 16 | ASCII
  uint8_t modifiers_ = 0;  // Kbshift bits: 0 right shift, 1 left shift, 2 control, 3 alternate

  // Shifter.
  uint8_t vbase_hi_ = 0, vbase_mid_ = 0, res_ = 0, sync_ = 2;
  uint16_t palette_[16] = {};
  // STE DMA sound ($FF8900-$FF8921) and the Microwire ($FF8922 data, $FF8924 mask: reads back what was
  // written, i.e. "sent"); the LMC1992 volume / tone it talks to isn't modelled (full volume).
  uint8_t ste_snd_[0x100] = {};
  bool dma_on_ = false;
  double dma_pos_ = 0;  // the address being played (fractional: the rate differs from ours)
  uint32_t dma_end_ = 0;
  int sample_rate_ = 48000;
  void DmaStart();
  void MixDma(size_t from);  // DMA sound into audio_[from..]
  // STE colours: 4 bits a channel (the 4th bit is the lowest: 0..7 shifted up, bit 3 added). Used once a
  // program writes a colour with a bit 3 set; until then the ST's 3 bits (so ST games keep their exact look).
  bool ste_colours_ = false;
 public:
  // An STE program (the registry says so): its colours' 4th bit counts. Off: ST games that write $FFF for white
  // keep the ST's colours.
  void SetSte(bool on) { ste_ = on; }
  // Start at 60 Hz, as an ST with US TOS does (programs that read $FF820A at boot pick their 60 Hz mode).
  void SetBoot60Hz(bool on) { sync_ = on ? 0 : 2; }
 private:
  bool ste_ = false;
  std::set<uint32_t> io_logged_;  // I/O addresses we don't handle, logged once
  uint32_t vbase_latched_ = 0;
  bool calm_ = false;
  float calm_rgb_[3] = {};      // colour 0 as shown (0..1)
  uint16_t calm_hist_[16] = {};  // colour 0 in the last frames
  int calm_at_ = 0;
  int calm_hold_ = 16;
  float calm_cap_ = 0.4f, calm_release_ = 0.05f;
  void CalmFrame();

  // YM2149.
  Ym2149 ym_;
  int ym_sel_ = 0;
  std::vector<float> audio_;

  // TOS state.
  std::map<std::string, disk::Bytes> files_;  // upper-case names
  std::string save_dir_;
  struct OpenFile {
    std::string name;
    size_t pos = 0;
    bool written = false;
  };
  std::map<int, OpenFile> handles_;
  uint32_t kbdvbase_ = 0;
  uint32_t random_ = 0x12345678;
};

}  // namespace st
