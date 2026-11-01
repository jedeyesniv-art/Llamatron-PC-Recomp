#include "disk.h"

#include <cstdio>
#include <cstring>
#include <functional>

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

// Pasti (.stx: disks kept with their copy protection, track by track) -> the readable 512-byte sectors as a raw
// image (protection and timing dropped). Per track: a 16-byte record (record size, fuzzy byte count, sector
// count, flags, track length, track number: bit 7 = side, type), sector descriptors if flags bit 0 (data
// offset, bit position, read time, ID track / side / number / size, CRC, FDC status), the fuzzy mask, the
// track data the offsets point into. See Tools/stx_to_st.py.
Bytes StxToRaw(const Bytes& d, std::string* error) {
  struct Sector {
    unsigned track, side, number;
    size_t at;
  };
  std::vector<Sector> sectors;
  unsigned tracks = d[10], max_track = 0, sides = 1, spt = 0;
  size_t p = 16;
  for (unsigned t = 0; t < tracks; ++t) {
    if (p + 16 > d.size()) break;
    uint32_t rec = Le32(&d[p]), fuzzy = Le32(&d[p + 4]);
    unsigned nsec = Le16(&d[p + 8]), flags = Le16(&d[p + 10]), tnum = d[p + 14];
    unsigned track = tnum & 0x7F, side = tnum >> 7;
    size_t data = p + 16 + ((flags & 1) ? nsec * 16 : 0) + fuzzy;
    for (unsigned i = 0; i < nsec; ++i) {
      size_t at;
      unsigned number = i + 1;
      if (flags & 1) {
        const uint8_t* sd = &d[p + 16 + i * 16];
        // ID field: track 8, side 9, number 10, size 11 (2 = 512 bytes), CRC 12-13; FDC status 14.
        if ((sd[11] & 3) != 2 || (sd[14] & 0x08)) continue;  // not 512 bytes, or not found
        number = sd[10];
        at = data + Le32(sd);
      } else {
        at = data + size_t(i) * 512;
      }
      if (number < 1 || number > 11 || at + 512 > d.size()) continue;
      sectors.push_back({track, side, number, at});
      if (track > max_track) max_track = track;
      if (side) sides = 2;
      if (number > spt) spt = number;
    }
    if (!rec) break;
    p += rec;
  }
  if (sectors.empty()) return *error = "no readable sectors in the STX image", Bytes{};
  Bytes out(size_t(max_track + 1) * sides * spt * 512);
  for (const Sector& s : sectors)
    std::memcpy(&out[((size_t(s.track) * sides + s.side) * spt + s.number - 1) * 512], &d[s.at], 512);
  return out;
}

}  // namespace

Bytes ReadHostFile(const std::string& path) { return ReadFile(path); }

Bytes LoadImage(const std::string& path, std::string* error) {
  Bytes d = ReadFile(path);
  if (d.size() < 512) {
    *error = "can't read " + path;
    return {};
  }
  if (std::memcmp(d.data(), "RSY", 4) == 0) return StxToRaw(d, error);
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
  auto chain = [&](unsigned c) {
    Bytes body;
    for (unsigned guard = 0; c >= 2 && c < 0xFF0 && guard < 4096; c = next(c), ++guard) {
      size_t off = data + size_t(c - 2) * spc * bps;
      if (off + spc * bps > d.size()) break;
      body.insert(body.end(), &d[off], &d[off] + spc * bps);
    }
    return body;
  };
  // A directory's entries (the root's fixed area, or a folder's clusters); folders a few levels deep. Files
  // in folders are listed by their name alone (programs open them that way); a root file of the same name wins.
  std::function<void(const uint8_t*, size_t, int, bool)> list = [&](const uint8_t* dir, size_t n, int depth,
                                                                     bool is_root) {
    for (size_t i = 0; i < n; ++i) {
      const uint8_t* e = dir + i * 32;
      if (e[0] == 0) break;
      if (e[0] == 0xE5 || e[0] == '.' || (e[11] & 0x08)) continue;  // deleted, . / .., volume label
      std::string name(reinterpret_cast<const char*>(e), 8), ext(reinterpret_cast<const char*>(e + 8), 3);
      name.erase(name.find_last_not_of(' ') + 1);
      ext.erase(ext.find_last_not_of(' ') + 1);
      if (!ext.empty()) name += "." + ext;
      if (e[11] & 0x10) {  // a folder
        if (depth < 3) {
          Bytes sub = chain(Le16(e + 26));
          list(sub.data(), sub.size() / 32, depth + 1, false);
        }
        continue;
      }
      if (!is_root && files.count(name)) continue;
      Bytes body = chain(Le16(e + 26));
      uint32_t size = Le32(e + 28);
      if (body.size() > size) body.resize(size);
      files[name] = std::move(body);
    }
  };
  if (root + ndir * 32 <= d.size()) list(&d[root], ndir, 0, true);
  return files;
}

bool LoadPrg(const Bytes& prg, Program* out, std::string* error) {
  if (prg.size() < 28 || Be16(prg.data()) != 0x601A) return *error = "not a TOS program", false;
  uint32_t tl = Be32(&prg[2]), dl = Be32(&prg[6]), bl = Be32(&prg[10]), sl = Be32(&prg[14]);
  bool no_reloc = Be16(&prg[26]) != 0;
  if (28 + uint64_t(tl) + dl + sl > prg.size()) return *error = "truncated program", false;
  out->text.assign(prg.begin() + 28, prg.begin() + 28 + tl + dl);
  out->relocs.clear();
  out->bss_size = bl;
  size_t r = 28 + size_t(tl) + dl + sl;
  if (no_reloc || r + 4 > prg.size()) return true;
  uint32_t off = Be32(&prg[r]);
  if (!off) return true;
  out->relocs.push_back(off);
  for (size_t i = r + 4; i < prg.size() && prg[i]; ++i) {
    if (prg[i] == 1) {
      off += 254;
    } else {
      off += prg[i];
      out->relocs.push_back(off);
    }
  }
  return true;
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
