// Native game activity for consumers such as a presence integration. The
// front-end dispatcher publishes values from the game thread (findings/29 s.8).
// The interface uses host values and carries no guest pointers.
#pragma once

#include <cstdint>
#include <optional>

namespace game_state {

enum class Phase { kStarting, kFrontEnd, kLoading, kInGame };

struct Snapshot {
  Phase phase = Phase::kStarting;
  std::optional<uint8_t> level_id;  // Internal level index, available in game.
  bool paused = false;
};

// Returns the most recent front-end update by value. Safe on any thread,
// including before the game starts. No game memory is accessed here.
Snapshot Current();

// Called by the game's main thread when it observes a new snapshot.
void Publish(Snapshot snapshot);

}  // namespace game_state
