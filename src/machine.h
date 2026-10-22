// The Atari ST as Llamatron uses it: 1 MB of RAM, the MFP 68901 (timer A, the keyboard ACIA interrupt), the
// keyboard processor (IKBD: joysticks and keys), the shifter (low resolution, palette), the YM2149, and the
// few TOS calls the game makes (high-level: there is no TOS ROM).
#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
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

// Joystick bits as the IKBD reports them.
enum : uint8_t { kJoyUp = 1, kJoyDown = 2, kJoyLeft = 4, kJoyRight = 8, kJoyFire = 0x80 };

class Machine {
 public:
  // Files on the game disk; saves go to `save_dir` (files written by the game shadow the disk's).
  bool Load(const disk::Program& program, std::map<std::string, disk::Bytes> files, const std::string& save_dir,
            std::string* error);

  // Runs one video frame (until the next VBL). Returns false if the program ended.
  bool RunFrame();

  // The last finished frame, 320x200 ARGB.
  const uint32_t* Frame() const { return frame_.data(); }
  bool Is60Hz() const { return !(sync_ & 2); }
  double FrameHz() const;

  // Audio produced so far (mono float at the rate given); the caller drains it.
  void SetSampleRate(int rate) { ym_.SetSampleRate(rate); }
  std::vector<float>& Audio() { return audio_; }

  // Input: joystick states (port 0 = the mouse port, 1 = the joystick port) and ST key scancodes.
  void SetJoystick(int port, uint8_t state);
  void KeyEvent(uint8_t scancode, bool down);

  // Bus (used by the CPU).
  uint32_t Read8(uint32_t a);
  uint32_t Read16(uint32_t a);
  void Write8(uint32_t a, uint8_t v);
  void Write16(uint32_t a, uint16_t v);
  uint8_t* Ram() { return ram_.data(); }

  // CPU callbacks.
  int InterruptAck(int level);
  bool IllegalOpcode(int opcode);

  // Where the program's text was loaded.
  uint32_t TextBase() const { return kTextBase; }

  bool trace_tos = false;

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
  void TimerAReload();
  void RunTimers(uint64_t until);

  // ACIA 6850 + IKBD.
  void IkbdCommand(uint8_t b);
  void IkbdSend(std::initializer_list<uint8_t> bytes);
  void AciaService(uint64_t until);

  // Shifter.
  void RenderFrame();

  // TOS.
  bool Gemdos(uint32_t args, uint32_t* d0);
  bool Bios(uint32_t args, uint32_t* d0);
  bool Xbios(uint32_t args, uint32_t* d0);
  void SaveFile(const std::string& name);
  std::string ReadString(uint32_t a);

  std::vector<uint8_t> ram_ = std::vector<uint8_t>(kRamSize);
  std::array<uint8_t, kRomSize> rom_{};
  std::array<uint32_t, kScreenW * kScreenH> frame_{};

  uint64_t frame_start_ = 0;   // cycle of the last VBL
  uint64_t cycles_done_ = 0;   // cycles run before the current execute slice
  uint64_t audio_synced_ = 0;  // cycle the YM has been run up to
  bool ended_ = false;

  // MFP.
  uint8_t mfp_[24] = {};       // registers by index (reg = (addr - 0xFFFA01) / 2)
  uint64_t timer_a_next_ = 0;  // cycle of the next timer A underflow (0 = stopped)
  double timer_a_period_ = 0;  // cycles per underflow
  double timer_a_frac_ = 0;

  // VBL.
  bool vbl_pending_ = false;

  // ACIA / IKBD.
  std::deque<uint8_t> ikbd_out_;  // bytes the IKBD still has to send
  uint64_t acia_next_ = 0;        // when the next byte arrives
  uint8_t acia_data_ = 0, acia_ctrl_ = 0;
  bool acia_rx_full_ = false;
  std::vector<uint8_t> ikbd_cmd_;
  bool joy_events_ = false;  // IKBD joystick event reporting (command $14)
  uint8_t joy_[2] = {};

  // Shifter.
  uint8_t vbase_hi_ = 0, vbase_mid_ = 0, res_ = 0, sync_ = 2;
  uint16_t palette_[16] = {};
  uint32_t vbase_latched_ = 0;

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
