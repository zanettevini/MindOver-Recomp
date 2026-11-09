// Stores the latest activity published by the front-end dispatcher. One
// lock keeps phase, level and pause state from the same update together.
#include "game_state.h"

#include <mutex>

namespace game_state {
namespace {

std::mutex g_mutex;
Snapshot g_snapshot;

}  // namespace

Snapshot Current() {
  std::lock_guard lock(g_mutex);
  return g_snapshot;
}

void Publish(Snapshot snapshot) {
  std::lock_guard lock(g_mutex);
  g_snapshot = snapshot;
}

}  // namespace game_state
