// =============================================================================
// system/thread_states.cpp -- Windows' answers about this process's threads
// (see the header: ids, names, run states for helper_threads.cpp)
// =============================================================================
#include "system/thread_states.h"

#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace thread_states {
namespace {

// ---- NtQuerySystemInformation(SystemProcessInformation) ----------------------
// The record layouts as Windows writes them on x64 (the documented winternl.h
// versions hide most fields as "Reserved"; the offsets used here are the
// stable ones every process-listing tool relies on: the thread array follows
// a 0x100-byte process record, each thread record is 0x50 bytes).
constexpr ULONG kSystemProcessInformation = 5;
constexpr LONG kStatusInfoLengthMismatch = LONG(0xC0000004);

struct UnicodeString {
  USHORT Length;
  USHORT MaximumLength;
  PWSTR Buffer;
};

struct ThreadRecord {           // SYSTEM_THREAD_INFORMATION
  LARGE_INTEGER KernelTime;
  LARGE_INTEGER UserTime;
  LARGE_INTEGER CreateTime;
  ULONG WaitTime;
  PVOID StartAddress;
  HANDLE UniqueProcess;         // CLIENT_ID
  HANDLE UniqueThread;
  LONG Priority;
  LONG BasePriority;
  ULONG ContextSwitches;
  ULONG ThreadState;            // KTHREAD_STATE, below
  ULONG WaitReason;
};

struct ProcessRecord {          // SYSTEM_PROCESS_INFORMATION (x64)
  ULONG NextEntryOffset;
  ULONG NumberOfThreads;
  BYTE Reserved1[48];
  UnicodeString ImageName;
  LONG BasePriority;
  HANDLE UniqueProcessId;
  BYTE Rest[0x100 - 0x58];      // up to the thread array
  // ThreadRecord Threads[NumberOfThreads] follows
};

#if defined(_WIN64)
static_assert(sizeof(ThreadRecord) == 0x50, "SYSTEM_THREAD_INFORMATION is 0x50 bytes on x64");
static_assert(offsetof(ProcessRecord, UniqueProcessId) == 0x50, "UniqueProcessId at +0x50 on x64");
static_assert(sizeof(ProcessRecord) == 0x100, "the thread array starts at +0x100 on x64");
#endif

// KTHREAD_STATE: 0 Initialized, 1 Ready, 2 Running, 3 Standby, 4 Terminated,
// 5 Waiting, 6 Transition (ready, its stack paged out), 7 DeferredReady,
// 8 GateWaitObsolete, 9 WaitingForProcessInSwap.
Run RunOf(ULONG state) {
  switch (state) {
    case 4: return Run::kEnded;
    case 5:
    case 8:
    case 9: return Run::kWaiting;
    default: return Run::kRunnable;  // a new thread (0) counts as about to run, as on Linux
  }
}

using NtQuerySystemInformationFn = LONG(NTAPI*)(ULONG, PVOID, ULONG, PULONG);
NtQuerySystemInformationFn Query() {
  static const NtQuerySystemInformationFn fn = reinterpret_cast<NtQuerySystemInformationFn>(
      reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQuerySystemInformation")));
  return fn;
}

using GetThreadDescriptionFn = HRESULT(WINAPI*)(HANDLE, PWSTR*);
GetThreadDescriptionFn Describe() {
  static const GetThreadDescriptionFn fn = reinterpret_cast<GetThreadDescriptionFn>(
      reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetThreadDescription")));
  return fn;
}

void NameOfHandle(HANDLE thread, char* out, size_t size) {
  if (!size) return;
  out[0] = 0;
  const GetThreadDescriptionFn describe = Describe();
  PWSTR wide = nullptr;
  if (!describe || FAILED(describe(thread, &wide)) || !wide) return;
  // Thread names here are ASCII ("XThread3", "audio::Streamer", "Sound Out").
  size_t i = 0;
  for (; wide[i] && i + 1 < size; ++i) out[i] = wide[i] < 128 ? char(wide[i]) : '?';
  out[i] = 0;
  LocalFree(wide);
}

std::mutex g_names_mutex;
std::unordered_map<uint32_t, std::string> g_names;  // thread id -> name (cached)

}  // namespace

uint32_t CurrentId() { return uint32_t(GetCurrentThreadId()); }

void CurrentName(char* out, size_t size) { NameOfHandle(GetCurrentThread(), out, size); }

void NameOf(uint32_t id, char* out, size_t size) {
  if (!size) return;
  {
    std::lock_guard<std::mutex> lock(g_names_mutex);
    if (const auto it = g_names.find(id); it != g_names.end()) {
      std::strncpy(out, it->second.c_str(), size - 1);
      out[size - 1] = 0;
      return;
    }
  }
  out[0] = 0;
  if (HANDLE thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, DWORD(id))) {
    NameOfHandle(thread, out, size);
    CloseHandle(thread);
  }
  // Cached only once named: the SDK names a thread right after creating it,
  // so an early look can find it still unnamed.
  if (out[0]) {
    std::lock_guard<std::mutex> lock(g_names_mutex);
    g_names[id] = out;
  }
}

bool Snapshot(std::vector<Thread>& out) {
  out.clear();
  const NtQuerySystemInformationFn query = Query();
  if (!query) return false;
  // Every process of the system comes back; this one is picked out below.
  // The buffer grows when Windows says it's too small, and is kept per thread.
  thread_local std::vector<uint8_t> buffer(1 << 20);
  ULONG needed = 0;
  LONG status = query(kSystemProcessInformation, buffer.data(), ULONG(buffer.size()), &needed);
  for (int tries = 0; status == kStatusInfoLengthMismatch && tries < 4; ++tries) {
    buffer.resize(size_t(needed) + (256 << 10));
    status = query(kSystemProcessInformation, buffer.data(), ULONG(buffer.size()), &needed);
  }
  if (status < 0) return false;
  const HANDLE self = reinterpret_cast<HANDLE>(uintptr_t(GetCurrentProcessId()));
  size_t at = 0;
  while (at + sizeof(ProcessRecord) <= buffer.size()) {
    const auto* process = reinterpret_cast<const ProcessRecord*>(buffer.data() + at);
    if (process->UniqueProcessId == self) {
      const auto* threads = reinterpret_cast<const ThreadRecord*>(process + 1);
      for (ULONG i = 0; i < process->NumberOfThreads; ++i) {
        out.push_back({uint32_t(uintptr_t(threads[i].UniqueThread)), RunOf(threads[i].ThreadState)});
      }
      return true;
    }
    if (!process->NextEntryOffset) break;
    at += process->NextEntryOffset;
  }
  return false;
}

}  // namespace thread_states
