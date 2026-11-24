// gen/structures_vault.h — a buried wayvault: three rooms, a lever, an iron door, a shaft down.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace dq {

constexpr int kWayvaultCeil = 5; // local y of the cobblestone ceiling over each room

struct Wayvault {
  struct Room { int x, z, rx, rz; std::string role; };
  std::vector<Room> rooms;
  int lever[3];
  int door[3];
  int chest[3];
  int spawner[3];
  int shaft[3]; // top of the air column (local x, y, z); x/z on the ante
};

// Local layout for `seed` (not a world position). The place step stores that seed; build calls this again.
bool planWayvault(uint32_t seed, Wayvault& out);
void registerStructureVaults();

} // namespace dq
