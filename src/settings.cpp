#include "settings.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>

#include "log.h"

namespace settings {
namespace {

std::string g_path;
std::map<std::string, double> g_values;
std::filesystem::file_time_type g_time{};

void Read() {
  std::error_code ec;
  g_time = std::filesystem::last_write_time(g_path, ec);
  FILE* f = std::fopen(g_path.c_str(), "r");
  if (!f) return;
  char line[256];
  while (std::fgets(line, sizeof(line), f)) {
    std::string s = line;
    size_t eq = s.find('=');
    if (s.empty() || s[0] == '#' || eq == std::string::npos) continue;
    std::string key = s.substr(0, eq);
    while (!key.empty() && key.back() == ' ') key.pop_back();
    g_values[key] = std::atof(s.c_str() + eq + 1);
  }
  std::fclose(f);
}

}  // namespace

void Load(const std::string& path) {
  g_path = path;
  Read();
}

void Save() {
  if (g_path.empty()) return;
  FILE* f = std::fopen(g_path.c_str(), "w");
  if (!f) return;
  std::fprintf(f, "# Llamatron PC settings (edit while the game runs: it rereads this file)\n");
  for (const auto& [key, value] : g_values) std::fprintf(f, "%s=%g\n", key.c_str(), value);
  std::fclose(f);
  std::error_code ec;
  g_time = std::filesystem::last_write_time(g_path, ec);
}

bool Reload() {
  std::error_code ec;
  auto t = std::filesystem::last_write_time(g_path, ec);
  if (ec || t == g_time) return false;
  Read();
  LOG("settings reread");
  return true;
}

double Get(const std::string& key, double def) {
  auto it = g_values.find(key);
  if (it != g_values.end()) return it->second;
  g_values[key] = def;
  return def;
}

void Set(const std::string& key, double value) { g_values[key] = value; }

void Erase(const std::string& key) { g_values.erase(key); }

}  // namespace settings
