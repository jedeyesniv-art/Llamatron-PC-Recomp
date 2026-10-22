// Windows: logs unhandled exceptions (code, address, a stack walk) as module offsets, for llvm-symbolizer:
//   llvm-symbolizer --obj=llamatron.exe 0x140000000+<offset>
#include "crash.h"

#ifdef _WIN32
#include <windows.h>

#include "log.h"

namespace {

void Describe(DWORD64 addr, char* out, size_t n) {
  HMODULE mod = nullptr;
  char name[MAX_PATH] = "?";
  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCSTR>(addr), &mod)) {
    GetModuleFileNameA(mod, name, MAX_PATH);
    const char* base = strrchr(name, '\\');
    snprintf(out, n, "%s+0x%llX", base ? base + 1 : name, (unsigned long long)(addr - reinterpret_cast<DWORD64>(mod)));
  } else {
    snprintf(out, n, "0x%llX", (unsigned long long)addr);
  }
}

LONG WINAPI Filter(EXCEPTION_POINTERS* ep) {
  char where[MAX_PATH + 32];
  Describe(reinterpret_cast<DWORD64>(ep->ExceptionRecord->ExceptionAddress), where, sizeof(where));
  LOG("CRASH: exception 0x%08lX at %s", ep->ExceptionRecord->ExceptionCode, where);
  CONTEXT ctx = *ep->ContextRecord;
  for (int i = 0; i < 32; ++i) {
    DWORD64 image = 0;
    PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(ctx.Rip, &image, nullptr);
    if (!fn) {  // leaf function
      ctx.Rip = *reinterpret_cast<DWORD64*>(ctx.Rsp);
      ctx.Rsp += 8;
    } else {
      void* handler_data;
      DWORD64 frame;
      RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, ctx.Rip, fn, &ctx, &handler_data, &frame, nullptr);
    }
    if (!ctx.Rip) break;
    Describe(ctx.Rip, where, sizeof(where));
    LOG("  #%d %s", i, where);
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

void InstallCrashHandler() { SetUnhandledExceptionFilter(Filter); }

#else
void InstallCrashHandler() {}
#endif
