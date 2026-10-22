#include "disk.h"

#include <cstdio>
#include <cstring>

namespace disk {
namespace {

uint16_t Be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }
uint32_t Be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
uint16_t Le16(const uint8_t* p) { return uint16_t(p[1] << 8 | p[0]); }
uint32_t Le32(const uint8_t* p) { return uint32_t(p[3]) << 24 | p[2] << 16 | p[1] << 8 | p[0]; }

Bytes ReadFile(const std::string& path) {
  Bytes d;
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return d;
  std::fseek(f, 0, SEEK_END);
  d.resize(size_t(std::ftell(f)));
  std::fseek(f, 0, SEEK_SET);
  if (std::fread(d.data(), 1, d.size(), f) != d.size()) d.clear();
  std::fclose(f);
  return d;
}

}  // namespace

Bytes LoadImage(const std::string& path, std::string* error) {
  Bytes d = ReadFile(path);
  if (d.size() < 512) {
    *error = "can't read " + path;
    return {};
  }
  if (Be16(d.data()) != 0x0E0F) return d;  // raw .st

  // MSA: header (magic, sectors per track, sides - 1, first track, last track), then per track a u16 length
  // and the track data, RLE-packed unless the length is a whole track: E5 <byte> <count u16>.
  unsigned spt = Be16(&d[2]), sides = Be16(&d[4]) + 1, first = Be16(&d[6]), last = Be16(&d[8]);
  size_t p = 10;
  Bytes out;
  for (unsigned t = first; t <= last; ++t) {
    for (unsigned s = 0; s < sides; ++s) {
      if (p + 2 > d.size()) return *error = "truncated MSA image", Bytes{};
      size_t n = Be16(&d[p]);
      p += 2;
      if (p + n > d.size()) return *error = "truncated MSA image", Bytes{};
      const uint8_t* blk = &d[p];
      p += n;
      if (n == spt * 512) {
        out.insert(out.end(), blk, blk + n);
        continue;
      }
      for (size_t i = 0; i < n;) {
        if (blk[i] == 0xE5 && i + 4 <= n) {
          out.insert(out.end(), Be16(blk + i + 2), blk[i + 1]);
          i += 4;
        } else {
          out.push_back(blk[i++]);
        }
      }
    }
  }
  return out;
}

std::map<std::string, Bytes> ReadFiles(const Bytes& d) {
  std::map<std::string, Bytes> files;
  if (d.size() < 512) return files;
  unsigned bps = Le16(&d[11]), spc = d[13], res = Le16(&d[14]), nfats = d[16], ndir = Le16(&d[17]);
  unsigned spf = Le16(&d[22]);
  if (bps != 512 || !spc) return files;
  const uint8_t* fat = &d[res * bps];
  size_t root = size_t(res + nfats * spf) * bps;
  size_t data = root + ndir * 32;
  auto next = [&](unsigned c) {
    unsigned v = fat[c * 3 / 2] | fat[c * 3 / 2 + 1] << 8;
    return c & 1 ? v >> 4 : v & 0xFFF;
  };
  for (unsigned i = 0; i < ndir && root + i * 32 + 32 <= d.size(); ++i) {
    const uint8_t* e = &d[root + i * 32];
    if (e[0] == 0) break;
    if (e[0] == 0xE5 || (e[11] & 0x18)) continue;  // deleted, volume label, directory
    std::string name(reinterpret_cast<const char*>(e), 8), ext(reinterpret_cast<const char*>(e + 8), 3);
    name.erase(name.find_last_not_of(' ') + 1);
    ext.erase(ext.find_last_not_of(' ') + 1);
    if (!ext.empty()) name += "." + ext;
    Bytes body;
    uint32_t size = Le32(e + 28);
    for (unsigned c = Le16(e + 26), guard = 0; c >= 2 && c < 0xFF0 && guard < 4096; c = next(c), ++guard) {
      size_t off = data + size_t(c - 2) * spc * bps;
      if (off + spc * bps > d.size()) break;
      body.insert(body.end(), &d[off], &d[off] + spc * bps);
    }
    if (body.size() > size) body.resize(size);
    files[name] = std::move(body);
  }
  return files;
}

bool UnpackLlamatron(const Bytes& prg, Program* out, std::string* error) {
  // The PRG is a self-extractor: a 38-byte mover, then an LZ77 stream that unpacks to the text start, followed
  // by a GEMDOS relocation table; the depacker then clears $965 words of BSS (Tools/depack.py).
  constexpr uint32_t kTextSize = 0x4CFA6, kStream = 0x26, kBssWords = 0x965;
  static const uint8_t kDepacker[] = {0x26, 0x6F, 0x00, 0x04, 0x47, 0xEB, 0x01, 0x00};
  if (prg.size() < 28 || Be16(prg.data()) != 0x601A) return *error = "not a TOS program", false;
  uint32_t tl = Be32(&prg[2]);
  if (28 + tl > prg.size()) return *error = "truncated program", false;
  const uint8_t* src = &prg[28];
  bool found = false;
  for (uint32_t i = 0; i + sizeof(kDepacker) <= tl && !found; ++i)
    found = std::memcmp(src + i, kDepacker, sizeof(kDepacker)) == 0;
  if (!found) return *error = "not the Llamatron program (depacker not found)", false;

  Bytes d;
  for (uint32_t p = kStream; p < tl;) {
    uint8_t b = src[p++];
    if (b >= 0x80) {  // match: 3..34 bytes, distance up to 2 KB
      if (p >= tl) break;
      unsigned n = (b & 0x1F) + 3, dist = ((b << 3) & 0xFF00) | src[p++];
      size_t from = d.size() - 0x800 + dist;
      if (from >= d.size()) return *error = "bad packed data", false;
      for (unsigned k = 0; k < n; ++k) d.push_back(d[from + k]);
    } else if (b == 0) {
      break;
    } else {  // literal run
      if (p + b > tl) return *error = "bad packed data", false;
      d.insert(d.end(), src + p, src + p + b);
      p += b;
    }
  }
  if (d.size() < kTextSize + 4) return *error = "bad packed data (too short)", false;
  out->text.assign(d.begin(), d.begin() + kTextSize);
  out->relocs.clear();
  const uint8_t* t = &d[kTextSize];
  size_t tn = d.size() - kTextSize;
  uint32_t off = Be32(t);
  if (off) {
    out->relocs.push_back(off);
    for (size_t i = 4; i < tn; ++i) {
      if (t[i] == 0) break;
      if (t[i] == 1) {
        off += 254;
      } else {
        off += t[i];
        out->relocs.push_back(off);
      }
    }
  }
  out->bss_size = kBssWords * 2;
  return true;
}

}  // namespace disk
