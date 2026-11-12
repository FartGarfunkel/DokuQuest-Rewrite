// sim/travel_journal.cpp — the Travel Journal (BTA's guidebook): everything a player has carried is noted in their
// data["found"]; using the journal opens it (ui/travel_journal.cpp shows each thing and how it's made).
#include "sim/gameplay.h"
#include "sim/controller.h"
#include "sim/player.h"

namespace dq {

void openContentScreen(Level& lv, const Player& p, const std::string& id, const json& args);   // sim/content.cpp

void registerTravelJournal() {
  static bool done = false;
  if (done) return;
  done = true;
  hooks().useItem.push_back([](PlayerController& pc, ItemStack&, const ItemDef& def, bool repeat) {
    if (repeat || def.key != "travel_journal") return false;
    openContentScreen(pc.lv, pc.p, "travel_journal", json::object());
    return true;
  });
  registerLevelSystem([](Level&) -> std::shared_ptr<LevelSystem> {
    struct S : LevelSystem {
      double t = 0;
      const char* name() const override { return "travel-journal"; }
      void tick(Level& lv, double dt) override {
        if (lv.isRemote() || (t += dt) < 1) return;
        t = 0;
        for (auto& e : lv.entities()) {
          auto* p = dynamic_cast<Player*>(e.get());
          if (!p || p->removed) continue;
          if (!p->data.is_object()) p->data = json::object();
          json& found = p->data["found"];
          if (!found.is_object()) found = json::object();
          for (auto* list : {&p->inv, &p->offhand, &p->armor, &p->back, &p->charm})
            for (const ItemStack& s : *list) if (!s.empty() && !found.contains(s.id)) found[s.id] = 1;
        }
      }
    };
    return std::make_shared<S>();
  });
}

} // namespace dq
