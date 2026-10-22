// The 68000 as the machine sees it. Implemented by cpu_musashi.cpp (interpreter, the reference) - the static
// recompilation will implement the same interface.
#pragma once

#include <cstdint>

namespace st {
class Machine;
}

namespace cpu {

enum Reg { D0 = 0, A0 = 8, A7 = 15, PC = 16, SR, USP, SSP, PPC };

void Init(st::Machine* machine);
int Execute(int cycles);  // runs at least `cycles` (unless ended early); returns cycles run
int CyclesRun();          // cycles run so far in the current Execute (0 outside)
void EndSlice();          // makes the current Execute return after this instruction
void SetIrq(int level);
uint32_t Get(Reg r);
void Set(Reg r, uint32_t v);
// If set, called before every instruction with its address and the interrupt mask (profiling; slow).
extern void (*g_profile)(uint32_t pc, int ipl);

// One-line disassembly (for traces).
const char* Disassemble(uint32_t pc, int* size);

}  // namespace cpu
