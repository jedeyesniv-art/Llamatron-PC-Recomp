// Minimal logging: to stderr and, if opened, a log file.
#pragma once

#include <cstdarg>
#include <cstdio>

namespace logging {

void Open(const char* path);
void Write(const char* fmt, ...)
#if defined(__clang__) || defined(__GNUC__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;

}  // namespace logging

#define LOG(...) logging::Write(__VA_ARGS__)
