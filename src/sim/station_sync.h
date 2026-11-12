// sim/station_sync.h — the immersive stations in multiplayer: a guest lights a forge, a kiln, a bloomery or a charcoal pit
// (or seals a barrel) in its own world and sends the block entity; on the host that change adds the station to the list
// its system ticks (lv.info.data "forges", "kilns", "bloomeries", "charcoalPits", "barrels"), as lighting it there would.
#pragma once

namespace dq {
class Level;
void stationSyncTick(Level& lv);   // (the host: hooks each open world once)
} // namespace dq
