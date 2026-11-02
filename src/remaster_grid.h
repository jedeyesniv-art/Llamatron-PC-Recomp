// Gridrunner's remaster capture (remaster.h): hooks on its draw routines, installed once play starts.
#pragma once

#include "machine.h"

namespace remaster_grid {

void Start();                 // a new run of the game
void Update(st::Machine* m);  // every frame, before RunFrame

}  // namespace remaster_grid
