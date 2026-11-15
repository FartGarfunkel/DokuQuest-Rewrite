// render/env_layers.h — the world's surroundings as render layers: distant terrain, clouds, weather
// (rain, snow, lightning, sand, ash), ambient effects (torch flames, smoke, drips, leaves, fireflies,
// wind) and the particle engine. registerEnvironmentLayers() is called once from App::run (client start).
#pragma once
#include <vector>
#include <glm/glm.hpp>

namespace dq {
void registerEnvironmentLayers();
void registerDynLights();
// the stonecutters about you (x, y, z, meta), found with the torches and fires each frame: their blades are turned by
// render/station_work_layer.cpp (data/shapes.json "spin")
const std::vector<glm::ivec4>& sawsNear();
const std::vector<glm::ivec4>& grindsNear();
const std::vector<glm::ivec4>& jukesNear();    // the jukeboxes about you (their Tactile arm and record)   // the grindstones about you (their Tactile wheel turned by render/station_work_layer.cpp)   // dyn_light.cpp: carried torches, glowing drops, lit TNT, fireballs light the world (called by registerEnvironmentLayers)
} // namespace dq
