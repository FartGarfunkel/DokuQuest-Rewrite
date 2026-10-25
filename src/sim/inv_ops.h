// sim/inv_ops.h — what clicking, dragging and shift-clicking in an inventory screen does (ui/invui.js
// slotClick, dragAdd, quickMove, hotbarSwap, takeOutput, bookPick, creativePick), as pure rules on a Player
// and the block entity being looked at, so a server can replay and validate them later.
//
// Slot groups follow the original's data-g names: main (0-8 hotbar, 9-35 bag), armor, off, back (backpack
// slot), hook (belt hook), charm, cape, bp (the worn backpack's contents), grid (crafting), cont (chest,
// barrel, backpack, ender chest...), furn (furnace / smoker / blast furnace: 0 input, 1 fuel, 2 output),
// out (the crafting result); mach (a millstone / sawmill / crusher: 0 input, 1 output), sta (a work station's
// two inputs), sout (its result), cut (the stonecutter's input).
// Storage: main/armor/off live on Player; back/hook/charm/cape/ender are stack arrays in Player::data
// (keys "back", "hook", "charm", "cape", "ender"); a backpack's contents are its stack's tag "c"; containers
// are block entities in the original's format ({type:'chest', items:[...]}, {type:'furnace', items:[in,
// fuel,out], burn, burnMax, cook}, backpack_block {stack}).
#pragma once
#include "sim/inventory.h"   // maxStackOf, addToSlots (shared with the gameplay code)
#include "sim/player.h"
#include "sim/crafting.h"
#include "sim/stations.h"

namespace dq {

class World;

enum class Grp : u8 { Main, Armor, Off, Back, Hook, Charm, Cape, Bp, Grid, Cont, Furn, Out, Mach, Sta, SOut, Cut, Count };
const char* grpName(Grp g);
bool stacksMerge(const ItemStack& a, const ItemStack& b);   // the original's canMerge

// Container.add: as much as fits (merging first, then empty slots); returns what's left (s.n is updated)


struct InvOpen {
  std::string mode = "inventory";   // inventory | table | furnace | container | station | stonecutter | machine
  std::string kind;                 // station: anvil | grindstone | smithing | loom | fletching
  bool hasPos = false; ivec3 pos{0};
  bool ender = false;               // the ender chest (player's own 27 slots)
  bool heldPack = false;            // the backpack in your hand
  bool single = false;              // a chest that must not join its neighbour
};

class InvSession {
public:
  InvSession(Player& p, World* w, InvOpen o, bool creative);
  Player& p; World* w; InvOpen o;
  bool creative = false;
  int W = 2;
  std::vector<ItemStack> grid;
  std::vector<std::string> ghost;   // recipe-book ghost ingredients on the grid ("" = none)
  int bookSel = -1;
  CraftMatch match;                 // what the grid makes now (refresh())

  // container info (refresh())
  std::string title;
  bool contIsPack = false;          // backpack contents: no backpacks inside
  std::string furnaceKind;          // furnace | smoker | blast
  StationResult staRes;             // station: what the two inputs make now
  std::vector<std::pair<std::string, int>> cutOpts;   // stonecutter: what the input cuts into
  std::string machKind;             // machine: millstone | sawmill | crusher
  float machProgress = 0;
  bool valid() const { return ok; }

  void refresh();                   // re-read every stored group (block entities, Player::data); recompute match
  std::vector<ItemStack>* arr(Grp g);
  int groupSize(Grp g);
  const ItemStack* at(Grp g, int i);   // the stack shown in a slot (out = the crafting result)
  bool accepts(Grp g, int i, const ItemStack& s) const;

  void slotClick(Grp g, int i, bool right, bool shift);
  bool dragTarget(Grp g, int i);
  void dragBegin(Grp g, int i, bool right);   // a press on a slot while holding something
  void dragAdd(Grp g, int i);                 // the mouse passed over another slot
  void dragEnd();                             // release: a plain left click with no drag places/swaps as before
  bool dragging() const { return drag.active; }
  void hotbarSwap(Grp g, int i, int k);
  void quickMove(Grp g, int i);
  int takeOutput(bool shift);
  void creativePick(const std::string& key, bool all);
  // work stations: take the result (false = nothing taken; anvilBroke set when the anvil wore away)
  bool takeStation(bool shift, bool* anvilBroke = nullptr);
  void cutPick(int option, bool all);   // the stonecutter's list
  void trashCursor();
  void bookPick(int recipeIdx, bool all, bool instant);   // instant: controller / touch (fill on first pick)
  void dropFromSlot(Grp g, int i, bool all);              // Q over a slot
  void throwCursor(int n);                                // click outside the panel
  void close();                     // grid and cursor go back to the bag (or are thrown)
  void giveBack(const ItemStack& s);
  int bagAdd(ItemStack& s);         // main, then the worn backpack (PlayerInventory.add)
  Bags bags();

  // effects the caller turns into sounds, drops, toasts
  std::function<void(const ItemStack&)> onThrow;            // drop this stack in front of the player
  std::function<void(const std::string&)> onSound;          // click, equip, craft
  std::function<bool(const ItemStack&)> onCraft;            // false cancels (EventBus "craft")
  std::function<void(const std::string&)> onRareCraft;      // first time a rare item was made
  std::function<void(Grp)> onChanged;                       // a stored group changed (block entity saved)

private:
  bool ok = true;
  std::vector<ItemStack> cont, furn, back, hook, charm, cape, bp, ender, mach, sta, cut;
  int contN = 0; bool dbl = false; ivec3 posA{0}, posB{0}; int nA = 0;
  struct Drag { bool active = false, right = false; ItemStack orig; std::vector<std::pair<Grp, int>> slots; std::vector<ItemStack> base; } drag;
  void store(Grp g);
  void changed(Grp g);
  void sound(const char* s) { if (onSound) onSound(s); }
  std::string takeOne(const std::string& key);
  json* entity(ivec3 pos, bool create, const json& init);
};

} // namespace dq
