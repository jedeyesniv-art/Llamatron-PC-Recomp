// Andes Attack's remaster capture (remaster.h): hooks on its draw routines, installed once play starts.
#pragma once

#include "machine.h"

namespace remaster_andes {

void Start();                 // a new run of the game
void Update(st::Machine* m);  // every frame, from stgames' Andes update

}  // namespace remaster_andes
