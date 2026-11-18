// =============================================================================
// system/thread_states.h -- what Windows says about this process's threads:
// their ids, names and run states (Windows' answers to helper_threads' /proc)
// =============================================================================
//
// WHY. "Loading on game frames" (system/helper_threads.h) decides, before
// every frame, whether the game's helper threads (loading, audio streaming)
// are all idle. On Linux it asks /proc: a thread's id (gettid), its name
// (/proc/self/task/<tid>/comm), its run state (/proc/self/task/<tid>/stat:
// R running or ready, S / D asleep) and whether it still exists. Windows has
// no /proc; this file answers the same questions from Windows' own calls, so
// helper_threads.cpp keeps its logic and only swaps where the answers come
// from (findings/68 s.6h).
//
//   id         GetCurrentThreadId
//   name       GetThreadDescription (Windows 10 1607+; the SDK names its
//              threads with SetThreadDescription, so the game's "XThread...",
//              "audio::..." and the port's "Sound Out", "VBlank" show here as
//              in /proc), looked up once per thread id and cached
//   run state  NtQuerySystemInformation(SystemProcessInformation): one
//              snapshot of every thread of this process with its KTHREAD
//              state (Ready / Running / Standby / DeferredReady = runnable,
//              like Linux's R; Waiting = asleep, like S / D)
//   ended      not in the snapshot, or its state is Terminated
//
// Its own file so that windows.h (and its macros: AGENTS.md s.5) stays out of
// helper_threads.cpp. Windows only: on other systems these functions are not
// built (helper_threads.cpp reads /proc there).
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace thread_states {

enum class Run : uint8_t {
  kRunnable,  // running, or ready to run (Linux: R)
  kWaiting,   // in a wait (Linux: S / D)
  kEnded,     // terminated (or about to be)
};

struct Thread {
  uint32_t id = 0;
  Run run = Run::kRunnable;
};

// The calling thread's id.
uint32_t CurrentId();
// The calling thread's name (empty if it has none), NUL-terminated in `out`.
void CurrentName(char* out, size_t size);
// Thread `id`'s name (cached per id; empty if it has none or is gone).
void NameOf(uint32_t id, char* out, size_t size);

// Every thread of this process right now. False (and `out` empty) if Windows
// didn't answer. Any thread; each calling thread keeps its own buffer.
bool Snapshot(std::vector<Thread>& out);

}  // namespace thread_states
