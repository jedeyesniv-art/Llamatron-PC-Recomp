#include "machine.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "cpu.h"
#include "log.h"

namespace st {
namespace {

// TOS stand-ins in the ROM area (offsets from kRomBase).
constexpr uint32_t kRomTrap1 = 0x000, kRomTrap13 = 0x004, kRomTrap14 = 0x008;  // ILLEGAL; RTE
constexpr uint32_t kRomRte = 0x00C, kRomRts = 0x010;
constexpr uint32_t kRomIkbdDefault = 0x014;  // tst.b $fffc02.w; rts (TOS's handler, discarding)
constexpr uint32_t kRomPterm = 0x01C;        // the program's return address: ILLEGAL
constexpr uint32_t kRomAcia = 0x040;         // MFP ACIA interrupt (see BuildRom)
constexpr uint32_t kRomVectors = 0x400;      // one ILLEGAL per exception vector: unhandled exceptions

constexpr uint32_t kSsp = 0x8000;        // supervisor stack (TOS's is in low memory too)
constexpr uint32_t kKbdvbase = 0x2000;   // the IKBD vector table (XBIOS Kbdvbase), 9 longs
constexpr uint32_t kScreen = 0xF8000;    // TOS's screen at the top of RAM
constexpr uint32_t kHitpa = kScreen;

constexpr double kMfpHz = 2457600.0;
constexpr double kCyclesPerMfp = kCpuHz / kMfpHz;
constexpr uint64_t kAciaByteCycles = 10240;  // 7812.5 baud, 10 bits per byte

// MFP register indices ((address - $FFFA01) / 2).
enum { GPIP, AER, DDR, IERA, IERB, IPRA, IPRB, ISRA, ISRB, IMRA, IMRB, VR, TACR, TBCR, TCDCR, TADR };

constexpr int kChanAcia = 6, kChanTimerA = 13;
constexpr int kPrescale[8] = {0, 4, 10, 16, 50, 64, 100, 200};  // timer delay modes 1-7

void Put16(uint8_t* p, uint16_t v) { p[0] = uint8_t(v >> 8), p[1] = uint8_t(v); }
void Put32(uint8_t* p, uint32_t v) { Put16(p, uint16_t(v >> 16)), Put16(p + 2, uint16_t(v)); }
uint16_t Get16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }
uint32_t Get32(const uint8_t* p) { return uint32_t(Get16(p)) << 16 | Get16(p + 2); }

std::string Upper(std::string s) {
  for (char& c : s) c = char(toupper(uint8_t(c)));
  return s;
}

}  // namespace

bool Machine::Load(const disk::Program& program, std::map<std::string, disk::Bytes> files,
                   const std::string& save_dir, std::string* error) {
  files_ = std::move(files);
  save_dir_ = save_dir;
  uint32_t text_end = kTextBase + uint32_t(program.text.size());
  if (text_end + program.bss_size > kHitpa) return *error = "program too large", false;

  // ROM: the stubs.
  auto rom16 = [&](uint32_t off, std::initializer_list<uint16_t> words) {
    for (uint16_t w : words) Put16(&rom_[off], w), off += 2;
  };
  rom16(kRomTrap1, {0x4AFC, 0x4E73});
  rom16(kRomTrap13, {0x4AFC, 0x4E73});
  rom16(kRomTrap14, {0x4AFC, 0x4E73});
  rom16(kRomRte, {0x4E73});
  rom16(kRomRts, {0x4E75});
  rom16(kRomIkbdDefault, {0x4A38, 0xFC02, 0x4E75});
  rom16(kRomPterm, {0x4AFC});
  // The ACIA interrupt, as TOS does it: call the IKBD handler (Kbdvbase()->ikbdsys) while the ACIA still
  // requests an interrupt (GPIP bit 4 low), then end the MFP's in-service bit.
  rom16(kRomAcia, {0x48E7, 0xF0F0,                         // movem.l d0-d3/a0-a3,-(sp)
                   0x2079, uint16_t((kKbdvbase + 0x20) >> 16), uint16_t(kKbdvbase + 0x20),  // movea.l ikbdsys,a0
                   0x4E90,                                 // jsr (a0)
                   0x0838, 0x0004, 0xFA01,                 // btst #4,$fffffa01.w
                   0x67F0,                                 // beq.s (the movea)
                   0x08B8, 0x0006, 0xFA11,                 // bclr #6,$fffffa11.w (ISRB)
                   0x4CDF, 0x0F0F,                         // movem.l (sp)+,d0-d3/a0-a3
                   0x4E73});                               // rte
  for (uint32_t v = 0; v < 256; ++v) Put16(&rom_[kRomVectors + v * 2], 0x4AFC);

  // Low memory: exception vectors, system variables, the IKBD vector table.
  std::fill(ram_.begin(), ram_.end(), 0);
  uint8_t* m = ram_.data();
  Put32(m + 0, kSsp);
  for (uint32_t v = 2; v < 256; ++v) Put32(m + v * 4, kRomBase + kRomVectors + v * 2);
  Put32(m + 0x68, kRomBase + kRomRte);     // HBL
  Put32(m + 0x70, kRomBase + kRomRte);     // VBL
  Put32(m + 0x84, kRomBase + kRomTrap1);   // GEMDOS
  Put32(m + 0xB4, kRomBase + kRomTrap13);  // BIOS
  Put32(m + 0xB8, kRomBase + kRomTrap14);  // XBIOS
  Put32(m + 0x118, kRomBase + kRomAcia);   // MFP vector base $40 + channel 6
  Put32(m + 0x42E, kRamSize);              // phystop
  Put32(m + 0x432, 0x10000);               // _membot
  Put32(m + 0x436, kHitpa);                // _memtop
  Put32(m + 0x44E, kScreen);               // _v_bas_ad
  for (int i = 0; i < 9; ++i) Put32(m + kKbdvbase + i * 4, kRomBase + kRomRts);
  Put32(m + kKbdvbase + 0x20, kRomBase + kRomIkbdDefault);
  kbdvbase_ = kKbdvbase;

  // The program, relocated, and its basepage.
  std::memcpy(m + kTextBase, program.text.data(), program.text.size());
  for (uint32_t r : program.relocs) {
    if (r + 4 > program.text.size()) return *error = "bad relocation", false;
    Put32(m + kTextBase + r, Get32(m + kTextBase + r) + kTextBase);
  }
  uint8_t* bp = m + kBasepage;
  Put32(bp + 0x00, kBasepage);
  Put32(bp + 0x04, kHitpa);
  Put32(bp + 0x08, kTextBase);
  Put32(bp + 0x0C, uint32_t(program.text.size()));
  Put32(bp + 0x10, text_end);
  Put32(bp + 0x18, text_end);
  Put32(bp + 0x1C, program.bss_size);
  Put32(bp + 0x20, kBasepage + 0x80);  // DTA = the command line
  Put32(bp + 0x2C, kBasepage + 0xF0);  // empty environment

  // Hardware as TOS leaves it.
  mfp_[VR] = 0x48;  // vector base $40, software end-of-interrupt
  mfp_[IERB] = mfp_[IMRB] = 1 << kChanAcia;
  mfp_[AER] = 0x04;
  acia_ctrl_ = 0x96;
  vbase_hi_ = uint8_t(kScreen >> 16);
  vbase_mid_ = uint8_t(kScreen >> 8);
  vbase_latched_ = kScreen;
  palette_[0] = 0x777;
  ym_.Reset();

  // The CPU: in user mode at the program's entry, 4(sp) = basepage.
  cpu::Init(this);
  uint32_t usp = kHitpa - 8;
  Put32(m + usp, kRomBase + kRomPterm);
  Put32(m + usp + 4, kBasepage);
  cpu::Set(cpu::SR, 0x2300);
  cpu::Set(cpu::A7, kSsp);
  cpu::Set(cpu::USP, usp);
  cpu::Set(cpu::SR, 0x0300);
  cpu::Set(cpu::PC, kTextBase);
  return true;
}

// ST time: the CPU runs speed_ cycles per ST cycle; cpu_rem_ carries the remainder between slices.
uint64_t Machine::Now() const { return cycles_done_ + uint64_t(cpu::CyclesRun() + cpu_rem_) / uint64_t(speed_); }

double Machine::FrameHz() const { return Is60Hz() ? kCpuHz / 133604.0 : kCpuHz / 160256.0; }

bool Machine::RunFrame() {
  uint64_t frame_end = frame_start_ + (Is60Hz() ? 133604 : 160256);
  while (!ended_ && cycles_done_ < frame_end) {
    uint64_t next = frame_end;
    if (timer_a_next_) next = std::min(next, timer_a_next_);
    if (!ikbd_out_.empty() && !acia_rx_full_) next = std::min(next, std::max(acia_next_, cycles_done_ + 1));
    int slice = int(std::max<uint64_t>(next - cycles_done_, 1));
    uint64_t ran = uint64_t(cpu::Execute(slice * speed_)) + cpu_rem_;
    cycles_done_ += ran / uint64_t(speed_);
    cpu_rem_ = int(ran % uint64_t(speed_));
    RunTimers(cycles_done_);
    AciaService(cycles_done_);
  }
  // VBL: the frame just shown, then the interrupt; the shifter latches the new screen address.
  RenderFrame();
  uint32_t base = uint32_t(vbase_hi_) << 16 | uint32_t(vbase_mid_) << 8;
  if (base != vbase_latched_) ++swaps_;  // the game shows a new frame
  vbase_latched_ = base;
  vbl_pending_ = true;
  UpdateIrq();
  frame_start_ = frame_end;
  Sync();
  return !ended_;
}

void Machine::Sync() {
  uint64_t now = Now();
  if (now <= audio_synced_) return;
  ym_.Run(double(now - audio_synced_) * (2000000.0 / kCpuHz), &audio_);
  audio_synced_ = now;
}

void Machine::UpdateIrq() {
  // MFP: channels pending and unmasked, above the highest one in service (software end-of-interrupt mode).
  unsigned pending = (mfp_[IPRA] & mfp_[IMRA]) << 8 | (mfp_[IPRB] & mfp_[IMRB]);
  if (mfp_[VR] & 8) {
    unsigned isr = mfp_[ISRA] << 8 | mfp_[ISRB];
    if (isr) {
      unsigned top = 15;
      while (!(isr & (1u << top))) --top;
      pending &= ~((2u << top) - 1);
    }
  }
  int level = pending ? 6 : vbl_pending_ ? 4 : 0;
  cpu::SetIrq(level);
}

int Machine::InterruptAck(int level) {
  if (level == 6) {
    unsigned pending = (mfp_[IPRA] & mfp_[IMRA]) << 8 | (mfp_[IPRB] & mfp_[IMRB]);
    for (int ch = 15; ch >= 0; --ch) {
      if (!(pending & (1u << ch))) continue;
      uint8_t& ipr = mfp_[ch >= 8 ? IPRA : IPRB];
      uint8_t& isr = mfp_[ch >= 8 ? ISRA : ISRB];
      ipr &= uint8_t(~(1u << (ch & 7)));
      if (mfp_[VR] & 8) isr |= uint8_t(1u << (ch & 7));
      UpdateIrq();
      return (mfp_[VR] & 0xF0) + ch;
    }
    UpdateIrq();
    return 24;  // spurious
  }
  if (level == 4) vbl_pending_ = false;
  UpdateIrq();
  return -1;  // autovector (M68K_INT_ACK_AUTOVECTOR)
}

// ---- MFP ----

void Machine::MfpRaise(int ch) {
  bool a = ch >= 8;
  uint8_t bit = uint8_t(1u << (ch & 7));
  if (!(mfp_[a ? IERA : IERB] & bit)) return;
  mfp_[a ? IPRA : IPRB] |= bit;
  UpdateIrq();
}

void Machine::TimerAReload() {
  int mode = mfp_[TACR] & 15;
  if (mode == 0 || mode > 7) {  // stopped (event count / pulse modes aren't used)
    timer_a_next_ = 0;
    return;
  }
  int data = mfp_[TADR] ? mfp_[TADR] : 256;
  timer_a_period_ = kPrescale[mode] * data * kCyclesPerMfp;
  if (trace_tos) LOG("timer A: mode %d data %d -> %.0f Hz", mode, data, kMfpHz / (kPrescale[mode] * data));
  timer_a_frac_ = 0;
  timer_a_next_ = Now() + uint64_t(timer_a_period_);
}

void Machine::RunTimers(uint64_t until) {
  while (timer_a_next_ && timer_a_next_ <= until) {
    MfpRaise(kChanTimerA);
    timer_a_frac_ += timer_a_period_;
    uint64_t whole = uint64_t(timer_a_frac_);
    timer_a_frac_ -= double(whole);
    timer_a_next_ += std::max<uint64_t>(whole, 1);
  }
}

uint8_t Machine::MfpRead(int reg) {
  switch (reg) {
    case GPIP: {
      uint8_t v = 0xFF & ~0x10;               // bit 7: colour monitor, bit 5: no FDC interrupt
      if (!acia_rx_full_) v |= 0x10;          // bit 4: ACIA interrupt (active low)
      return v;
    }
    case TADR: {  // the current count
      if (!timer_a_next_) return mfp_[TADR];
      double left = double(timer_a_next_ - std::min(timer_a_next_, Now())) / timer_a_period_;
      int data = mfp_[TADR] ? mfp_[TADR] : 256;
      return uint8_t(int(left * data) + 1);
    }
    default:
      return reg < 24 ? mfp_[reg] : 0xFF;
  }
}

void Machine::MfpWrite(int reg, uint8_t v) {
  switch (reg) {
    case IERA:
    case IERB:
      mfp_[reg] = v;
      mfp_[reg + 2] &= v;  // disabling a channel clears its pending bit
      break;
    case IPRA:
    case IPRB:
    case ISRA:
    case ISRB:
      mfp_[reg] &= v;  // writing 0 clears
      break;
    case TACR:
      mfp_[TACR] = v & 0x1F;
      TimerAReload();
      break;
    case TADR:  // a running timer takes the new count at its next underflow
      mfp_[TADR] = v;
      if (timer_a_next_) timer_a_period_ = kPrescale[mfp_[TACR] & 7] * (v ? v : 256) * kCyclesPerMfp;
      break;
    default:
      if (reg < 24) mfp_[reg] = v;
      break;
  }
  UpdateIrq();
}

// ---- ACIA / IKBD ----

void Machine::IkbdSend(std::initializer_list<uint8_t> bytes) {
  if (ikbd_out_.empty() && !acia_rx_full_) acia_next_ = std::max(acia_next_, Now() + kAciaByteCycles);
  for (uint8_t b : bytes) ikbd_out_.push_back(b);
  cpu::EndSlice();  // reschedule
}

void Machine::AciaService(uint64_t until) {
  if (acia_rx_full_ || ikbd_out_.empty() || acia_next_ > until) return;
  acia_data_ = ikbd_out_.front();
  ikbd_out_.pop_front();
  acia_rx_full_ = true;
  if (acia_ctrl_ & 0x80) MfpRaise(kChanAcia);  // receive interrupts enabled
}

void Machine::SetJoystick(int port, uint8_t state) {
  if (joy_[port] == state) return;
  joy_[port] = state;
  if (joy_events_) IkbdSend({uint8_t(0xFE + port), state});
}

void Machine::KeyEvent(uint8_t scancode, bool down) { IkbdSend({uint8_t(down ? scancode : scancode | 0x80)}); }

void Machine::IkbdCommand(uint8_t b) {
  // Command lengths (command byte included); unknown commands are single bytes.
  static const std::map<uint8_t, int> kLength = {
      {0x07, 2}, {0x08, 1}, {0x09, 5}, {0x0A, 3}, {0x0B, 3}, {0x0C, 3}, {0x0D, 1}, {0x0E, 6}, {0x0F, 1},
      {0x10, 1}, {0x11, 1}, {0x12, 1}, {0x13, 1}, {0x14, 1}, {0x15, 1}, {0x16, 1}, {0x17, 2}, {0x18, 1},
      {0x19, 7}, {0x1A, 1}, {0x1B, 7}, {0x1C, 1}, {0x20, 4}, {0x21, 3}, {0x22, 3}, {0x80, 2}};
  ikbd_cmd_.push_back(b);
  auto it = kLength.find(ikbd_cmd_[0]);
  size_t need = it == kLength.end() ? 1 : size_t(it->second);
  if (ikbd_cmd_.size() < need) return;
  uint8_t cmd = ikbd_cmd_[0];
  if (trace_tos) LOG("ikbd: command $%02X", cmd);
  switch (cmd) {
    case 0x80:  // reset
      if (ikbd_cmd_[1] == 0x01) {
        joy_events_ = false;
        IkbdSend({0xF0});
      }
      break;
    case 0x14:  // joystick event reporting
      joy_events_ = true;
      break;
    case 0x15:
    case 0x1A:
      joy_events_ = false;
      break;
    case 0x16:  // joystick interrogate
      IkbdSend({0xFD, joy_[0], joy_[1]});
      break;
    default:
      break;
  }
  ikbd_cmd_.clear();
}

// ---- Shifter ----

void Machine::RenderFrame() {
  uint32_t pal[16];
  for (int i = 0; i < 16; ++i) {
    unsigned c = palette_[i];
    auto ch = [](unsigned v) { return (v << 5) | (v << 2) | (v >> 1); };  // 3 bits -> 8
    pal[i] = 0xFF000000u | ch((c >> 8) & 7) << 16 | ch((c >> 4) & 7) << 8 | ch(c & 7);
  }
  uint32_t base = vbase_latched_;
  for (int y = 0; y < kScreenH; ++y) {
    uint32_t* out = &frame_[y * kScreenW];
    for (int g = 0; g < 20; ++g) {
      uint32_t a = (base + uint32_t(y * 160 + g * 8)) % kRamSize;
      uint16_t p0 = Get16(&ram_[a]), p1 = Get16(&ram_[a + 2]), p2 = Get16(&ram_[a + 4]), p3 = Get16(&ram_[a + 6]);
      for (int x = 0; x < 16; ++x) {
        int s = 15 - x;
        int idx = ((p0 >> s) & 1) | ((p1 >> s) & 1) << 1 | ((p2 >> s) & 1) << 2 | ((p3 >> s) & 1) << 3;
        out[g * 16 + x] = pal[idx];
      }
    }
  }
}

// ---- Bus ----

uint32_t Machine::Read8(uint32_t a) {
  a &= 0xFFFFFF;
  if (a < kRamSize) return ram_[a];
  if (a >= kRomBase && a < kRomBase + kRomSize) return rom_[a - kRomBase];
  if (a < 0xFF8000) return 0xFF;
  if ((a & 0xFFFF00) == 0xFF8800) return (a & 2) ? 0xFF : ym_.Read(ym_sel_);
  if (a >= 0xFFFA00 && a < 0xFFFA40) return (a & 1) ? MfpRead(int(a - 0xFFFA01) / 2) : 0xFF;
  switch (a) {
    case 0xFF8201: return vbase_hi_;
    case 0xFF8203: return vbase_mid_;
    case 0xFF820A: return sync_ | 0xFC;
    case 0xFF8260: return res_ | 0xFC;
    case 0xFF8604:
    case 0xFF8605: return 0;  // FDC status: bit 6 clear = the disk isn't write protected
    case 0xFFFC00: return uint8_t((acia_rx_full_ ? 0x81 : 0) | 2);  // IRQ, RDRF; TDRE always
    case 0xFFFC02: {
      uint8_t v = acia_data_;
      if (acia_rx_full_) {
        acia_rx_full_ = false;
        acia_next_ = std::max(acia_next_, Now() + kAciaByteCycles);
        cpu::EndSlice();
      }
      return v;
    }
    case 0xFFFC04: return 2;
    default: break;
  }
  if (a >= 0xFF8240 && a < 0xFF8260) {
    uint16_t c = palette_[(a - 0xFF8240) / 2];
    return (a & 1) ? (c & 0xFF) : (c >> 8);
  }
  return 0xFF;
}

uint32_t Machine::Read16(uint32_t a) {
  a &= 0xFFFFFF;
  if (a + 1 < kRamSize) return uint32_t(ram_[a]) << 8 | ram_[a + 1];
  return Read8(a) << 8 | Read8(a + 1);
}

void Machine::Write8(uint32_t a, uint8_t v) {
  a &= 0xFFFFFF;
  if (a < kRamSize) {
    ram_[a] = v;
    return;
  }
  if (a < 0xFF8000) return;
  if ((a & 0xFFFF00) == 0xFF8800) {
    if (a & 2) {
      Sync();
      ym_.Write(ym_sel_, v);
    } else {
      ym_sel_ = v & 15;
    }
    return;
  }
  if (a >= 0xFFFA00 && a < 0xFFFA40) {
    if (a & 1) MfpWrite(int(a - 0xFFFA01) / 2, v);
    return;
  }
  if (a >= 0xFF8240 && a < 0xFF8260) {
    uint16_t& c = palette_[(a - 0xFF8240) / 2];
    c = (a & 1) ? uint16_t((c & 0xFF00) | v) : uint16_t((c & 0xFF) | v << 8);
    c &= 0x777;
    return;
  }
  switch (a) {
    case 0xFF8201: vbase_hi_ = v; break;
    case 0xFF8203: vbase_mid_ = v; break;
    case 0xFF820A: sync_ = v & 3; break;
    case 0xFF8260: res_ = v & 3; break;
    case 0xFFFC00: acia_ctrl_ = v; break;
    case 0xFFFC02: IkbdCommand(v); break;
    default: break;
  }
}

void Machine::Write16(uint32_t a, uint16_t v) {
  a &= 0xFFFFFF;
  if (a + 1 < kRamSize) {
    ram_[a] = uint8_t(v >> 8);
    ram_[a + 1] = uint8_t(v);
    return;
  }
  Write8(a, uint8_t(v >> 8));
  Write8(a + 1, uint8_t(v));
}

// ---- TOS ----

std::string Machine::ReadString(uint32_t a) {
  std::string s;
  for (; a < kRamSize && ram_[a] && s.size() < 256; ++a) s += char(ram_[a]);
  return s;
}

bool Machine::IllegalOpcode(int) {
  uint32_t pc = cpu::Get(cpu::PPC) & 0xFFFFFF;
  if (auto hook = hooks_.find(pc); hook != hooks_.end()) {
    hook->second();
    return true;
  }
  if (pc < kRomBase || pc >= kRomBase + kRomSize) return false;
  uint32_t off = pc - kRomBase;
  if (off == kRomPterm) {
    LOG("program returned (rts from its entry)");
    ended_ = true;
    cpu::EndSlice();
    return true;
  }
  if (off >= kRomVectors) {
    uint32_t vec = (off - kRomVectors) / 2;
    uint32_t ssp = cpu::Get(cpu::A7);
    LOG("unhandled exception vector %u ($%X) from pc $%06X (sr $%04X)", vec, vec * 4, Get32(&ram_[ssp + 2]),
        Get16(&ram_[ssp]));
    ended_ = true;
    cpu::EndSlice();
    return true;
  }
  // A trap: the exception frame (SR, PC) is on the supervisor stack, the arguments on the caller's stack.
  uint32_t ssp = cpu::Get(cpu::A7) & 0xFFFFFF;
  if (ssp + 6 > kRamSize) {
    LOG("trap with a bad supervisor stack $%X (pc $%X)", ssp, pc);
    ended_ = true;
    cpu::EndSlice();
    return true;
  }
  uint16_t sr = Get16(&ram_[ssp]);
  uint32_t ret = Get32(&ram_[ssp + 2]);
  uint32_t args = (sr & 0x2000) ? ssp + 6 : cpu::Get(cpu::USP);
  uint32_t d0 = 0;
  uint16_t fn = Get16(&ram_[args]);
  if (off == kRomTrap1 && fn == 0x20) {  // Super(stack): switch to supervisor mode; the user stack becomes SSP
    uint32_t stack = Get32(&ram_[args + 2]);
    if (!(sr & 0x2000)) {
      uint32_t usp = cpu::Get(cpu::USP);
      cpu::Set(cpu::D0, ssp + 6);
      cpu::Set(cpu::SR, sr | 0x2000);
      cpu::Set(cpu::A7, stack ? stack : usp);
    } else {
      cpu::Set(cpu::D0, cpu::Get(cpu::USP));
      cpu::Set(cpu::A7, ssp + 6);
      cpu::Set(cpu::USP, ssp + 6);
      cpu::Set(cpu::SR, sr & ~0x2000);
      if (stack) cpu::Set(cpu::SSP, stack);
    }
    cpu::Set(cpu::PC, ret);
    if (trace_tos) LOG("gemdos Super($%X)", stack);
    return true;
  }
  if (off == kRomTrap14 && fn == 38) {  // Supexec(func): run it in supervisor mode, then return from the trap
    uint32_t func = Get32(&ram_[args + 2]);
    uint32_t sp = ssp - 4;
    Put32(&ram_[sp], kRomBase + kRomRte);
    cpu::Set(cpu::A7, sp);
    cpu::Set(cpu::PC, func);
    return true;
  }
  bool handled = off == kRomTrap1 ? Gemdos(args, &d0) : off == kRomTrap13 ? Bios(args, &d0) : Xbios(args, &d0);
  if (!handled) {
    LOG("unimplemented %s call %u from $%06X", off == kRomTrap1 ? "GEMDOS" : off == kRomTrap13 ? "BIOS" : "XBIOS",
        fn, ret);
    d0 = uint32_t(-32);  // EINVFN
  }
  cpu::Set(cpu::D0, d0);
  // Return from the exception.
  cpu::Set(cpu::A7, ssp + 6);
  cpu::Set(cpu::SR, sr);
  cpu::Set(cpu::PC, ret);
  return true;
}

bool Machine::Gemdos(uint32_t args, uint32_t* d0) {
  const uint8_t* p = &ram_[args];
  uint16_t fn = Get16(p);
  switch (fn) {
    case 0x00:
    case 0x4C:
      LOG("program ended (Pterm)");
      ended_ = true;
      cpu::EndSlice();
      return true;
    case 0x0E: *d0 = 1; return true;  // Dsetdrv: drive bitmap (A:)
    case 0x19: *d0 = 0; return true;  // Dgetdrv
    case 0x4A: *d0 = 0; return true;  // Mshrink
    case 0x3C:                        // Fcreate(name, attr)
    case 0x3D: {                      // Fopen(name, mode)
      std::string name = Upper(ReadString(Get32(p + 2)));
      size_t slash = name.find_last_of("\\:");
      if (slash != std::string::npos) name = name.substr(slash + 1);
      if (fn == 0x3C) {
        files_[name].clear();
      } else if (!files_.count(name)) {
        if (trace_tos) LOG("gemdos Fopen(%s): not found", name.c_str());
        *d0 = uint32_t(-33);  // EFILNF
        return true;
      }
      int h = 6;
      while (handles_.count(h)) ++h;
      handles_[h] = OpenFile{name, 0, fn == 0x3C};
      if (trace_tos) LOG("gemdos %s(%s) = %d", fn == 0x3C ? "Fcreate" : "Fopen", name.c_str(), h);
      *d0 = uint32_t(h);
      return true;
    }
    case 0x3E: {  // Fclose(handle)
      auto it = handles_.find(Get16(p + 2));
      if (it == handles_.end()) return *d0 = uint32_t(-37), true;
      if (it->second.written) SaveFile(it->second.name);
      handles_.erase(it);
      *d0 = 0;
      return true;
    }
    case 0x3F:    // Fread(handle, count, buf)
    case 0x40: {  // Fwrite(handle, count, buf)
      auto it = handles_.find(Get16(p + 2));
      if (it == handles_.end()) return *d0 = uint32_t(-37), true;
      uint32_t count = Get32(p + 4), buf = Get32(p + 8) & 0xFFFFFF;
      OpenFile& f = it->second;
      disk::Bytes& data = files_[f.name];
      count = std::min(count, kRamSize - std::min(buf, kRamSize));
      if (fn == 0x3F) {
        size_t n = std::min<size_t>(count, data.size() - std::min(f.pos, data.size()));
        std::memcpy(&ram_[buf], data.data() + f.pos, n);
        f.pos += n;
        *d0 = uint32_t(n);
      } else {
        if (data.size() < f.pos + count) data.resize(f.pos + count);
        std::memcpy(data.data() + f.pos, &ram_[buf], count);
        f.pos += count;
        f.written = true;
        *d0 = count;
      }
      if (trace_tos) LOG("gemdos %s(%s, %u) = %d", fn == 0x3F ? "Fread" : "Fwrite", f.name.c_str(), count, int(*d0));
      return true;
    }
    case 0x42: {  // Fseek(offset, handle, mode)
      auto it = handles_.find(Get16(p + 6));
      if (it == handles_.end()) return *d0 = uint32_t(-37), true;
      int32_t off = int32_t(Get32(p + 2));
      size_t size = files_[it->second.name].size();
      int mode = Get16(p + 8);
      int64_t pos = mode == 0 ? off : mode == 1 ? int64_t(it->second.pos) + off : int64_t(size) + off;
      it->second.pos = size_t(std::clamp<int64_t>(pos, 0, int64_t(size)));
      *d0 = uint32_t(it->second.pos);
      return true;
    }
    default:
      return false;
  }
}

bool Machine::Bios(uint32_t args, uint32_t* d0) {
  const uint8_t* p = &ram_[args];
  switch (Get16(p)) {
    case 1: *d0 = 0; return true;            // Bconstat
    case 3:                                  // Bconout(dev, c)
      if (Get16(p + 2) == 4) IkbdCommand(uint8_t(Get16(p + 4)));
      *d0 = 1;
      return true;
    case 5: {  // Setexc(vector, address)
      uint32_t vec = Get16(p + 2) & 0xFF, addr = Get32(p + 4);
      *d0 = Get32(&ram_[vec * 4]);
      if (addr != 0xFFFFFFFF) Put32(&ram_[vec * 4], addr);
      return true;
    }
    case 8: *d0 = uint32_t(-1); return true;  // Bcostat: ready
    case 11: *d0 = 0; return true;            // Kbshift
    default: return false;
  }
}

bool Machine::Xbios(uint32_t args, uint32_t* d0) {
  const uint8_t* p = &ram_[args];
  switch (Get16(p)) {
    case 2:  // Physbase
    case 3:  // Logbase
      *d0 = uint32_t(vbase_hi_) << 16 | uint32_t(vbase_mid_) << 8;
      return true;
    case 4: *d0 = res_; return true;  // Getrez
    case 5: {                         // Setscreen(log, phys, rez)
      uint32_t phys = Get32(p + 6);
      if (phys != 0xFFFFFFFF) vbase_hi_ = uint8_t(phys >> 16), vbase_mid_ = uint8_t(phys >> 8);
      int16_t rez = int16_t(Get16(p + 10));
      if (rez >= 0) res_ = uint8_t(rez);
      return true;
    }
    case 6: {  // Setpalette(ptr)
      uint32_t a = Get32(p + 2) & 0xFFFFFF;
      for (int i = 0; i < 16; ++i) palette_[i] = Get16(&ram_[a + i * 2]) & 0x777;
      return true;
    }
    case 7: {  // Setcolor(n, c)
      int n = Get16(p + 2) & 15;
      *d0 = palette_[n];
      int16_t c = int16_t(Get16(p + 4));
      if (c >= 0) palette_[n] = uint16_t(c) & 0x777;
      return true;
    }
    case 17:  // Random: 24 bits
      random_ = random_ * 3141592621u + 1;
      *d0 = (random_ >> 8) & 0xFFFFFF;
      return true;
    case 25: {  // Ikbdws(count - 1, ptr)
      uint32_t a = Get32(p + 4) & 0xFFFFFF;
      for (int i = 0; i <= Get16(p + 2); ++i) IkbdCommand(ram_[a + i]);
      return true;
    }
    case 34: *d0 = kbdvbase_; return true;  // Kbdvbase
    case 37: return true;                   // Vsync (not waited for)
    default: return false;
  }
}

void Machine::SaveFile(const std::string& name) {
  if (save_dir_.empty()) return;
  std::string path = save_dir_ + "/" + name;
  if (FILE* f = std::fopen(path.c_str(), "wb")) {
    const disk::Bytes& d = files_[name];
    std::fwrite(d.data(), 1, d.size(), f);
    std::fclose(f);
    LOG("saved %s (%zu bytes)", path.c_str(), d.size());
  } else {
    LOG("can't write %s", path.c_str());
  }
}

}  // namespace st
