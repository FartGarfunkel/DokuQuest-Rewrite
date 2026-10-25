// sim/villager_hooks.cpp — gameplay's "use an entity" (sim/controller.cpp hooks().useEntity) goes through the creatures'
// useOnCreature (villagers talk, the missing follow, animals are fed, tamed, sheared, ridden...). A file of its own:
// sim/gameplay.h and sim/content.h declare the same helpers with different default arguments.
#include "sim/gameplay.h"
#include "sim/controller.h"
#include "sim/creature.h"

namespace dq {

void registerVillagerUseHook() {
  hooks().useEntity.push_back([](PlayerController& pc, Entity& e, ItemStack*, const ItemDef*) {
    auto* c = dynamic_cast<Creature*>(&e);
    return c && !c->ghost && useOnCreature(pc.lv, pc.p, *c, pc.p.sneaking);
  });
}

} // namespace dq
