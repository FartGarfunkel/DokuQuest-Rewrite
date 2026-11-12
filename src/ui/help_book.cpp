// ui/help_book.cpp — How to Play, written in the guidebook (ui/book.h): a bookmark per subject, the pages running on
// from one to the next. Opened from the title screen and the pause menu (makeHowToScreen).
#include "ui/ui.h"
#include "ui/book.h"
#include "client/app.h"
#include <imgui.h>

namespace dq {

namespace {
using namespace ui;

enum K { H, P, KEY, NUM, BUL, SUB };   // heading, paragraph, key row, numbered step, bullet, small heading
struct L { K k; const char* a; const char* b = ""; };
struct Section { const char* name; const char* icon; std::vector<L> lines; };

const std::vector<Section>& helpText() {
  static const std::vector<Section> S = {
  {"Welcome", "book", {
    {H, "Welcome to DokuQuest"},
    {P, "Every world here is one tall column: the Skylands float at the top, the Overworld is in the middle, the Nether lies under it and the End is at the very bottom. There are no loading screens between them \xE2\x80\x94 a portal is a window you walk straight through."},
    {SUB, "Your first days"},
    {NUM, "Punch a tree. Chop the base and the whole tree comes down."},
    {NUM, "Make a crafting table, then wooden and stone tools."},
    {NUM, "Smelt iron ore in a furnace (coal or wood burns as fuel) and make iron tools."},
    {NUM, "Build a shelter and light it before the first night: torches keep monsters from spawning."},
    {NUM, "Go deeper for diamonds, build an obsidian frame and light it to reach the Nether."},
    {NUM, "Fill a frame of glowstone with water to open the way up to the Skylands."},
    {NUM, "Find a stronghold, set twelve Eyes of Ender in its frames, and face the End."},
    {SUB, "Books you carry"},
    {P, "This guide is always in the pause menu. In the game, the menu tabs along the top (Q / E, or LB / RB) lead to your Journal, Skills, Spellbook, Stable and Achievements. Use a Travel Journal to see everything you've picked up so far and how each thing is made."},
  }},
  {"Keyboard & mouse", "compass", {
    {H, "Keyboard & mouse"},
    {KEY, "W A S D", "Move (double-tap W or hold Ctrl to sprint)"}, {KEY, "Space", "Jump \xC2\xB7 swim up \xC2\xB7 in Creative, double-tap to fly"},
    {KEY, "Shift", "Sneak: you won't walk off edges, and you chop one log at a time. Tap twice to crawl"},
    {KEY, "Left click", "Mine \xC2\xB7 attack. In Creative every click breaks a block"},
    {KEY, "Right click", "Place \xC2\xB7 use \xC2\xB7 eat \xC2\xB7 open"},
    {KEY, "Shift + right click", "Empty hand: pick up a chest, furnace or animal and carry it. Holding something: set it down"},
    {KEY, "1\xE2\x80\x93" "9 \xC2\xB7 wheel", "Choose a hotbar slot"},
    {KEY, "Tab", "Bring the next row of your bag into the hotbar"},
    {KEY, "R", "Placing mode: vertical slabs, upside-down stairs and trapdoors"},
    {KEY, "F", "Swap main hand and off hand"}, {KEY, "E", "Inventory and crafting"},
    {KEY, "Q", "Drop the item (Ctrl + Q: the whole stack)"},
    {KEY, "M", "Look at your map"}, {KEY, "J \xC2\xB7 K", "Journal \xC2\xB7 skills and perks"},
    {KEY, "B \xC2\xB7 G", "Spellbook \xC2\xB7 change spell"}, {KEY, "Z \xC2\xB7 N", "Cast the right hand's spell \xC2\xB7 the left hand's"},
    {KEY, "H", "Your stable (Shift + H calls or sends home your favourite mount)"},
    {KEY, "T \xC2\xB7 /", "Chat \xC2\xB7 commands (/help lists them)"},
    {KEY, "F1 \xC2\xB7 F2", "Hide the HUD \xC2\xB7 take a screenshot"},
    {KEY, "F3", "Debug information"}, {KEY, "F5 \xC2\xB7 V \xC2\xB7 X", "Camera: first person, over the shoulder, behind, in front \xC2\xB7 swap shoulder"},
    {KEY, "Esc", "Pause"},
    {SUB, "In the books"},
    {KEY, "\xE2\x86\x90 \xE2\x86\x92 \xC2\xB7 wheel", "Turn the pages"}, {KEY, "Tab \xC2\xB7 bookmark", "Jump to the next subject"},
    {P, "Every key can be changed in Options \xE2\x86\x92 Controls."},
  }},
  {"Controller", "spyglass", {
    {H, "Controller"},
    {KEY, "Sticks", "Move \xC2\xB7 look (click the left stick to sprint)"}, {KEY, "RT \xC2\xB7 LT", "Mine \xC2\xB7 use"},
    {KEY, "A \xC2\xB7 B", "Jump \xC2\xB7 sneak (tap B twice to crawl)"}, {KEY, "Y \xC2\xB7 X", "Inventory \xC2\xB7 drop"},
    {KEY, "LB \xC2\xB7 RB", "Hotbar (in menus: the menu tabs)"}, {KEY, "D-pad down", "Swap hands"},
    {KEY, "Sneak + LB / RB", "Cast a spell"},
    {KEY, "Start", "Pause \xC2\xB7 on a second controller, join split screen"},
    {SUB, "In the books"},
    {KEY, "D-pad \xE2\x86\x90 \xE2\x86\x92", "Turn the pages"}, {KEY, "X", "Next bookmark"}, {KEY, "D-pad \xE2\x86\x91 \xE2\x86\x93 \xC2\xB7 A", "Choose a spell or a companion \xC2\xB7 use it"},
    {KEY, "B", "Close"},
    {SUB, "Touch"},
    {KEY, "Left thumb", "Move (push to the top edge to sprint)"}, {KEY, "Drag right side", "Look around"}, {KEY, "Tap", "Place \xC2\xB7 use"},
    {KEY, "Press and hold", "Mine (or hold the Mine button)"},
  }},
  {"Surviving", "golden_apple", {
    {H, "Surviving"},
    {P, "There is no hunger. Eating any food heals you on the spot \xE2\x80\x94 apples, berries and roast boar are your lifeline. You have ten hearts. Heart Crystals (from bosses) add one for good; charms in the charm slot add more while you wear them, or guard against fire, water and falls."},
    {P, "Under water, watch your air bubbles. Sprint while under water to swim. If you die, your things wait in a grave and you're given a compass that points to it."},
    {SUB, "The night"},
    {P, "At night and in dark caves the monsters come out. Ghouls shamble at you, skeletons keep their distance and shoot, spiders climb walls and drop on you from cave roofs, and fuselings creep close, hiss and burst \xE2\x80\x94 back away when you hear one."},
    {P, "A good axe or sword can take a limb off: a ghoul without a leg crawls, a spider short of legs can't leap, a skeleton that loses its bow arm drops the bow."},
    {P, "Torches keep the dark away. Hold one in your off hand, or hang a torch or lantern on your belt hook and it lights everything around you as you walk."},
  }},
  {"Crafting & forging", "crafting_table", {
    {H, "Crafting"},
    {P, "Crafting works on the grid, the classic way: 2\xC3\x97" "2 in your inventory, 3\xC3\x97" "3 at a crafting table. Stuck? The recipe book beside the grid shows what you can make and can lay the ingredients out for you. A Travel Journal remembers everything you've carried and shows how each one is made."},
    {SUB, "The forge"},
    {P, "At the forge you make tool heads and blades from any metal or gem and fit them to handles, so one tool can mix materials. A tool or armour piece gains levels as you use it."},
    {SUB, "Metals and gems"},
    {BUL, "Copper, tin, iron, silver, gold, mithril and adamantite ores in the Overworld's rock; bronze and steel are alloys made from them."},
    {BUL, "Zanite (Skylands): the ore gives zanite gemstones \xE2\x80\x94 tools and armour a step past iron."},
    {BUL, "Gravitite (Skylands): smelt the ore into plates \xE2\x80\x94 as strong as diamond."},
    {BUL, "Thallasium (the End): smelt the ore into ingots \xE2\x80\x94 a little under iron, and light."},
    {BUL, "Ender ore gives ender shards, amber ore amber gems, and ambrosium ore ambrosium shards, which heal you."},
    {SUB, "The trommel"},
    {P, "Cobblestone round an iron mesh, over a row of logs. Fill its four slots with dirt, gravel, sand, clay, mud, soul sand, brimsand or sulphur, give it furnace fuel, and it sifts them one at a time: flint, clay, pebbles, olivine, quartz, crushed iron and gold, even ruby glass from brimsand. What it finds pops out of its left side, straight into a chest if you stand one there. Four pebbles make two cobblestone."},
    {SUB, "Workstations"},
    {P, "Anvils repair and join tools, the loom and smithing table dye and upgrade, the fletching table makes arrows, the stonecutter and sawmill cut blocks. A spyglass zooms; a compass and clock show their faces on screen when held."},
  }},
  {"The Nether", "netherrack", {
    {H, "The Nether"},
    {P, "Build a frame of obsidian (anything from 2\xC3\x97" "3 to 21\xC3\x97" "21) and light it with flint and steel, an Ember Igniter or fire. One step in the Nether is several in the Overworld."},
    {SUB, "Its seven lands"},
    {BUL, "Crags: ridges of netherrack and the odd tall spire."},
    {BUL, "Volcanic Islands: brimstone and brimsand islands with lava running between them."},
    {BUL, "Sulfur Pools: yellow brimstone round pools of acid. Acid eats at bare skin and wears your armour down \xE2\x80\x94 stay out."},
    {BUL, "Crystal Forest: great clear ruby-glass crystals growing out of the ground."},
    {BUL, "Crystal Plains: flat open ground between the crystals."},
    {BUL, "The Shelf: wide stepped terraces."},
    {BUL, "The Old World: grey ash dunes \xE2\x80\x94 what's left of a world that burned."},
    {SUB, "Who lives there"},
    {P, "Zombie pigmen walk in packs and leave you alone \xE2\x80\x94 until you hit one. Ghasts drift overhead; punch their fireballs back at them. Watch for magma and sulfur slimes, charred ghouls, scorched skeletons, crystal wisps among the crystals, ember fuselings on the volcanoes, and cinder wisps round the fortresses."},
    {SUB, "What to look for"},
    {P, "Fortresses, bastions, spires, ruined forges and bone pits stand out on the land. /locate finds the nearest one when commands are on."},
  }},
  {"The Skylands", "feather", {
    {H, "The Skylands"},
    {P, "A frame of glowstone filled with water from a bucket opens the way up. The Skylands are islands in the open sky \xE2\x80\x94 their waterfalls pour off the edges and vanish into the clouds far below."},
    {P, "Mine zanite for gemstones, gravitite for plates and ambrosium for its healing shards. Aether crystals make a Pocket Key to your own private dimension."},
    {P, "Don't fall: the Overworld is a long way down. Elytra let you glide down safely."},
  }},
  {"The End", "end_stone", {
    {H, "The End"},
    {P, "Strongholds hide deep in the Overworld. Their ring of twelve frames takes an Eye of Ender each; fill them all and the pool opens on the End."},
    {P, "The Ender Dragon circles the great pillars of the central island, healed by the crystals on top of them. Break the crystals first."},
    {P, "Beyond the central island lie the outer lands, with thallasium and ender ore, amber and strange forests. Elytra hang aboard the ships of the outer islands: wear them, jump from a height and glide."},
  }},
  {"Magic & skills", "apprentice_staff", {
    {H, "Skills"},
    {P, "Skills grow by doing: mining makes you a better miner, fighting a better fighter. Each level brings you closer to a perk. K shows them all."},
    {H, "Magic"},
    {P, "You start knowing Firebolt and Home Teleport. Press Z to cast your right hand's spell and N for your left's (with both at once, both hands); G changes spell. Any spell casts with bare hands; a staff makes it much stronger and quickens your mana (the Apprentice Staff: two sticks and an emerald, amethyst or lapis)."},
    {P, "Magic grows by casting, by beating foes with spells, and by learning tomes. Each tome needs a Magic level: one that's still too deep teaches you a little the first time you study it. The simpler tomes can be crafted from a book, lapis and what the spell is made of (coal for Firebolt, a snowball for Frost, glowstone for Light...); the rest come from librarians, towers and dungeons. Your Spellbook (B) lists every spell by school: pick one for your right hand, and right-click (or X on a controller) another for your left. Spells cost mana, which comes back on its own; using a spell ranks it up."},
    {P, "The enchanting table and the brewing stand are magic too: enchant gear with experience, brew potions from what you find."},
  }},
  {"Animals & companions", "saddle", {
    {H, "Farms and animals"},
    {P, "Cows, pigs, sheep and chickens roam the grasslands. Feed two of a kind (wheat, carrots or seeds) and they'll have a baby. Milk cows with a bucket, shear sheep, and carry an animal home with an empty hand while sneaking."},
    {P, "Bone meal ripens crops and saplings. Shovels make paths, hoes till soil, and scaffolding lets you climb and sneak back down."},
    {H, "Your stable"},
    {P, "Tame an animal, raise a drake from a dragon egg, or breed your pets, and they join your stable (H, or a whistle). Summon brings one to your side wherever it is; Dismiss sends it home. Mark your favourite mount with the star: Shift + H calls it or sends it home."},
  }},
  {"Redstone & rails", "redstone", {
    {H, "Redstone"},
    {P, "Mine redstone ore and lay the dust on the ground to carry power from levers, buttons, pressure plates, tripwires and torches. Power fades one step per block of dust; a repeater sends it on at full strength. Redstone opens doors, lights lamps, fires dispensers and droppers, and moves pistons."},
    {P, "Hoppers pass items between chests and furnaces. A comparator reads how full a container is. Observers pulse when the block they watch changes."},
    {H, "Rails"},
    {P, "Rails join up, climb slopes and turn corners as you lay them. Set a minecart on the track and right-click to ride; W gives a push, sneak to get out. Powered rails speed carts up, detector rails send a signal as a cart passes, activator rails tip riders out. Feed a furnace cart coal and it pushes itself."},
  }},
  {"Worlds & commands", "command_block", {
    {H, "World settings"},
    {P, "Each world has its own settings (World settings in the pause menu, or More options when you make one): its name, game mode, how long days and nights last, and game rules such as keep inventory, mob griefing and fire spread."},
    {P, "When an update brings new land, World settings \xE2\x86\x92 Upgrade lets an older world grow it in the places you haven't built on."},
    {H, "Commands"},
    {P, "Where commands are on (always in Creative), press T or / to type one: /time set day, /gamemode creative, /tp, /give, /fill, /locate and more. Builders get WorldEdit: //wand gives the wand, then //set, //replace, //walls, //copy, //paste, //rotate, //stack, //sphere and //undo."},
    {H, "Your worlds are safe"},
    {P, "The game saves every few seconds. Worlds keep working through updates, and you can make a backup from the pause menu at any time."},
  }},
  };
  return S;
}

class HelpBook : public Screen {
public:
  const char* id() const override { return "howto"; }
  bool pausesGame() const override { return true; }
  // (Esc / B close it: the app pops the top screen)
  void draw(App& a) override {
    if (a.topScreen() != this) return;
    const auto& S = helpText();
    std::vector<BookTab> tabs;
    for (auto& s : S) tabs.push_back({s.name, s.icon});
    book.begin(a, "##howto", tabs);
    if (book.pageW() != builtW) {   // laid out once for the page's width
      builtW = book.pageW(); rows.clear();
      for (auto& s : S) {
        BookRows r; int num = 0;
        for (auto& l : s.lines) {
          switch (l.k) {
            case H: book.heading(r, l.a); num = 0; break;
            case SUB: book.gap(r, 4 * Z()); book.sub(r, l.a); num = 0; break;
            case P: book.para(r, l.a); break;
            case KEY: book.keyRow(r, l.a, l.b); break;
            case BUL: book.bullet(r, l.a); break;
            case NUM: { std::string m = std::to_string(++num) + "."; book.bullet(r, l.a, ink::text, m.c_str()); break; }
          }
        }
        rows.push_back(std::move(r));
      }
    }
    book.draw(rows);
    book.hint(a.input.usingPad() ? "D-pad: turn the pages \xC2\xB7 X: next bookmark \xC2\xB7 B: close" : "\xE2\x86\x90 \xE2\x86\x92 or the wheel: turn the pages \xC2\xB7 Tab or a bookmark: next subject \xC2\xB7 Esc: close");
    book.end();
  }
private:
  Book book;
  float builtW = -1;
  std::vector<BookRows> rows;
};
} // namespace

std::unique_ptr<Screen> makeHowToScreen() { return std::make_unique<HelpBook>(); }

} // namespace dq
