// The 68000 interpreter backend: Musashi (third_party/musashi, configured by src/musashi/m68kconf.h).
#include "cpu.h"

#include "machine.h"

extern "C" {
#include "m68k.h"
}

namespace {
st::Machine* g_machine = nullptr;
bool g_in_execute = false;
}  // namespace

extern "C" {
unsigned int m68k_read_memory_8(unsigned int a) { return g_machine->Read8(a); }
unsigned int m68k_read_memory_16(unsigned int a) { return g_machine->Read16(a); }
unsigned int m68k_read_memory_32(unsigned int a) { return g_machine->Read16(a) << 16 | g_machine->Read16(a + 2); }
void m68k_write_memory_8(unsigned int a, unsigned int v) { g_machine->Write8(a, uint8_t(v)); }
void m68k_write_memory_16(unsigned int a, unsigned int v) { g_machine->Write16(a, uint16_t(v)); }
void m68k_write_memory_32(unsigned int a, unsigned int v) {
  g_machine->Write16(a, uint16_t(v >> 16));
  g_machine->Write16(a + 2, uint16_t(v));
}
// The disassembler must not touch I/O.
unsigned int m68k_read_disassembler_16(unsigned int a) {
  a &= 0xFFFFFF;
  return a + 1 < st::kRamSize ? g_machine->Ram()[a] << 8 | g_machine->Ram()[a + 1] : 0;
}
unsigned int m68k_read_disassembler_32(unsigned int a) {
  return m68k_read_disassembler_16(a) << 16 | m68k_read_disassembler_16(a + 2);
}
int st_int_ack(int level) { return g_machine->InterruptAck(level); }
int st_illegal(int opcode) { return g_machine->IllegalOpcode(opcode) ? 1 : 0; }
}

namespace cpu {

void Init(st::Machine* machine) {
  g_machine = machine;
  m68k_init();
  m68k_set_cpu_type(M68K_CPU_TYPE_68000);
  m68k_pulse_reset();
}

int Execute(int cycles) {
  g_in_execute = true;
  int n = m68k_execute(cycles);
  g_in_execute = false;
  return n;
}

int CyclesRun() { return g_in_execute ? m68k_cycles_run() : 0; }
void EndSlice() { m68k_end_timeslice(); }
void SetIrq(int level) { m68k_set_irq(level); }

static m68k_register_t Map(Reg r) {
  if (r < A0) return m68k_register_t(int(M68K_REG_D0) + int(r));
  if (r <= A7) return m68k_register_t(int(M68K_REG_A0) + int(r - A0));
  switch (r) {
    case PC: return M68K_REG_PC;
    case SR: return M68K_REG_SR;
    case USP: return M68K_REG_USP;
    case SSP: return M68K_REG_ISP;
    default: return M68K_REG_PPC;
  }
}

uint32_t Get(Reg r) { return m68k_get_reg(nullptr, Map(r)); }

void Set(Reg r, uint32_t v) {
  if (r != SR) return m68k_set_reg(Map(r), v);
  // Musashi's SR setter doesn't swap the stack pointers on a mode change: do it here.
  bool was_super = Get(SR) & 0x2000, super = v & 0x2000;
  uint32_t usp = Get(USP), ssp = Get(SSP);
  m68k_set_reg(M68K_REG_SR, v);
  if (was_super == super) return;
  m68k_set_reg(M68K_REG_A7, super ? ssp : usp);
  m68k_set_reg(super ? M68K_REG_USP : M68K_REG_ISP, super ? usp : ssp);
}

const char* Disassemble(uint32_t pc, int* size) {
  static char buf[128];
  *size = int(m68k_disassemble(buf, pc, M68K_CPU_TYPE_68000));
  return buf;
}

}  // namespace cpu
