#include "log.h"

#include <mutex>

namespace logging {
namespace {
FILE* g_file = nullptr;
std::mutex g_mutex;
}  // namespace

void Open(const char* path) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_file) std::fclose(g_file);
  g_file = std::fopen(path, "w");
}

void Write(const char* fmt, ...) {
  char buf[1024];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  std::lock_guard<std::mutex> lock(g_mutex);
  std::fprintf(stderr, "%s\n", buf);
  if (g_file) {
    std::fprintf(g_file, "%s\n", buf);
    std::fflush(g_file);
  }
}

}  // namespace logging
