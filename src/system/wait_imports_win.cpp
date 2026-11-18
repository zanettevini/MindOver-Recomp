// =============================================================================
// system/wait_imports_win.cpp -- WINDOWS: the game's five wait imports go
// through helper_threads::Wait, as the Xbox calls meter's wrappers do on Linux
// =============================================================================
//
// WHY. "Loading on game frames" (system/helper_threads.h) needs to know when
// each of the game's helper threads (loading, audio streaming) is inside a
// wait, and it moves their timed waits onto game time. On Linux every call
// the generated code makes to a kernel import (__imp__<Name>) can be
// redirected with the linker's --wrap, and CMakeLists.txt generates wrappers
// that call helper_threads::Wait for the five waits. Windows' linker has no
// --wrap, so on Windows these waits went straight to the SDK: the helpers
// worked whenever the PC let them, and a LAN session on one PC parted in one
// run out of five (findings/68 s.6h).
//
// HOW. The SDK's runtime library is a DLL (rexruntime) built with
// WINDOWS_EXPORT_ALL_SYMBOLS, so it exports every __imp__<Name>, and the
// generated code (and the function table in generated/default/
// crash_mom_init.cpp) refers to those names as plain functions, resolved by
// the runtime's import library. An import library only supplies a symbol that
// no object file defines, so the definitions below, in the game's own
// executable, are what the game calls. Each one finds the runtime's original
// once (GetProcAddress on the loaded module that exports that name; the DLL's
// file name differs by build type: rexruntimerd.dll, rexruntime.dll ...) and
// calls helper_threads::Wait with it, exactly like the Linux wrapper:
//
//   import                        timeout argument (guest pointer)   a sleep?
//   KeWaitForSingleObject         r7 (object, reason, mode, alertable, timeout)
//   KeWaitForMultipleObjects      r9 (count, objects, type, reason, mode, alertable, timeout, blocks)
//   NtWaitForSingleObjectEx       r6 (handle, mode, alertable, timeout)
//   NtWaitForMultipleObjectsEx    r8 (count, handles, type, mode, alertable, timeout)
//   KeDelayExecutionThread        r5 (mode, alertable, interval)          yes
//
// (The same table as CMakeLists.txt's Linux wrappers.)
//
// If the original can't be found (a renamed export), the call is a fatal
// error with the name: a silent fallback would leave the game without its
// waits.
// =============================================================================
#if defined(_WIN32)

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// (after windows.h) K32EnumProcessModules: kernel32 itself, no psapi.lib needed
#include <psapi.h>

#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

#include "system/helper_threads.h"

namespace {

using Import = void (*)(PPCContext&, uint8_t*);

// The loaded module (other than the game's executable) that exports `name`.
Import FindOriginal(const char* name) {
  HMODULE modules[512];
  DWORD bytes = 0;
  const HANDLE process = GetCurrentProcess();
  if (!K32EnumProcessModules(process, modules, sizeof(modules), &bytes)) return nullptr;
  const HMODULE self = GetModuleHandleW(nullptr);
  const DWORD count = bytes / sizeof(HMODULE);
  for (DWORD i = 0; i < count && i < 512; ++i) {
    if (modules[i] == self) continue;
    if (FARPROC f = GetProcAddress(modules[i], name)) {
      return reinterpret_cast<Import>(reinterpret_cast<void*>(f));
    }
  }
  return nullptr;
}

// One of the five: the original, found once (the first wait can come from any
// thread, hence the once flag).
struct Original {
  const char* name;
  std::once_flag once;
  Import function = nullptr;

  Import Get() {
    std::call_once(once, [this] {
      function = FindOriginal(name);
      if (function) {
        REXLOG_INFO("Loading on game frames (Windows): {} goes through the helper-thread rules", name);
      }
    });
    if (!function) {
      std::fprintf(stderr, "wait_imports_win: the runtime doesn't export %s\n", name);
      std::abort();
    }
    return function;
  }
};

Original g_ke_wait_single{"__imp__KeWaitForSingleObject"};
Original g_ke_wait_multiple{"__imp__KeWaitForMultipleObjects"};
Original g_nt_wait_single{"__imp__NtWaitForSingleObjectEx"};
Original g_nt_wait_multiple{"__imp__NtWaitForMultipleObjectsEx"};
Original g_ke_delay{"__imp__KeDelayExecutionThread"};

// helper_threads::Wait takes a plain function pointer: one trampoline each.
void RealKeWaitSingle(PPCContext& ctx, uint8_t* base) { g_ke_wait_single.Get()(ctx, base); }
void RealKeWaitMultiple(PPCContext& ctx, uint8_t* base) { g_ke_wait_multiple.Get()(ctx, base); }
void RealNtWaitSingle(PPCContext& ctx, uint8_t* base) { g_nt_wait_single.Get()(ctx, base); }
void RealNtWaitMultiple(PPCContext& ctx, uint8_t* base) { g_nt_wait_multiple.Get()(ctx, base); }
void RealKeDelay(PPCContext& ctx, uint8_t* base) { g_ke_delay.Get()(ctx, base); }

}  // namespace

// The game's calls land here instead of the runtime's import (see the header).
extern "C" REX_FUNC(__imp__KeWaitForSingleObject) {
  helper_threads::Wait(ctx, base, RealKeWaitSingle, 7, false);
}
extern "C" REX_FUNC(__imp__KeWaitForMultipleObjects) {
  helper_threads::Wait(ctx, base, RealKeWaitMultiple, 9, false);
}
extern "C" REX_FUNC(__imp__NtWaitForSingleObjectEx) {
  helper_threads::Wait(ctx, base, RealNtWaitSingle, 6, false);
}
extern "C" REX_FUNC(__imp__NtWaitForMultipleObjectsEx) {
  helper_threads::Wait(ctx, base, RealNtWaitMultiple, 8, false);
}
extern "C" REX_FUNC(__imp__KeDelayExecutionThread) {
  helper_threads::Wait(ctx, base, RealKeDelay, 5, true);
}

#endif  // _WIN32
