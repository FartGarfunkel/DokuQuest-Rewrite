// sim/fallen.h — the fallen adventurers (our own): an adventurer's bones where
// they fell (remains_traveller / desert / snowy / jungle / swamp / miner, tools/port/polish_data.py), a few of their
// things on them. What they carry is rolled the first time they're opened (their kind and where they lie decide it,
// the same for everyone); now and then they're a trap, and opening them raises what killed them.
#pragma once
#include "sim/level.h"
#include <memory>

namespace dq {
class Player;

// fills a remains' container record (sim/build.cpp containerEntity, the first time it's made)
void fallenFill(World& w, int x, int y, int z, json& be);
// opening it (sim/controller.cpp, before the container screen): a trap springs (the host's game only)
void fallenOpened(Level& lv, Player& p, World& w, int x, int y, int z);

// How each one lies (render/fallen_layer.cpp draws it; its hitboxes are the same bones): one of the poses, its kind's own
// the likeliest, each limb a little off, and some with arrows still in them. All from where it lies.
class Creature; struct RigModel;
enum FallenPoseKind { FALLEN_SIT, FALLEN_LIE, FALLEN_KNEEL, FALLEN_FACEDOWN, FALLEN_CURLED, FALLEN_FOLDED, FALLEN_SPRAWL, FALLEN_REACH,
                      FALLEN_RIBCAGE, FALLEN_SCATTERED, FALLEN_POSES };
struct FallenKind { const char* model; int pose; const char* helmet; const char* held; };
const FallenKind* fallenKind(const std::string& kind);   // null: not a remains kind
struct FallenVariant { int pose = 0, jitter = 0, arrows = 0; u32 seed = 0; };
FallenVariant fallenVariant(ivec3 at, const FallenKind& K);
// one posed skeleton per kind, realm, pose and twist (posed once, kept): its creature (mats posed, block units, the
// remains' cell's floor centre the origin), which parts are left out, and whether it still holds its thing
struct FallenPose { std::unique_ptr<Creature> c; const RigModel* RM = nullptr; const FallenKind* K = nullptr; std::vector<u8> hide; bool held = true; };
const FallenPose* fallenPose(const std::string& kind, int dim, int pose, int jitter);
// the remains' bones as boxes (pixels, the block's cell): set as their shapes (world/shapes.h setPlacedShapes)
void registerFallen();

} // namespace dq
