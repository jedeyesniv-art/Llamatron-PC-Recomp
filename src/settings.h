// userdata\settings.txt: key=value lines, written back on exit (unknown keys kept, missing ones added with their
// defaults so they can be found and edited). Reread while running when the file changes.
#pragma once

#include <map>
#include <string>

namespace settings {

void Load(const std::string& path);
void Save();
// Rereads the file if it changed since the last load (call now and then). True if it was reread.
bool Reload();

double Get(const std::string& key, double def);
void Set(const std::string& key, double value);

}  // namespace settings
