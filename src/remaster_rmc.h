// Revenge of the Mutant Camels: the remaster (remaster.h; REMASTER.md). All three builds (512K, 1 MB STE, TT).
#pragma once

#include <string>

#include "machine.h"

namespace remaster_rmc {

void Start(const std::string& program);  // the build's addresses
void Update(st::Machine* m);             // every frame, before it runs

}  // namespace remaster_rmc
