#include "machine.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "cpu.h"
#include "log.h"

namespace st {
namespace {

// TOS stand-ins in the ROM area (offsets from kRomBase).
constexpr uint32_t kRomTrap1 = 0x000, kRomTrap13 = 0x004, kRomTrap14 = 0x008;  // ILLEGAL; RTE
constexpr uint32_t kRomRte = 0x00C, kRomRts = 0x010;
constexpr uint32_t kRomIkbdDefault = 0x014;  // TOS's IKBD handler (ikbdsys): ILLEGAL -> IkbdSysHle
constexpr uint32_t kRomPterm = 0x01C;        // the program's return address: ILLEGAL
constexpr uint32_t kRomTrap2 = 0x020;        // GEM (AES / VDI): ILLEGAL; RTE
constexpr uint32_t kRomLineA = 0x024;        // Line-A exception: ILLEGAL (LineA does the work and returns)
constexpr uint32_t kRomFont = 0x100;         // an empty font header for Line-A's font table (90 bytes)
constexpr uint32_t kRomVbl = 0x200;          // TOS's VBL routine (see Load)
constexpr uint32_t kLineA = 0x1A00;          // the Line-A variables (negative offsets below it too)
constexpr uint32_t kVblQueue = 0x1C00;       // TOS's VBL queue: 8 slots (programs add routines there)
constexpr uint32_t kRomAcia = 0x040;         // MFP ACIA interrupt (see Load)
constexpr uint32_t kRomIkbdCall = 0x080;     // calls a Kbdvbase vector with a packet (see Load)
constexpr uint32_t kRomVectors = 0x400;      // one ILLEGAL per exception vector: unhandled exceptions

constexpr uint32_t kSsp = 0x8000;        // supervisor stack (TOS's is in low memory too)
constexpr uint32_t kKbdvbase = 0x2000;   // the IKBD vector table (XBIOS Kbdvbase), 9 longs
constexpr uint32_t kIkbdTarget = kKbdvbase + 0x40, kIkbdPacket = kKbdvbase + 0x48;  // for kRomIkbdCall
constexpr uint32_t kScreen = 0xF8000;    // TOS's screen at the top of RAM
constexpr uint32_t kHitpa = kScreen;

constexpr double kMfpHz = 2457600.0;
constexpr double kCyclesPerMfp = kCpuHz / kMfpHz;
constexpr uint64_t kAciaByteCycles = 10240;  // 7812.5 baud, 10 bits per byte

// MFP register indices ((address - $FFFA01) / 2).
enum { GPIP, AER, DDR, IERA, IERB, IPRA, IPRB, ISRA, ISRB, IMRA, IMRB, VR, TACR, TBCR, TCDCR, TADR };

constexpr int kChanAcia = 6;
constexpr int kTimerChannel[4] = {13, 8, 5, 4};  // MFP channels of timers A, B, C, D
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
  rom16(kRomIkbdDefault, {0x4AFC});
  rom16(kRomPterm, {0x4AFC});
  rom16(kRomTrap2, {0x4AFC, 0x4E73});
  rom16(kRomLineA, {0x4AFC});
  // The VBL as TOS does it: count frames, take a new palette (colorptr) and screen (screenpt), then call the
  // routines in the VBL queue.
  rom16(kRomVbl, {0x52B8, 0x0466,          // addq.l #1,frclock.w
                  0x48E7, 0xFFFE,          // movem.l d0-d7/a0-a6,-(sp)
                  0x52B8, 0x0462,          // addq.l #1,_vbclock.w
                  0x2038, 0x045A,          // move.l colorptr.w,d0
                  0x6712,                  // beq.s (no new palette)
                  0x2040,                  // movea.l d0,a0
                  0x43F8, 0x8240,          // lea $ff8240.w,a1
                  0x7007,                  // moveq #7,d0
                  0x22D8,                  // move.l (a0)+,(a1)+
                  0x51C8, 0xFFFC,          // dbra d0,(the move)
                  0x42B8, 0x045A,          // clr.l colorptr.w
                  0x2038, 0x045E,          // move.l screenpt.w,d0
                  0x670C,                  // beq.s (no new screen)
                  0xE088,                  // lsr.l #8,d0
                  0x11C0, 0x8203,          // move.b d0,$ff8203.w
                  0xE048,                  // lsr.w #8,d0
                  0x11C0, 0x8201,          // move.b d0,$ff8201.w
                  0x3E38, 0x0454,          // move.w nvbls.w,d7
                  0x5347,                  // subq.w #1,d7
                  0x6B18,                  // bmi.s (done)
                  0x2078, 0x0456,          // movea.l vblqueue.w,a0
                  0x2018,                  // loop: move.l (a0)+,d0
                  0x670C,                  // beq.s (next)
                  0x48E7, 0x0180,          // movem.l d7/a0,-(sp)
                  0x2040,                  // movea.l d0,a0
                  0x4E90,                  // jsr (a0)
                  0x4CDF, 0x0180,          // movem.l (sp)+,d7/a0
                  0x51CF, 0xFFEE,          // next: dbra d7,loop
                  0x4CDF, 0x7FFF,          // done: movem.l (sp)+,d0-d7/a0-a6
                  0x4E73});                // rte
  // Line-A's table of font header pointers (3) at kRomFont + 0x80, all to the empty header.
  for (int i = 0; i < 3; ++i) Put32(&rom_[kRomFont + 0x80 + i * 4], kRomBase + kRomFont);
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
  // A packet for a Kbdvbase vector, as TOS calls them: the packet address in a0 and on the stack.
  rom16(kRomIkbdCall, {0x2F08,                                                          // move.l a0,-(sp)
                       0x2279, uint16_t(kIkbdTarget >> 16), uint16_t(kIkbdTarget),      // movea.l target,a1
                       0x4E91,                                                          // jsr (a1)
                       0x588F,                                                          // addq.l #4,sp
                       0x4E75});                                                        // rts
  for (uint32_t v = 0; v < 256; ++v) Put16(&rom_[kRomVectors + v * 2], 0x4AFC);

  // Low memory: exception vectors, system variables, the IKBD vector table.
  std::fill(ram_.begin(), ram_.end(), 0);
  uint8_t* m = ram_.data();
  Put32(m + 0, kSsp);
  for (uint32_t v = 2; v < 256; ++v) Put32(m + v * 4, kRomBase + kRomVectors + v * 2);
  Put32(m + 0x68, kRomBase + kRomRte);     // HBL
  Put32(m + 0x70, kRomBase + kRomVbl);     // VBL
  Put16(m + 0x452, 1);                     // vblsem
  Put16(m + 0x454, 8);                     // nvbls
  Put32(m + 0x456, kVblQueue);             // _vblqueue (8 empty slots)
  Put32(m + 0x84, kRomBase + kRomTrap1);   // GEMDOS
  Put32(m + 0x88, kRomBase + kRomTrap2);   // GEM
  Put32(m + 0x28, kRomBase + kRomLineA);   // Line-A
  // Line-A variables: planes, bytes per line; below the base: screen size in pixels and bytes per line.
  Put16(m + kLineA + 0x00, 4);
  Put16(m + kLineA + 0x02, 160);
  Put16(m + kLineA - 0x0C, kScreenW);
  Put16(m + kLineA - 0x04, kScreenH);
  Put16(m + kLineA - 0x02, 160);
  logbase_ = kScreen;
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
  for (uint32_t r : program.relocs)
    if (r + 4 > program.text.size()) return *error = "bad relocation", false;
  parents_.clear();
  mallocs_.clear();
  malloc_top_ = kHitpa;
  PlaceProgram(kBasepage, kHitpa, program, 0, 0);

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

  cpu::Init(this);
  StartProcess(kBasepage);
  return true;
}

void Machine::PlaceProgram(uint32_t bp, uint32_t hitpa, const disk::Program& program, uint32_t parent,
                           uint32_t cmdline) {
  uint8_t* m = ram_.data();
  uint32_t text = bp + 0x100, size = uint32_t(program.text.size());
  uint32_t text_end = text + size;
  std::fill(m + bp, m + bp + 0x100, 0);
  std::memcpy(m + text, program.text.data(), size);
  for (uint32_t r : program.relocs)
    if (r + 4 <= size) Put32(m + text + r, Get32(m + text + r) + text);
  std::fill(m + text_end, m + std::min(text_end + program.bss_size, kRamSize), 0);
  uint8_t* b = m + bp;
  Put32(b + 0x00, bp);
  Put32(b + 0x04, hitpa);
  Put32(b + 0x08, text);
  Put32(b + 0x0C, size);  // text + data together: our loader doesn't keep them apart
  Put32(b + 0x10, text_end);
  Put32(b + 0x18, text_end);
  Put32(b + 0x1C, program.bss_size);
  Put32(b + 0x20, bp + 0x80);  // DTA = the command line
  Put32(b + 0x24, parent);
  Put32(b + 0x2C, bp + 0xF0);  // empty environment
  if (cmdline) std::memcpy(b + 0x80, m + (cmdline & 0xFFFFFF), 0x80);
  cur_bp_ = bp;
  cur_end_ = hitpa;
}

// The CPU: in user mode at the program's entry, the stack at the top of its memory with 4(sp) = basepage.
void Machine::StartProcess(uint32_t bp) {
  uint8_t* m = ram_.data();
  uint32_t usp = (Get32(m + bp + 4) & ~1u) - 8;
  Put32(m + usp, kRomBase + kRomPterm);
  Put32(m + usp + 4, bp);
  cpu::Set(cpu::SR, 0x2300);
  cpu::Set(cpu::A7, kSsp);
  cpu::Set(cpu::USP, usp);
  cpu::Set(cpu::SR, 0x0300);
  cpu::Set(cpu::PC, Get32(m + bp + 8));
  for (int r = 0; r < 15; ++r) cpu::Set(cpu::Reg(r), 0);
  cpu::Set(cpu::A0, 0);  // 0 = an application (not an accessory)
  // TOS's loader leaves A4 at the end of the program's text + data (the BSS start); packers rely on it (the
  // Pompey Packer stub on GRIDRUNR.PRG reads its packed length from just below A4).
  cpu::Set(cpu::Reg(cpu::A0 + 4), Get32(m + bp + 0x10));
  cpu::Set(cpu::Reg(cpu::A0 + 5), bp);
  cur_bp_ = bp;
  cur_end_ = Get32(m + bp + 4);
}

// Pexec(mode, name, cmdline, env): 0 load and go, 3 load (returns the basepage), 4 go (basepage), 5 create a
// basepage, 6 go and free. The parent waits in its trap; the child's Pterm comes back to it (EndProcess).
bool Machine::Pexec(uint32_t args, uint32_t* d0, bool* started) {
  const uint8_t* p = &ram_[args];
  int mode = Get16(p + 2);
  uint32_t name = Get32(p + 4), cmd = Get32(p + 8);
  uint32_t bp = 0;
  if (mode == 0 || mode == 3 || mode == 5 || mode == 7) {
    disk::Program prog;
    std::string fname;
    if (mode != 5 && mode != 7) {
      fname = FileName(name);
      auto it = files_.find(fname);
      std::string error;
      if (it == files_.end()) {
        LOG("Pexec(%d, %s): not found", mode, fname.c_str());
        return *d0 = uint32_t(-33), true;
      }
      if (!disk::LoadPrg(it->second, &prog, &error)) {
        LOG("Pexec(%d, %s): %s", mode, fname.c_str(), error.c_str());
        return *d0 = uint32_t(-66), true;  // EPLFMT
      }
    }
    // The child goes after the running program's memory (as shrunk by Mshrink), up to the Malloc'ed blocks.
    bp = (cur_end_ + 0xFF) & ~0xFFu;
    uint32_t hitpa = malloc_top_;
    if (bp + 0x100 + prog.text.size() + prog.bss_size > hitpa) {
      LOG("Pexec(%d, %s): no memory ($%X..$%X)", mode, fname.c_str(), bp, hitpa);
      return *d0 = uint32_t(-39), true;  // ENSMEM
    }
    uint32_t keep_bp = cur_bp_, keep_end = cur_end_;
    PlaceProgram(bp, hitpa, prog, keep_bp, cmd);
    cur_bp_ = keep_bp, cur_end_ = keep_end;  // still the parent running
    LOG("Pexec(%d, %s): basepage $%X, %zu bytes", mode, fname.c_str(), bp, prog.text.size());
    if (mode == 3 || mode == 5 || mode == 7) return *d0 = bp, true;
  } else if (mode == 4 || mode == 6) {
    bp = Get32(p + 8);
    LOG("Pexec(%d): go, basepage $%X", mode, bp);
  } else {
    return false;
  }
  // Go: the parent waits in this trap.
  uint32_t ssp = cpu::Get(cpu::A7);
  Process parent;
  parent.bp = cur_bp_, parent.end = cur_end_;
  for (int r = 0; r < 16; ++r) parent.regs[r] = cpu::Get(cpu::Reg(r));
  parent.usp = cpu::Get(cpu::USP);
  parent.sr = Get16(&ram_[ssp]);
  parent.ret = Get32(&ram_[ssp + 2]);
  parent.ssp = ssp + 6;
  parents_.push_back(parent);
  StartProcess(bp);
  *started = true;
  return true;
}

bool Machine::EndProcess(uint32_t code) {
  if (parents_.empty()) return false;
  Process parent = parents_.back();
  parents_.pop_back();
  LOG("process $%X ended (%d): back to $%X", cur_bp_, int(code), parent.bp);
  cur_bp_ = parent.bp, cur_end_ = parent.end;
  cpu::Set(cpu::SR, 0x2700);
  for (int r = 0; r < 15; ++r) cpu::Set(cpu::Reg(r), parent.regs[r]);
  cpu::Set(cpu::A7, parent.ssp);
  cpu::Set(cpu::USP, parent.usp);
  cpu::Set(cpu::SR, parent.sr);
  cpu::Set(cpu::D0, code);
  cpu::Set(cpu::PC, parent.ret);
  return true;
}

// ST time: the CPU runs speed_ cycles per ST cycle; cpu_rem_ carries the remainder between slices.
uint64_t Machine::Now() const { return cycles_done_ + uint64_t(cpu::CyclesRun() + cpu_rem_) / uint64_t(speed_); }

double Machine::FrameHz() const { return Is60Hz() ? kCpuHz / 133604.0 : kCpuHz / 160256.0; }

bool Machine::RunFrame() {
  // A frame is 313 lines of 512 cycles (50 Hz) or 263 of 508 (60 Hz); the 200 displayed lines start at line 63
  // (34); a line's display ends 376 cycles into it - then it's drawn and timer B counts it.
  const bool hz60 = Is60Hz();
  const uint64_t cpl = hz60 ? 508 : 512, first = hz60 ? 34 : 63;
  uint64_t frame_end = frame_start_ + (hz60 ? 133604 : 160256);
  auto line_end = [&](int l) { return frame_start_ + (first + uint64_t(l)) * cpl + 376; };
  line_ = 0;
  while (!ended_ && cycles_done_ < frame_end) {
    uint64_t next = frame_end;
    for (const Timer& t : timers_)
      if (t.next) next = std::min(next, t.next);
    if (line_ < kScreenH) next = std::min(next, line_end(line_));
    if (!ikbd_out_.empty() && !acia_rx_full_) next = std::min(next, std::max(acia_next_, cycles_done_ + 1));
    int slice = int(std::max<uint64_t>(next - cycles_done_, 1));
    uint64_t ran = uint64_t(cpu::Execute(slice * speed_)) + cpu_rem_;
    cycles_done_ += ran / uint64_t(speed_);
    cpu_rem_ = int(ran % uint64_t(speed_));
    RunTimers(cycles_done_);
    AciaService(cycles_done_);
    while (line_ < kScreenH && line_end(line_) <= cycles_done_) {
      if (!sandbox) RenderLine(line_);
      ++line_;
      DisplayLineEnd();
    }
  }
  while (!sandbox && line_ < kScreenH) RenderLine(line_++);  // (the program ended mid-frame)
  // VBL: the shifter latches the new screen address.
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
  if (sandbox) audio_synced_ = now;
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
  if (level == 4) vbl_pending_ = false, ++vbls_taken_;
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

int Machine::TimerMode(int t) const {
  switch (t) {
    case 0: return mfp_[TACR] & 15;
    case 1: return mfp_[TBCR] & 15;
    case 2: return (mfp_[TCDCR] >> 4) & 7;
    default: return mfp_[TCDCR] & 7;
  }
}

// After a control write: delay mode (1-7) runs on the clock; event count (8, timer B: display lines) and pulse
// modes don't.
void Machine::TimerReload(int t) {
  Timer& tm = timers_[t];
  int mode = TimerMode(t), data = mfp_[TADR + t] ? mfp_[TADR + t] : 256;
  if (mode < 1 || mode > 7) {
    tm.next = 0;
    if (mode == 8 && tm.count <= 0) tm.count = data;
    return;
  }
  if (tm.next) return;  // already running: a new prescaler applies from the next underflow
  tm.period = kPrescale[mode] * data * kCyclesPerMfp;
  if (trace_tos) LOG("timer %c: mode %d data %d -> %.0f Hz", 'A' + t, mode, data, kMfpHz / (kPrescale[mode] * data));
  tm.frac = 0;
  tm.next = Now() + uint64_t(tm.period);
}

void Machine::RunTimers(uint64_t until) {
  for (int t = 0; t < 4; ++t) {
    Timer& tm = timers_[t];
    while (tm.next && tm.next <= until) {
      MfpRaise(kTimerChannel[t]);
      tm.frac += tm.period;
      uint64_t whole = uint64_t(tm.frac);
      tm.frac -= double(whole);
      tm.next += std::max<uint64_t>(whole, 1);
    }
  }
}

void Machine::DisplayLineEnd() {
  if (TimerMode(1) != 8) return;
  Timer& tb = timers_[1];
  if (--tb.count <= 0) {
    MfpRaise(kTimerChannel[1]);
    tb.count = mfp_[TADR + 1] ? mfp_[TADR + 1] : 256;
  }
}

uint8_t Machine::MfpRead(int reg) {
  switch (reg) {
    case GPIP: {
      uint8_t v = 0xFF & ~0x10;               // bit 7: colour monitor, bit 5: no FDC interrupt
      if (!acia_rx_full_) v |= 0x10;          // bit 4: ACIA interrupt (active low)
      return v;
    }
    case TADR:
    case TADR + 1:
    case TADR + 2:
    case TADR + 3: {  // the current count
      const Timer& tm = timers_[reg - TADR];
      if (TimerMode(reg - TADR) == 8) return uint8_t(tm.count);
      if (!tm.next) return mfp_[reg];
      double left = double(tm.next - std::min(tm.next, Now())) / tm.period;
      int data = mfp_[reg] ? mfp_[reg] : 256;
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
    case TBCR: {
      int t = reg - TACR;
      int old = TimerMode(t);
      mfp_[reg] = v & 0x1F;
      if (TimerMode(t) != old) timers_[t].next = 0;
      TimerReload(t);
      break;
    }
    case TCDCR: {
      int oc = TimerMode(2), od = TimerMode(3);
      mfp_[TCDCR] = v & 0x77;
      if (TimerMode(2) != oc) timers_[2].next = 0;
      if (TimerMode(3) != od) timers_[3].next = 0;
      TimerReload(2);
      TimerReload(3);
      break;
    }
    case TADR:
    case TADR + 1:
    case TADR + 2:
    case TADR + 3: {  // a running timer takes the new count at its next underflow; a stopped one loads it
      int t = reg - TADR;
      mfp_[reg] = v;
      Timer& tm = timers_[t];
      int mode = TimerMode(t);
      if (mode == 0) tm.count = v ? v : 256;
      if (tm.next) tm.period = kPrescale[mode & 7] * (v ? v : 256) * kCyclesPerMfp;
      break;
    }
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
  if (joy_events_ && !(port == 0 && mouse_on_)) IkbdSend({uint8_t(0xFE + port), state});  // port 0: the mouse
}

void Machine::KeyEvent(uint8_t scancode, bool down) { IkbdSend({uint8_t(down ? scancode : scancode | 0x80)}); }

void Machine::MouseInput(int dx, int dy, uint8_t buttons) {
  gem_mouse_x_ = std::clamp(gem_mouse_x_ + dx, 0, kScreenW - 1);
  gem_mouse_y_ = std::clamp(gem_mouse_y_ + dy, 0, kScreenH - 1);
  if (!mouse_on_) {
    mouse_buttons_ = buttons;
    return;
  }
  mouse_dx_ += dx, mouse_dy_ += dy;
  mouse_buttons_ = buttons;
  SendMouse();
}

// Relative mouse packets: $F8 | buttons, dx, dy (each -128..127), once a threshold is reached or the buttons
// change. Not too many queued: the ACIA moves ~780 bytes a second.
void Machine::SendMouse() {
  while (ikbd_out_.size() < 30 &&(std::abs(mouse_dx_) >= mouse_thr_x_ || std::abs(mouse_dy_) >= mouse_thr_y_ ||
                                   mouse_buttons_ != mouse_sent_buttons_)) {
    // Small steps, as a real mouse sends at threshold 1: Defender 2's handler checks for a packet header ($F8-$FB)
    // before it checks whether it's inside a packet, so a delta of -5..-8 would be read as a new packet.
    int dx = std::clamp(mouse_dx_, -4, 4), dy = std::clamp(mouse_dy_, -4, 4);
    mouse_dx_ -= dx, mouse_dy_ -= dy;
    mouse_sent_buttons_ = mouse_buttons_;
    if (trace_tos) LOG("mouse packet: buttons %d dx %d dy %d", mouse_buttons_ & 3, dx, dy);
    IkbdSend({uint8_t(0xF8 | (mouse_buttons_ & 3)), uint8_t(int8_t(dx)), uint8_t(int8_t(mouse_y_up_ ? -dy : dy))});
  }
}

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
    case 0x80:  // reset: relative mouse, joystick events
      if (ikbd_cmd_[1] == 0x01) {
        joy_events_ = true;
        mouse_on_ = true, mouse_y_up_ = false, mouse_thr_x_ = mouse_thr_y_ = 1;
        IkbdSend({0xF0});
      }
      break;
    case 0x08:  // relative mouse
      mouse_on_ = true;
      break;
    case 0x0B:  // mouse threshold
      mouse_thr_x_ = std::max(1, int(ikbd_cmd_[1])), mouse_thr_y_ = std::max(1, int(ikbd_cmd_[2]));
      break;
    case 0x0F: mouse_y_up_ = true; break;
    case 0x10: mouse_y_up_ = false; break;
    case 0x12:  // disable mouse
      mouse_on_ = false;
      break;
    case 0x14:  // joystick event reporting (both ports: no mouse)
      joy_events_ = true;
      mouse_on_ = false;
      break;
    case 0x15:
      joy_events_ = false;
      mouse_on_ = false;
      break;
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

namespace {
float Luma(uint16_t c) { return (0.30f * float((c >> 8) & 7) + 0.59f * float((c >> 4) & 7) + 0.11f * float(c & 7)) / 7; }
}  // namespace

void Machine::SetCalmBackground(bool on, int hold, float cap, float release) {
  calm_release_ = release;
  calm_hold_ = std::clamp(hold, 1, 16);
  calm_cap_ = cap;
  if (on && !calm_) {
    for (uint16_t& h : calm_hist_) h = palette_[0];
    for (int k = 0; k < 3; ++k) calm_rgb_[k] = float((palette_[0] >> (8 - 4 * k)) & 7) / 7;
  }
  calm_ = on;
}

void Machine::CalmFrame() {
  calm_hist_[calm_at_++ % 16] = palette_[0];
  uint16_t peak = palette_[0];
  for (int i = 0; i < calm_hold_; ++i) {
    uint16_t h = calm_hist_[(calm_at_ - 1 - i + 32) % 16];
    if (Luma(h) > Luma(peak)) peak = h;
  }
  float target[3], shown_luma = 0.30f * calm_rgb_[0] + 0.59f * calm_rgb_[1] + 0.11f * calm_rgb_[2];
  for (int k = 0; k < 3; ++k) target[k] = float((peak >> (8 - 4 * k)) & 7) / 7;
  float target_luma = Luma(peak);
  // A white flash becomes a dim grey glow, a red one dim red (saturated red flashes are the worst kind).
  const float kMaxChannel = calm_cap_;
  float top = std::max({target[0], target[1], target[2]});
  if (top > kMaxChannel)
    for (float& t : target) t *= kMaxChannel / top;
  target_luma = 0.30f * target[0] + 0.59f * target[1] + 0.11f * target[2];
  float rate = target_luma > shown_luma ? 0.12f : calm_release_;  // up in ~1/4 s, down in ~1/2 s (strobes)
  for (int k = 0; k < 3; ++k) calm_rgb_[k] += (target[k] - calm_rgb_[k]) * rate;
}

void Machine::RenderLine(int y) {
  uint32_t pal[16];
  for (int i = 0; i < 16; ++i) {
    unsigned c = palette_[i];
    auto ch = [](unsigned v) { return (v << 5) | (v << 2) | (v >> 1); };  // 3 bits -> 8
    pal[i] = 0xFF000000u | ch((c >> 8) & 7) << 16 | ch((c >> 4) & 7) << 8 | ch(c & 7);
  }
  if (calm_) {
    if (y == 0) CalmFrame();
    auto b = [](float v) { return uint32_t(std::clamp(v, 0.0f, 1.0f) * 255 + 0.5f); };
    pal[0] = 0xFF000000u | b(calm_rgb_[0]) << 16 | b(calm_rgb_[1]) << 8 | b(calm_rgb_[2]);
  }
  uint32_t* out = &frame_[y * kScreenW];
  for (int g = 0; g < 20; ++g) {
    uint32_t a = (vbase_latched_ + uint32_t(y * 160 + g * 8)) % kRamSize;
    uint16_t p0 = Get16(&ram_[a]), p1 = Get16(&ram_[a + 2]), p2 = Get16(&ram_[a + 4]), p3 = Get16(&ram_[a + 6]);
    for (int x = 0; x < 16; ++x) {
      int s = 15 - x;
      int idx = ((p0 >> s) & 1) | ((p1 >> s) & 1) << 1 | ((p2 >> s) & 1) << 2 | ((p3 >> s) & 1) << 3;
      out[g * 16 + x] = pal[idx];
    }
  }
}

// ---- TOS's IKBD handler ----

// Called (via Kbdvbase()->ikbdsys) for each byte from the keyboard processor: packets go to the Kbdvbase
// vectors (mouse, joystick, status, clock) through kRomIkbdCall, key presses to the console buffer.
void Machine::IkbdSysHle() {
  uint8_t b = uint8_t(Read8(0xFFFC02));
  cpu::Set(cpu::PC, kRomBase + kRomRts);
  if (!packet_need_) {
    if (b < 0xF6) {
      ConsoleKey(b);
      return;
    }
    static const uint8_t kLen[10] = {8, 6, 3, 3, 3, 3, 7, 3, 2, 2};  // $F6..$FF
    packet_ = {b};
    packet_need_ = kLen[b - 0xF6];
    return;
  }
  packet_.push_back(b);
  if (packet_.size() < packet_need_) return;
  packet_need_ = 0;
  uint8_t h = packet_[0];
  uint32_t vec_off = h == 0xF6 ? 0x0C : h <= 0xFB ? 0x10 : h == 0xFC ? 0x14 : 0x18;
  std::vector<uint8_t> pkt = packet_;
  if (h >= 0xFE) {  // TOS passes joystick packets as [header, joystick 0, joystick 1]
    packet_joy_[h - 0xFE] = packet_[1];
    pkt = {h, packet_joy_[0], packet_joy_[1]};
  }
  uint32_t vec = Get32(&ram_[kbdvbase_ + vec_off]);
  if (vec == kRomBase + kRomRts) return;
  std::memcpy(&ram_[kIkbdPacket], pkt.data(), pkt.size());
  Put32(&ram_[kIkbdTarget], vec);
  cpu::Set(cpu::A0, kIkbdPacket);
  cpu::Set(cpu::PC, kRomBase + kRomIkbdCall);
}

void Machine::TypeAhead(const std::string& text) {
  for (char c : text) {
    uint8_t code = 0;
    static const char kKeys[] = "\0\x1b" "1234567890-=\b\tqwertyuiop[]\r\0asdfghjkl;'`\0#zxcvbnm,./";
    for (uint8_t i = 1; i < sizeof(kKeys) - 1; ++i)
      if (kKeys[i] == std::tolower(uint8_t(c))) code = i;
    console_keys_.push_back(uint32_t(code) << 16 | uint8_t(c));
  }
}

void Machine::ConsoleKey(uint8_t sc) {
  bool up = sc & 0x80;
  uint8_t code = sc & 0x7F;
  static const uint8_t kModifier[4] = {0x36, 0x2A, 0x1D, 0x38};  // Kbshift bit order
  for (int i = 0; i < 4; ++i) {
    if (code != kModifier[i]) continue;
    modifiers_ = uint8_t(up ? modifiers_ & ~(1u << i) : modifiers_ | (1u << i));
    return;
  }
  if (up || console_keys_.size() >= 32) return;
  // ASCII for the main block (UK layout, enough for names and menus).
  static const char kLow[] = "\0\x1b" "1234567890-=\b\tqwertyuiop[]\r\0asdfghjkl;'`\0#zxcvbnm,./\0\0\0 ";
  static const char kHigh[] = "\0\x1b" "!\"#$%^&*()_+\b\tQWERTYUIOP{}\r\0ASDFGHJKL:@~\0~ZXCVBNM<>?\0\0\0 ";
  char a = code < sizeof(kLow) - 1 ? (modifiers_ & 3 ? kHigh : kLow)[code] : 0;
  console_keys_.push_back(uint32_t(code) << 16 | uint8_t(a));
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

std::string Machine::FileName(uint32_t a) {
  std::string name = Upper(ReadString(a & 0xFFFFFF));
  size_t slash = name.find_last_of("\\:");
  if (slash != std::string::npos) name = name.substr(slash + 1);
  return name;
}

bool Machine::IllegalOpcode(int) {
  uint32_t pc = cpu::Get(cpu::PPC) & 0xFFFFFF;
  if (auto hook = hooks_.find(pc); hook != hooks_.end()) {
    hook->second(*this);
    return true;
  }
  if (pc < kRomBase || pc >= kRomBase + kRomSize) return false;
  uint32_t off = pc - kRomBase;
  if (off == kRomIkbdDefault) {
    IkbdSysHle();
    return true;
  }
  if (off == kRomPterm) {
    if (EndProcess(cpu::Get(cpu::D0))) return true;
    LOG("program returned (rts from its entry)");
    ended_ = true;
    cpu::EndSlice();
    return true;
  }
  if (off == kRomLineA) {  // the frame: SR, then the address of the $Axxx instruction
    uint32_t ssp = cpu::Get(cpu::A7);
    uint16_t sr = Get16(&ram_[ssp]);
    uint32_t at = Get32(&ram_[ssp + 2]) & 0xFFFFFF;
    LineA(Read16(at) & 0xFFF);
    cpu::Set(cpu::A7, ssp + 6);
    cpu::Set(cpu::SR, sr);
    cpu::Set(cpu::PC, at + 2);
    return true;
  }
  if (off == kRomTrap2) {  // GEM: d0 = $C8 AES / $73 VDI, d1 = the parameter block; RTE follows the ILLEGAL
    Gem(cpu::Get(cpu::D0), cpu::Get(cpu::Reg(cpu::D0 + 1)) & 0xFFFFFF);
    cpu::Set(cpu::PC, pc + 2);
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
  if (off == kRomTrap1 && (fn == 0x00 || fn == 0x4C || fn == 0x31)) {  // Pterm0, Pterm(code), Ptermres
    uint32_t code = fn == 0x00 ? 0 : uint32_t(int32_t(int16_t(Get16(&ram_[args + (fn == 0x31 ? 6 : 2)]))));
    if (EndProcess(code)) return true;
    LOG("program ended (Pterm)");
    ended_ = true;
    cpu::EndSlice();
    return true;
  }
  trap_wait_ = false;
  bool handled;
  if (off == kRomTrap1 && fn == 0x4B) {
    bool started = false;
    handled = Pexec(args, &d0, &started);
    if (started) return true;  // the child runs now; its Pterm returns from this trap
  } else {
    handled = off == kRomTrap1 ? Gemdos(args, &d0) : off == kRomTrap13 ? Bios(args, &d0) : Xbios(args, &d0);
  }
  if (trap_wait_) {  // nothing to read yet: run the trap again
    cpu::Set(cpu::A7, ssp + 6);
    cpu::Set(cpu::SR, sr);
    cpu::Set(cpu::PC, ret - 2);
    return true;
  }
  if (!handled) {
    LOG("args: %04X %04X %04X %04X %04X %04X %04X", Get16(&ram_[args + 2]), Get16(&ram_[args + 4]),
        Get16(&ram_[args + 6]), Get16(&ram_[args + 8]), Get16(&ram_[args + 10]), Get16(&ram_[args + 12]),
        Get16(&ram_[args + 14]));
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
    case 0x06:  // Crawio(c): $FF = read a key if there is one (else 0), otherwise write c
      if ((Get16(p + 2) & 0xFF) == 0xFF) {
        if (!console_keys_.empty()) *d0 = console_keys_.front(), console_keys_.pop_front();
        else *d0 = 0;
      }
      return true;
    case 0x01:  // Cconin
    case 0x07:  // Crawcin
    case 0x08:  // Cnecin: these wait for a key
      if (!console_keys_.empty()) *d0 = console_keys_.front(), console_keys_.pop_front();
      else trap_wait_ = true;
      return true;
    case 0x02:  // Cconout
    case 0x03:  // Cauxin
    case 0x04:  // Cauxout
    case 0x05:  // Cprnout
      return true;
    case 0x0A: {  // Cconrs(buf): a line from the keyboard - an empty one
      uint32_t buf = Get32(p + 2) & 0xFFFFFF;
      if (buf + 1 < kRamSize) ram_[buf + 1] = 0;
      return true;
    }
    case 0x10: *d0 = uint32_t(-1); return true;  // Cconos
    case 0x11:                                   // Cprnos
    case 0x13: *d0 = uint32_t(-1); return true;  // Cauxos: ready (output goes nowhere)
    case 0x12: *d0 = 0; return true;             // Cauxis: nothing from the serial port
    case 0x1A: dta_ = Get32(p + 2); return true;  // Fsetdta
    case 0x2F: *d0 = dta_ ? dta_ : cur_bp_ + 0x80; return true;  // Fgetdta
    case 0x2A: *d0 = (2026 - 1980) << 9 | 10 << 5 | 31; return true;  // Tgetdate
    case 0x2C: *d0 = 12 << 11; return true;  // Tgettime
    case 0x2B:
    case 0x2D: return true;  // Tsetdate, Tsettime
    case 0x30: *d0 = 0x1300; return true;  // Sversion: GEMDOS 0.19 (TOS 1.04)
    case 0x36: {  // Dfree(buf, drive): an empty 720 KB disk
      uint32_t buf = Get32(p + 2) & 0xFFFFFF;
      Put32(&ram_[buf], 700), Put32(&ram_[buf + 4], 711), Put32(&ram_[buf + 8], 512), Put32(&ram_[buf + 12], 2);
      return true;
    }
    case 0x3B: return true;  // Dsetpath
    case 0x47: {             // Dgetpath(buf, drive): the root
      ram_[Get32(p + 2) & 0xFFFFFF] = 0;
      return true;
    }
    case 0x41: {  // Fdelete
      std::string name = FileName(Get32(p + 2));
      *d0 = files_.erase(name) ? 0 : uint32_t(-33);
      return true;
    }
    case 0x43: {  // Fattrib(name, set, attr)
      *d0 = files_.count(FileName(Get32(p + 2))) ? 0 : uint32_t(-33);
      return true;
    }
    case 0x4E:    // Fsfirst(name, attr)
    case 0x4F: {  // Fsnext
      if (fn == 0x4E) {
        std::string pattern = FileName(Get32(p + 2));
        search_.clear();
        auto match = [](const std::string& pat, const std::string& s) {  // * and ? as in TOS (8.3 parts)
          auto part = [](const std::string& x, bool ext) {
            size_t dot = x.find('.');
            return ext ? (dot == std::string::npos ? std::string() : x.substr(dot + 1)) : x.substr(0, dot);
          };
          for (int e = 0; e < 2; ++e) {
            std::string pp = part(pat, e), ss = part(s, e);
            size_t i = 0;
            for (; i < pp.size(); ++i) {
              if (pp[i] == '*') break;
              if (i >= ss.size() || (pp[i] != '?' && pp[i] != ss[i])) return false;
            }
            if (i == pp.size() && ss.size() > pp.size()) return false;
          }
          return true;
        };
        for (const auto& [n, data] : files_)
          if (match(pattern, n)) search_.push_back(n);
        if (trace_tos) LOG("gemdos Fsfirst(%s): %zu found", pattern.c_str(), search_.size());
      }
      if (search_.empty()) return *d0 = uint32_t(fn == 0x4E ? -33 : -49), true;  // EFILNF, ENMFIL
      std::string n = search_.front();
      search_.pop_front();
      uint32_t dta = (dta_ ? dta_ : cur_bp_ + 0x80) & 0xFFFFFF;
      std::fill(&ram_[dta], &ram_[dta + 44], 0);
      ram_[dta + 21] = 0;  // attributes
      Put16(&ram_[dta + 22], 0), Put16(&ram_[dta + 24], 0);  // time, date
      uint32_t size = uint32_t(files_[n].size());
      Put32(&ram_[dta + 26], size);
      std::memcpy(&ram_[dta + 30], n.c_str(), std::min<size_t>(n.size(), 13));
      *d0 = 0;
      return true;
    }
    case 0x48: {  // Malloc(size): -1 = the largest free block; taken from the top of memory
      int32_t size = int32_t(Get32(p + 2));
      uint32_t low = (cur_end_ + 0xFF) & ~0xFFu;
      uint32_t free = malloc_top_ > low ? malloc_top_ - low : 0;
      if (size < 0) return *d0 = free, true;
      uint32_t n = (uint32_t(size) + 15) & ~15u;
      if (n > free) {
        LOG("Malloc(%d): only %u free", size, free);
        return *d0 = 0, true;
      }
      malloc_top_ -= n;
      mallocs_[malloc_top_] = n;
      if (trace_tos) LOG("gemdos Malloc(%d) = $%X", size, malloc_top_);
      *d0 = malloc_top_;
      return true;
    }
    case 0x49: {  // Mfree(block): the space comes back if it's the lowest block
      uint32_t a = Get32(p + 2);
      auto it = mallocs_.find(a);
      if (it == mallocs_.end()) return *d0 = uint32_t(-40), true;
      mallocs_.erase(it);
      malloc_top_ = mallocs_.empty() ? kHitpa : mallocs_.begin()->first;
      *d0 = 0;
      return true;
    }
    case 0x0B:  // Cconis
      *d0 = console_keys_.empty() ? 0 : uint32_t(-1);
      return true;
    case 0x09:  // Cconws(string)
      LOG("gemdos Cconws: %s", ReadString(Get32(p + 2)).c_str());
      *d0 = 0;
      return true;
    case 0x0E: *d0 = 1; return true;  // Dsetdrv: drive bitmap (A:)
    case 0x19: *d0 = 0; return true;  // Dgetdrv
    case 0x4A: {  // Mshrink(0, block, size): the running program gives back the end of its memory
      uint32_t block = Get32(p + 4), size = Get32(p + 8);
      if (block == cur_bp_) cur_end_ = block + size;
      if (trace_tos) LOG("gemdos Mshrink($%X, %u)", block, size);
      *d0 = 0;
      return true;
    }
    case 0x3C:    // Fcreate(name, attr)
    case 0x3D: {  // Fopen(name, mode)
      std::string name = FileName(Get32(p + 2));
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

// GEM, as much as a full-screen program uses it at startup: AES (d0 $C8) and VDI (d0 $73). Results are what
// TOS gives in ST low resolution. Calls we don't know are logged once and return "ok".
void Machine::Gem(uint32_t d0, uint32_t pb) {
  auto w16 = [&](uint32_t a, int i, int v) { Put16(&ram_[(a & 0xFFFFFF) + uint32_t(i) * 2], uint16_t(v)); };
  auto r16 = [&](uint32_t a, int i) { return int(int16_t(Get16(&ram_[(a & 0xFFFFFF) + uint32_t(i) * 2]))); };
  auto first = [&](int code) { return gem_seen_.insert(code).second; };
  if ((d0 & 0xFFFF) == 0xC8) {
    uint32_t control = Get32(&ram_[pb]), global = Get32(&ram_[pb + 4]), int_in = Get32(&ram_[pb + 8]);
    uint32_t int_out = Get32(&ram_[pb + 12]), addr_in = Get32(&ram_[pb + 16]);
    int op = r16(control, 0);
    if (trace_tos || first(0x10000 + op)) LOG("AES %d", op);
    w16(int_out, 0, 1);
    switch (op) {
      case 10:  // appl_init: AES 1.40, one application, id 0
        w16(int_out, 0, 0);
        w16(global, 0, 0x0140), w16(global, 1, 1), w16(global, 2, 0);
        break;
      case 77:  // graf_handle: VDI handle, character cell 8x8, box 9x9 (low resolution)
        w16(int_out, 0, 1), w16(int_out, 1, 8), w16(int_out, 2, 8), w16(int_out, 3, 9), w16(int_out, 4, 9);
        break;
      case 79:  // graf_mkstate: mouse x, y, buttons, shift keys
        w16(int_out, 1, gem_mouse_x_), w16(int_out, 2, gem_mouse_y_);
        w16(int_out, 3, (mouse_buttons_ & 2 ? 1 : 0) | (mouse_buttons_ & 1 ? 2 : 0)), w16(int_out, 4, modifiers_);
        break;
      case 52:  // form_alert(default, text)
        LOG("AES form_alert: %s", ReadString(Get32(&ram_[addr_in & 0xFFFFFF])).c_str());
        w16(int_out, 0, r16(int_in, 0) > 0 ? r16(int_in, 0) : 1);
        break;
      case 90:  // fsel_input: cancelled (no GEM file selector here)
        w16(int_out, 0, 1), w16(int_out, 1, 0);
        break;
      default:
        break;
    }
    return;
  }
  if ((d0 & 0xFFFF) == 0x73) {
    uint32_t contrl = Get32(&ram_[pb]), intout = Get32(&ram_[pb + 12]), ptsout = Get32(&ram_[pb + 16]);
    int op = r16(contrl, 0);
    if (trace_tos || first(op)) LOG("VDI %d", op);
    w16(contrl, 2, 0), w16(contrl, 4, 0);
    switch (op) {
      case 1:
      case 100: {  // v_opnwk / v_opnvwk: low resolution workstation
        w16(contrl, 6, 1);
        static const int16_t kOut[45] = {319, 199, 0, 278, 278, 3, 7, 6, 8, 8, 24, 12, 16, 1, 0, 2,
                                         0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                         0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        for (int i = 0; i < 45; ++i) w16(intout, i, kOut[i]);
        w16(intout, 13, 16), w16(intout, 39, 512);  // colours, palette size
        static const int16_t kPts[12] = {0, 7, 0, 1, 15, 1, 0, 0, 0, 0, 0, 0};
        for (int i = 0; i < 12; ++i) w16(ptsout, i, kPts[i]);
        w16(contrl, 2, 6), w16(contrl, 4, 45);
        break;
      }
      case 124:  // vq_mouse: buttons, x, y
        w16(intout, 0, (mouse_buttons_ & 2 ? 1 : 0) | (mouse_buttons_ & 1 ? 2 : 0));
        w16(ptsout, 0, gem_mouse_x_), w16(ptsout, 1, gem_mouse_y_);
        w16(contrl, 2, 1), w16(contrl, 4, 1);
        break;
      default:  // v_clsvwk, v_show_c, v_hide_c, ...: nothing to do
        break;
    }
    return;
  }
  if (first(0x20000 + int(d0 & 0xFFFF))) LOG("GEM trap with d0 = $%X", d0);
}

// ---- Line-A (low resolution, 4 planes, drawing into the logical screen) ----

int Machine::GetPixel(int x, int y) {
  uint32_t a = (logbase_ + uint32_t(y) * 160 + uint32_t(x >> 4) * 8) % kRamSize;
  int bit = 15 - (x & 15), c = 0;
  for (int p = 0; p < 4; ++p) c |= ((Get16(&ram_[a + p * 2]) >> bit) & 1) << p;
  return c;
}

void Machine::SetPixel(int x, int y, int c) {
  if (x < 0 || y < 0 || x >= kScreenW || y >= kScreenH) return;
  uint32_t a = (logbase_ + uint32_t(y) * 160 + uint32_t(x >> 4) * 8) % kRamSize;
  uint16_t bit = uint16_t(0x8000 >> (x & 15));
  for (int p = 0; p < 4; ++p) {
    uint16_t w = Get16(&ram_[a + p * 2]);
    Put16(&ram_[a + p * 2], (c >> p) & 1 ? (w | bit) : (w & ~bit));
  }
}

// One pixel by the write mode: 0 replace, 1 transparent, 2 xor, 3 reverse transparent; `on` = the pattern bit.
void Machine::PlotMode(int x, int y, bool on, int colour, int mode) {
  if (x < 0 || y < 0 || x >= kScreenW || y >= kScreenH) return;
  switch (mode & 3) {
    case 0: SetPixel(x, y, on ? colour : 0); break;
    case 1: if (on) SetPixel(x, y, colour); break;
    case 2: if (on) SetPixel(x, y, GetPixel(x, y) ^ 15); break;
    case 3: if (!on) SetPixel(x, y, colour); break;
  }
}

void Machine::LineA(int fn) {
  uint8_t* v = &ram_[kLineA];
  auto var = [&](int off) { return int(int16_t(Get16(v + off))); };
  int colour = (var(0x18) & 1) | (var(0x1A) & 1) << 1 | (var(0x1C) & 1) << 2 | (var(0x1E) & 1) << 3;
  int mode = var(0x24);
  if (gem_seen_.insert(0x30000 + fn).second) LOG("Line-A $A%03X", fn);
  switch (fn) {
    case 0x0:  // init: a0 = d0 = the variables, a1 = font headers, a2 = the routine table
      cpu::Set(cpu::D0, kLineA);
      cpu::Set(cpu::A0, kLineA);
      cpu::Set(cpu::Reg(cpu::A0 + 1), kRomBase + kRomFont + 0x80);
      cpu::Set(cpu::Reg(cpu::A0 + 2), kRomBase + kRomFont + 0x90);
      break;
    case 0x3: {  // arbitrary line X1,Y1 - X2,Y2 with the line mask
      int x0 = var(0x26), y0 = var(0x28), x1 = var(0x2A), y1 = var(0x2C);
      uint16_t mask = uint16_t(var(0x22));
      int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
      int err = dx + dy;
      for (int guard = 0; guard < 4096; ++guard) {
        bool last = x0 == x1 && y0 == y1;
        if (!last || var(0x20)) PlotMode(x0, y0, mask & 0x8000, colour, mode);
        mask = uint16_t(mask << 1 | mask >> 15);
        if (last) break;
        int e2 = 2 * err;
        if (e2 >= dy) err += dy, x0 += sx;
        if (e2 <= dx) err += dx, y0 += sy;
      }
      Put16(v + 0x22, mask);
      break;
    }
    case 0x6: {  // horizontal line X1 - X2 at Y1 with the fill pattern (PATPTR, PATMSK, MFILL)
      int x0 = var(0x26), y = var(0x28), x1 = var(0x2A);
      if (x0 > x1) std::swap(x0, x1);
      uint32_t pat = Get32(v + 0x2E) & 0xFFFFFF;
      int patmsk = var(0x32), mfill = var(0x34);
      if (var(0x36)) {  // clipping
        x0 = std::max(x0, var(0x38)), x1 = std::min(x1, var(0x3C));
        if (y < var(0x3A) || y > var(0x3E)) break;
      }
      for (int x = x0; x <= x1; ++x) {
        if (!pat) {
          PlotMode(x, y, true, colour, mode);
          continue;
        }
        int row = y & patmsk, bit = 15 - (x & 15);
        if (mfill) {  // a multi-plane pattern: the colour comes from the pattern
          int c = 0;
          for (int p = 0; p < 4; ++p) c |= ((Get16(&ram_[(pat + uint32_t((p * (patmsk + 1) + row) * 2)) & 0xFFFFFF]) >> bit) & 1) << p;
          if (mode == 0 || c) SetPixel(x, y, c);
        } else {
          PlotMode(x, y, (Get16(&ram_[(pat + uint32_t(row * 2)) & 0xFFFFFF]) >> bit) & 1, colour, mode);
        }
      }
      break;
    }
    case 0xC: {  // undraw sprite: a2 = the save buffer (our layout: valid, x, y, 16 rows of 32 pixels' colours)
      uint32_t buf = cpu::Get(cpu::Reg(cpu::A0 + 2)) & 0xFFFFFF;
      if (!Get16(&ram_[buf])) break;
      int x = int(int16_t(Get16(&ram_[buf + 2]))), y = int(int16_t(Get16(&ram_[buf + 4])));
      for (int r = 0; r < 16; ++r)
        for (int c = 0; c < 16; c += 2) {
          uint8_t b = ram_[buf + 6 + uint32_t(r * 8 + c / 2)];
          SetPixel(x + c, y + r, b >> 4), SetPixel(x + c + 1, y + r, b & 15);
        }
      Put16(&ram_[buf], 0);
      break;
    }
    case 0xD: {  // draw sprite at d0, d1: a0 = definition (hot x, hot y, format, bg colour, fg colour, 16 x
                 // (bg mask, fg mask)), a2 = the save buffer
      uint32_t def = cpu::Get(cpu::A0) & 0xFFFFFF, buf = cpu::Get(cpu::Reg(cpu::A0 + 2)) & 0xFFFFFF;
      int x = int(int16_t(cpu::Get(cpu::D0))) - int(int16_t(Get16(&ram_[def]))),
          y = int(int16_t(cpu::Get(cpu::Reg(cpu::D0 + 1)))) - int(int16_t(Get16(&ram_[def + 2])));
      int fmt = int(int16_t(Get16(&ram_[def + 4]))), bg = Get16(&ram_[def + 6]) & 15, fg = Get16(&ram_[def + 8]) & 15;
      Put16(&ram_[buf], 1), Put16(&ram_[buf + 2], uint16_t(x)), Put16(&ram_[buf + 4], uint16_t(y));
      for (int r = 0; r < 16; ++r) {
        for (int c = 0; c < 16; c += 2) {
          auto at = [&](int px) { return x + px >= 0 && x + px < kScreenW && y + r >= 0 && y + r < kScreenH ? GetPixel(x + px, y + r) : 0; };
          ram_[buf + 6 + uint32_t(r * 8 + c / 2)] = uint8_t(at(c) << 4 | at(c + 1));
        }
        uint16_t bm = Get16(&ram_[def + 10 + uint32_t(r * 4)]), fm = Get16(&ram_[def + 12 + uint32_t(r * 4)]);
        for (int c = 0; c < 16; ++c) {
          bool b = (bm >> (15 - c)) & 1, f = (fm >> (15 - c)) & 1;
          if (fmt == -1) {  // xor
            if (b) SetPixel(x + c, y + r, bg);
            if (f && x + c >= 0 && x + c < kScreenW && y + r >= 0 && y + r < kScreenH)
              SetPixel(x + c, y + r, GetPixel(x + c, y + r) ^ fg);
          } else {
            if (b) SetPixel(x + c, y + r, bg);
            if (f) SetPixel(x + c, y + r, fg);
          }
        }
      }
      break;
    }
    default:
      LOG("Line-A $A%03X not done", fn);
      break;
  }
}

bool Machine::Bios(uint32_t args, uint32_t* d0) {
  const uint8_t* p = &ram_[args];
  switch (Get16(p)) {
    case 1:  // Bconstat(dev): 2 = console
      *d0 = Get16(p + 2) == 2 && !console_keys_.empty() ? uint32_t(-1) : 0;
      return true;
    case 2:  // Bconin(dev): waits for a key (console); other devices: 0
      if (Get16(p + 2) != 2) *d0 = 0;
      else if (!console_keys_.empty()) *d0 = console_keys_.front(), console_keys_.pop_front();
      else trap_wait_ = true;
      return true;
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
    case 11: *d0 = modifiers_; return true;  // Kbshift
    default: return false;
  }
}

bool Machine::Xbios(uint32_t args, uint32_t* d0) {
  const uint8_t* p = &ram_[args];
  switch (Get16(p)) {
    case 0: {  // Initmous(type, params, vector): 0 off, 1 relative, (2 absolute, 4 keycode: not done)
      int type = Get16(p + 2);
      uint32_t params = Get32(p + 4) & 0xFFFFFF, vec = Get32(p + 8);
      if (type == 0) {
        IkbdCommand(0x12);
        return true;
      }
      Put32(&ram_[kbdvbase_ + 0x10], vec);
      if (type != 1) LOG("Initmous type %d: treated as relative", type);
      // params: y origin (1 = at the bottom: y grows upwards), buttons, x threshold, y threshold. (Gridrunner
      // passes 1 and subtracts dy from its pointer's screen y: so 1 = bottom.)
      IkbdCommand(ram_[params] ? 0x0F : 0x10);
      for (uint8_t b : {uint8_t(0x08), uint8_t(0x0B), ram_[params + 2], ram_[params + 3]}) IkbdCommand(b);
      if (trace_tos) LOG("Initmous: relative, threshold %d/%d, vector $%X", ram_[params + 2], ram_[params + 3], vec);
      return true;
    }
    case 13: {  // Mfpint(channel, vector)
      int ch = Get16(p + 2) & 15;
      Put32(&ram_[(0x40 + ch) * 4], Get32(p + 4));
      return true;
    }
    case 26:    // Jdisint(channel)
    case 27: {  // Jenabint(channel)
      int ch = Get16(p + 2) & 15;
      uint8_t bit = uint8_t(1u << (ch & 7));
      int ier = ch >= 8 ? IERA : IERB, imr = ch >= 8 ? IMRA : IMRB;
      if (Get16(p) == 27) MfpWrite(ier, mfp_[ier] | bit), MfpWrite(imr, mfp_[imr] | bit);
      else MfpWrite(ier, uint8_t(mfp_[ier] & ~bit)), MfpWrite(imr, uint8_t(mfp_[imr] & ~bit));
      return true;
    }
    case 31: {  // Xbtimer(timer, control, data, vector): 0 A, 1 B, 2 C, 3 D
      int t = Get16(p + 2) & 3, ctrl = Get16(p + 4) & 0xFF, data = Get16(p + 6) & 0xFF;
      int ch = kTimerChannel[t];
      Put32(&ram_[(0x40 + ch) * 4], Get32(p + 8));
      static const int kCtrl[4] = {TACR, TBCR, TCDCR, TCDCR};
      if (t < 2) MfpWrite(kCtrl[t], 0);
      MfpWrite(TADR + t, uint8_t(data));
      if (t == 2) MfpWrite(TCDCR, uint8_t((mfp_[TCDCR] & 0x07) | (ctrl & 7) << 4));
      else if (t == 3) MfpWrite(TCDCR, uint8_t((mfp_[TCDCR] & 0x70) | (ctrl & 7)));
      else MfpWrite(kCtrl[t], uint8_t(ctrl));
      uint8_t bit = uint8_t(1u << (ch & 7));
      int ier = ch >= 8 ? IERA : IERB, imr = ch >= 8 ? IMRA : IMRB;
      MfpWrite(ier, mfp_[ier] | bit), MfpWrite(imr, mfp_[imr] | bit);
      if (trace_tos) LOG("Xbtimer: timer %c control %d data %d vector $%X", 'A' + t, ctrl, data, Get32(p + 8));
      return true;
    }
    case 2:  // Physbase
      *d0 = uint32_t(vbase_hi_) << 16 | uint32_t(vbase_mid_) << 8;
      return true;
    case 3: *d0 = logbase_; return true;  // Logbase
    case 15: *d0 = 0; return true;        // Rsconf (serial port settings)
    case 4: *d0 = res_; return true;  // Getrez
    case 5: {                         // Setscreen(log, phys, rez)
      uint32_t log = Get32(p + 2), phys = Get32(p + 6);
      if (log != 0xFFFFFFFF) logbase_ = log & 0xFFFFFF, Put32(&ram_[0x44E], logbase_);
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
  if (save_dir_.empty() || sandbox) return;
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
