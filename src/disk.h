// Atari ST disk images and the Llamatron program file.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace disk {

using Bytes = std::vector<uint8_t>;

// Decodes an .msa image to a raw sector image; a raw .st image is returned unchanged. Empty on error.
Bytes LoadImage(const std::string& path, std::string* error);

// A file on the PC (empty if it can't be read).
Bytes ReadHostFile(const std::string& path);

// The files in the root directory of a FAT12 floppy image, keyed by upper-case "NAME.EXT".
std::map<std::string, Bytes> ReadFiles(const Bytes& image);

// TRON1MEG.PRG unpacked (see Tools/depack.py): the game's text as loaded at address 0, and the offsets of the
// longs to relocate.
struct Program {
  Bytes text;
  std::vector<uint32_t> relocs;
  uint32_t bss_size = 0;
};
bool UnpackLlamatron(const Bytes& prg, Program* out, std::string* error);

// A standard TOS program (.PRG): text + data as loaded at 0, its relocations and BSS size.
bool LoadPrg(const Bytes& prg, Program* out, std::string* error);

}  // namespace disk
