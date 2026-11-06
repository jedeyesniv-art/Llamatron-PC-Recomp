// Defender 2: the remaster (remaster.h; REMASTER.md).
#pragma once

#include "machine.h"

namespace remaster_d2 {

void Start();
void Update(st::Machine* m);  // every frame, before it runs
// Play, or an attract screen whose animation is in the VBL (the title, player select, the high scores):
// drawn every VBL (d2_50fps), on the faster CPU, captured.
bool DrawsEveryVbl(const st::Machine& m);
// One of those attract screens (not play: its faster CPU is the registry's rule, which checks the VBL routine too).
bool Attract(const st::Machine& m);

}  // namespace remaster_d2
