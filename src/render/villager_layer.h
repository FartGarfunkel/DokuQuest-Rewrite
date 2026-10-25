// render/villager_layer.h — the villagers as voxel people (game/villagers.js Mob.draws through avatarDraws): each one's
// skin painted from their seed and job (sim npcSkinPixels), built by the character builders (assets/char_model) and
// animated by the character rig (render/char_anim: walking, the villagers' waving, talking, folded arms, reading, sitting
// on the benches and sleeping in their beds; the head turning to look at you); guards in iron. Their names over their
// heads up close, a ! over anyone with work for you (a ? when you can hand it in), your jobs top right (questHud), and
// the screens the simulation asks for (event villagerScreen).
//
// Dev switches (environment): DQ_VILLAGERS=farmer,librarian,guard,elder (villagers in front of you after a few frames),
// DQ_VILLAGER_OPEN=talk|shop|board|path (opens that screen on the nearest one), DQ_DAYTIME=0..1 (time of day).
#pragma once
#include "client/session.h"

namespace dq {

void registerVillagerClient();   // idempotent; called from GameSession's constructor

} // namespace dq
