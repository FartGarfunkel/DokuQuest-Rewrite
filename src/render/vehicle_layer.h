// render/vehicle_layer.h — minecarts, boats, the sailing ship, windmills/waterwheels/machines, moving pistons and
// lit TNT as animated voxel models; riding input and container screens for the redstone/vehicle port.
#pragma once

namespace dq {
void registerVehicleLayers();   // once, before the session's layer factories run (client/session.cpp)
} // namespace dq
