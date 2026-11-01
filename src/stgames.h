// Pad controls for the other Llamasoft ST games (PLAN-ST-GAMES.md): Revenge of the Mutant Camels and Andes
// Attack. Each reads the game's state from its RAM, polls pad 1 (pad.h) and feeds the ST's joystick, mouse and
// keyboard. Keys from the PC keyboard go to the ST as they are (plus the cursor keys as a joystick in RMC).
//
// Revenge of the Mutant Camels (REVENGE.TOS): its VBL calls a per-state hook ($42E18): $1118E play (and the
// attract demo), $10526 pause, $10294 title, $107FA the menus. Keys (its own table, $42D91 flags): P pause,
// O resume, X abort to the menu (play only).
//   left stick / d-pad = joystick 1, A / X / RT / Start = fire; Back = pause / resume; B paused = back to the
//   menu; B twice elsewhere = back to the disks.
// Andes Attack (ANDES.BOY): state $12EEE (1 play, 3 paused, 5 title / demo). Mouse: y = altitude ($2B682),
// left button = thrust, right button = reverse (facing $1C088: 0 right, 1 left); Left Shift = fire; Right
// Shift = pause, Space = resume.
//   left stick up / down = altitude; left / right = thrust that way (pushing against the facing reverses first);
//   A / RT = fire, LT = thrust, X = reverse, Y = smart bomb (Left Control); Back = pause / resume; B twice =
//   back to the disks. Away from play
//   A / RT is the left mouse button (start).
#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <string>

#include "machine.h"

namespace stgames {

// Defender 2 (DEFENDR2.PRG -> DEFENDR2.EXE): VBL $4AF1A dispatches on $4B086 (0 play, 2 title, 8 player
// select); pointer $62456 / $62458 (written directly for menu clicks; the title wants Space first). Control
// scheme $1BCC4 (0 mouse, 1 keyboard, 2 demo): forced to the keyboard one while a pad is used. Keys: Control
// up, Left Shift down, Return thrust, Alt reverse, # fire, Right Shift smart bomb, Caps Lock inviso
// (Stargate), Tab ship type (Defender II). No pause key: Back pauses the emulation.
//   title: d-pad picks a box (Defender, Defender II, Stargate / Trainer, Options, History), A opens it;
//   player select: d-pad left / right, A; other screens: left stick = pointer, A / RT left, X / LT right.
//   play: left stick up / down = climb / dive (pulsed: analog), left / right = thrust that way (turns round
//   first), A / RT fire, Y smart bomb, X reverse, LT thrust, LB inviso, RB Tab; Back pause; B twice = disks.
// Hardcore (HARDCORE.PRG, Yak 1992, an unfinished "5-level evaluation demo"): VBL $168F2 calls the hook at
// $338BA ($104EA title, $145D0 level intro, $10C82 play, $11242 paused). The mouse pointer $338C2 (x, 21..257)
// / $338C4 (y, 22..178, mouse up = bigger) places the blasters on the four edges; the left button fires. Keys
// (table $337B2 -> flags $33833, test $16ECE): p pause, o resume, h 50/60 Hz, s strobes off / on, i puck
// intelligence off / on, n next level (Yak's notes, DOC100E.PRG on the D-Bug 100 disk). The guns fire by
// themselves; the left button = side lasers along the four tracks.
//   left stick = the pointer (relative, through a curve), right stick = the pointer at that spot of the field;
//   A / RT side lasers (and click on the title), Y puck intelligence, Back pause / resume, B twice = disks.
//   Strobes off at the start of play (hc_strobes=1 keeps them).
// Docs: a document reader on a game's disk (D-Bug's displayers, The Alien's DOC displayer): up / down (d-pad
// or stick, repeating) = cursor keys, LB / RB / LT / RT = Shift + cursor (pages), A Return, Y Help, B = back
// to the disks at once. Keys given to StartDocs are typed first (a menu choice).
// Photon Storm (PHOTON.YAK, Arc / Llamasoft 1990; the Andes Attack engine): state $44846 (1 play, 7 title,
// 10 paused); pointer $4F1C0 / $4F1C2 (0..79): its place from the middle is the ship's direction (16, table
// $27958); left button fire, right button thrust; Right Shift pause, Space resume, Q quit.
//   left stick = the direction (written to the pointer), pushed far = thrust too; A / RT fire, X / LT thrust;
//   Back pause / resume, B paused = quit to the title, B twice = disks; elsewhere the stick is the mouse.
enum class Game { kRmc, kAndes, kDefender2, kHardcore, kDocs, kPhotonStorm };
void StartDocs(const std::string& keys);
// Which build is running (addresses differ: Revenge 512K / 1 MB STE).
void SetProgram(const std::string& program);

void Start(Game g);
// Forgets the last game's state (its "leave" request, held keys...): before every game, whichever kind.
void Reset();
// Once per frame, after pad::Poll: pad and keyboard joystick -> the ST; the host mouse's movement and buttons
// (bit 1 left, bit 0 right) are added in.
void Update(st::Machine* m, int mouse_dx, int mouse_dy, uint8_t mouse_buttons);
// Keyboard events (Esc = B; RMC: cursor keys + Space as joystick 1). Returns true if used.
bool HandleKey(const SDL_Event& e, st::Machine* m);
const char* Hint(st::Machine* m, bool have_pad);
bool QuitRequested();
// The background's flashes toned down (Machine::SetCalmBackground) right now.
bool Calm(const st::Machine& m);
// How: peak hold (frames) and brightness cap for Machine::SetCalmBackground.
void CalmStyle(int* hold, float* cap, float* release);
// The emulation is paused (Defender 2's Back): don't run frames.
bool HostPaused();

}  // namespace stgames
