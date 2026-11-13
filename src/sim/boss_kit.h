// sim/boss_kit.h — imported bosses fighting by their own attack tables (sim/boss_kit.cpp)
#pragma once
namespace dq {
void registerBossKits();   // (initCore, after the registry: every mobs.json type with a "kit")
} // namespace dq
