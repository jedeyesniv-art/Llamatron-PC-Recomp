// ADD TO STEAM (steam.h). Steam keeps non-Steam games in userdata\<account>\config\shortcuts.vdf (binary VDF: a
// byte type - 0 map, 1 string, 2 int32, 7 int64, 8 end of map - then the key as a C string, then the value) and
// their library art in config\grid\<appid>{p,,_hero,_logo}.png. A shortcut's appid is crc32("\"exe\"" + name) |
// 0x80000000 (newer Steam stores it as "appid" in the entry; we write it too). Steam reads shortcuts.vdf at start
// and writes it back on exit: changed while it runs, the change is lost - the grid images are only files.
#include "steam.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include "log.h"

namespace fs = std::filesystem;

namespace steam {
namespace {

constexpr const char* kName = "Llamasoft on the ST";
constexpr uint64_t kSteamId64Base = 76561197960265728ull;  // account id = 64-bit Steam id - this

// --- binary VDF ---------------------------------------------------------------------------------------------
struct Node {
  uint8_t type = 0;  // 0 map, 1 string, 2 int32, 7 int64
  std::string key, str;
  uint64_t num = 0;
  std::vector<Node> kids;
};

bool ParseMap(const std::string& d, size_t* at, std::vector<Node>* out) {
  for (;;) {
    if (*at >= d.size()) return false;
    uint8_t type = uint8_t(d[(*at)++]);
    if (type == 8) return true;
    size_t z = d.find('\0', *at);
    if (z == std::string::npos) return false;
    Node n;
    n.type = type, n.key = d.substr(*at, z - *at);
    *at = z + 1;
    if (type == 0) {
      if (!ParseMap(d, at, &n.kids)) return false;
    } else if (type == 1) {
      z = d.find('\0', *at);
      if (z == std::string::npos) return false;
      n.str = d.substr(*at, z - *at), *at = z + 1;
    } else if (type == 2 || type == 7) {
      size_t len = type == 2 ? 4 : 8;
      if (*at + len > d.size()) return false;
      for (size_t i = 0; i < len; ++i) n.num |= uint64_t(uint8_t(d[*at + i])) << (8 * i);
      *at += len;
    } else {
      return false;  // (types we don't know: leave the file alone)
    }
    out->push_back(std::move(n));
  }
}

void WriteMap(const std::vector<Node>& nodes, std::string* out) {
  for (const Node& n : nodes) {
    out->push_back(char(n.type));
    *out += n.key, out->push_back('\0');
    if (n.type == 0) WriteMap(n.kids, out);
    if (n.type == 1) *out += n.str, out->push_back('\0');
    if (n.type == 2 || n.type == 7)
      for (int i = 0; i < (n.type == 2 ? 4 : 8); ++i) out->push_back(char(n.num >> (8 * i)));
  }
  out->push_back(char(8));
}

Node* Find(std::vector<Node>& nodes, const char* key) {  // keys case-insensitively (Steam wrote both "Exe" and "exe")
  for (Node& n : nodes)
    if (SDL_strcasecmp(n.key.c_str(), key) == 0) return &n;
  return nullptr;
}

Node Str(const char* key, const std::string& v) { Node n; n.type = 1, n.key = key, n.str = v; return n; }
Node Int(const char* key, uint32_t v) { Node n; n.type = 2, n.key = key, n.num = v; return n; }

// --- helpers --------------------------------------------------------------------------------------------------
uint32_t Crc32(const std::string& s) {
  uint32_t c = 0xFFFFFFFFu;
  for (unsigned char ch : s) {
    c ^= ch;
    for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
  }
  return ~c;
}

std::string Utf8(const std::wstring& w) {
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(size_t(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}

fs::path FromU8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

std::string U8(const fs::path& p) {
  std::u8string u = p.u8string();
  return std::string(u.begin(), u.end());
}

std::string Lower(std::string s) {
  for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string Unquote(std::string s) {
  if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
  return s;
}

std::string ReadFile(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(f), {});
}

bool WriteFile(const fs::path& p, const std::string& data) {
  fs::path tmp = p;
  tmp += ".new";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f.write(data.data(), std::streamsize(data.size()))) return false;
  }
  std::error_code ec;
  fs::rename(tmp, p, ec);
  return !ec;
}

std::string Env(const char* name) {
  const char* v = std::getenv(name);
  return v ? v : "";
}

// Steam's folder: under Proton (the Deck) the Linux client's, through Wine's Z: drive; on Windows the registry's.
fs::path SteamRoot() {
  std::string test = Env("LLAMATRON_STEAM_ROOT");  // tests: a stand-in Steam folder (never the real one)
  if (!test.empty()) return FromU8(test);
  std::string linux_path = Env("STEAM_COMPAT_CLIENT_INSTALL_PATH");
  if (!linux_path.empty()) return FromU8("Z:" + linux_path);
  wchar_t buf[MAX_PATH];
  DWORD size = sizeof(buf);
  if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ, nullptr, buf, &size) ==
      ERROR_SUCCESS)
    return fs::path(buf);
  return fs::path(L"C:\\Program Files (x86)\\Steam");
}

bool SteamRunning() {
  if (!Env("LLAMATRON_STEAM_ROOT").empty()) return false;  // (tests: the stand-in isn't the running Steam's)
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) return false;
  PROCESSENTRY32W e{};
  e.dwSize = sizeof(e);
  bool found = false;
  for (BOOL ok = Process32FirstW(snap, &e); ok && !found; ok = Process32NextW(snap, &e))
    found = _wcsicmp(e.szExeFile, L"steam.exe") == 0;
  CloseHandle(snap);
  return found;
}

// The account that last logged in (loginusers.vdf, text: "<64-bit id>" { ... "MostRecent" "1" }), else the only
// one with a config folder.
std::optional<fs::path> CurrentUser(const fs::path& root) {
  std::string text = ReadFile(root / "config" / "loginusers.vdf");
  for (size_t at = 0; (at = text.find("\"7656", at)) != std::string::npos;) {
    size_t end = text.find('"', at + 1);
    std::string id = text.substr(at + 1, end - at - 1);
    size_t block_end = text.find('}', end);
    std::string block = Lower(text.substr(end, block_end - end));
    at = end;
    size_t mr = block.find("\"mostrecent\"");
    if (mr != std::string::npos && block.find("\"1\"", mr) != std::string::npos && block.find("\"1\"", mr) < mr + 20) {
      uint64_t account = std::strtoull(id.c_str(), nullptr, 10) - kSteamId64Base;
      fs::path p = root / "userdata" / std::to_string(account);
      if (fs::exists(p)) return p;
    }
  }
  std::vector<fs::path> users;
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(root / "userdata", ec))
    if (e.is_directory() && fs::exists(e.path() / "config")) users.push_back(e.path());
  if (users.size() == 1) return users[0];
  return std::nullopt;
}

// The art: next to the exe (the release's Extras\Steam art), else the repository's res\steam above it.
std::optional<fs::path> ArtFolder() {
  const char* base = SDL_GetBasePath();
  fs::path exe_dir = base ? FromU8(base) : fs::current_path();
  for (fs::path p = exe_dir; !p.empty(); p = p.parent_path()) {
    for (fs::path c : {p / "Extras" / "Steam art", p / "res" / "steam"})
      if (fs::exists(c / "capsule.png")) return c;
    if (p == p.root_path()) break;
  }
  return std::nullopt;
}

bool CopyArt(const fs::path& art, const fs::path& user, uint32_t appid) {
  fs::path grid = user / "config" / "grid";
  std::error_code ec;
  fs::create_directories(grid, ec);
  const std::string id = std::to_string(appid);
  const std::pair<const char*, std::string> files[] = {
      {"capsule.png", id + "p.png"}, {"wide.png", id + ".png"}, {"hero.png", id + "_hero.png"}, {"logo.png", id + "_logo.png"}};
  bool ok = true;
  for (const auto& [from, to] : files) {
    fs::copy_file(art / from, grid / to, fs::copy_options::overwrite_existing, ec);
    if (ec) LOG("steam: couldn't copy %s to %s (%s)", from, U8(grid / to).c_str(), ec.message().c_str()), ok = false;
  }
  if (ok) LOG("steam: library art for %u in %s", appid, U8(grid).c_str());
  return ok;
}

}  // namespace

std::string AddToSteam() {
  std::optional<fs::path> art = ArtFolder();
  if (!art) return LOG("steam: no art folder (Extras\\Steam art)"), "NO ART FOUND";
  const fs::path root = SteamRoot();
  if (!fs::exists(root / "userdata")) return LOG("steam: no Steam at %s", U8(root).c_str()), "STEAM NOT FOUND";

  // Started from Steam as a non-Steam game: SteamGameId = appid << 32 | 0x02000000.
  uint64_t game_id = std::strtoull(Env("SteamGameId").c_str(), nullptr, 10);
  if (game_id >> 32 && (game_id & 0x02000000)) {
    uint32_t appid = uint32_t(game_id >> 32);
    bool any = false;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(root / "userdata", ec)) {  // the users with this shortcut
      std::string vdf = ReadFile(e.path() / "config" / "shortcuts.vdf");
      std::vector<Node> top;
      size_t at = 0;
      bool has = false;
      if (ParseMap(vdf, &at, &top))
        if (Node* list = Find(top, "shortcuts"))
          for (Node& s : list->kids)
            if (Node* a = Find(s.kids, "appid")) has |= uint32_t(a->num) == appid;
      if (has) any |= CopyArt(*art, e.path(), appid);
    }
    LOG("steam: started from Steam as shortcut %u", appid);
    return any ? "ART ADDED" : "SHORTCUT NOT FOUND";
  }

  std::optional<fs::path> user = CurrentUser(root);
  if (!user) return LOG("steam: no logged-in user found under %s", U8(root).c_str()), "NO STEAM USER";
  wchar_t exe_w[MAX_PATH];
  GetModuleFileNameW(nullptr, exe_w, MAX_PATH);
  const std::string exe = Utf8(exe_w);
  const fs::path vdf_path = *user / "config" / "shortcuts.vdf";
  std::string vdf = ReadFile(vdf_path);
  std::vector<Node> top;
  size_t at = 0;
  if (!vdf.empty() && !ParseMap(vdf, &at, &top)) return LOG("steam: can't read %s", U8(vdf_path).c_str()), "CAN'T READ STEAM";
  Node* list = Find(top, "shortcuts");
  if (!list) top.push_back(Node{0, "shortcuts", "", 0, {}}), list = &top.back();

  for (Node& s : list->kids) {  // already there: its art
    Node* e = Find(s.kids, "exe");
    if (!e || Lower(Unquote(e->str)) != Lower(exe)) continue;
    Node* a = Find(s.kids, "appid");
    Node* name = Find(s.kids, "appname");
    uint32_t appid = a ? uint32_t(a->num) : Crc32(e->str + (name ? name->str : "")) | 0x80000000u;
    return CopyArt(*art, *user, appid) ? "ART ADDED" : "ART NOT COPIED";
  }
  if (SteamRunning()) return LOG("steam: Steam is running; it would undo a new shortcut"), "CLOSE STEAM FIRST";

  const std::string quoted = "\"" + exe + "\"";
  const uint32_t appid = Crc32(quoted + kName) | 0x80000000u;
  std::string dir = U8(FromU8(exe).parent_path());
  if (!dir.empty() && dir.back() != '\\') dir += '\\';
  Node entry;
  entry.type = 0;
  int index = 0;
  for (const Node& s : list->kids) index = std::max(index, std::atoi(s.key.c_str()) + 1);
  entry.key = std::to_string(index);
  entry.kids = {Int("appid", appid), Str("AppName", kName), Str("Exe", quoted), Str("StartDir", "\"" + dir + "\""),
                Str("icon", exe), Str("ShortcutPath", ""), Str("LaunchOptions", ""), Int("IsHidden", 0),
                Int("AllowDesktopConfig", 1), Int("AllowOverlay", 1), Int("OpenVR", 0), Int("Devkit", 0),
                Str("DevkitGameID", ""), Int("DevkitOverrideAppID", 0), Int("LastPlayTime", 0),
                Str("FlatpakAppID", ""), Node{0, "tags", "", 0, {}}};
  list->kids.push_back(std::move(entry));
  std::string out;
  WriteMap(top, &out);
  std::error_code ec;
  if (!vdf.empty()) fs::copy_file(vdf_path, fs::path(vdf_path) += ".bak", fs::copy_options::overwrite_existing, ec);
  fs::create_directories(vdf_path.parent_path(), ec);
  if (!WriteFile(vdf_path, out)) return LOG("steam: can't write %s", U8(vdf_path).c_str()), "CAN'T WRITE STEAM";
  LOG("steam: added %s as %u to %s", exe.c_str(), appid, U8(vdf_path).c_str());
  CopyArt(*art, *user, appid);
  return "ADDED";
}

}  // namespace steam
