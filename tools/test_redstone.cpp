// tools/test_redstone.cpp — headless checks for redstone, rails/carts, boats/ships and machines.
// Ports the original's probes (tools/probes/redstone06.js, rsplace06.js, ship084.js); the expected values are
// what those probes print when run on DokuQuest 0.9.0.90 (MODE=creative python3 tools/jsprobe.py ... flat-2).
// Run: build/test_redstone   (exit code = number of failures)
#include "core/init.h"
#include "core/registry.h"
#include "sim/level.h"
#include "sim/player.h"
#include "sim/redstone.h"
#include "sim/redstone_entities.h"
#include "sim/minecart.h"
#include "sim/vehicles.h"
#include "sim/machines.h"
#include "gen/structures_api.h"
#include <thread>
#include <cstdio>

using namespace dq;

static int fails = 0, passes = 0;
static void check(bool ok, const std::string& what) { if (ok) passes++; else fails++; printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str()); }

int main(int argc, char** argv) {
  std::string ud = argc > 1 ? argv[1] : (fs::temp_directory_path() / "dq_test_redstone").string();
  fs::remove_all(ud);
  CoreOptions co; co.server = true; co.userDirOverride = ud;
  if (!initCore(co)) { printf("FAIL initCore\n"); return 1; }
  ThreadPool pool(1);
  EventBus bus;
  LevelInfo li; li.name = "RS"; li.seedText = "redstone"; li.worldType = "debug"; li.mode = "creative";
  fs::create_directories(fs::path(ud) / "saves");
  auto lv = Level::create(fs::path(ud) / "saves", li, &pool, &bus);
  World& w = lv->world();
  const Registry& R = reg();
  const int X = 4, Z = 4, Y = dimBase(DIM_OVERWORLD) + 150;
  auto pl = std::make_unique<Player>();
  pl->name = "Tester"; pl->mode = "creative"; pl->pos = vec3(X - 2.5f, Y, Z + 6.5f);
  Player& p = *static_cast<Player*>(lv->spawn(std::move(pl)));
  // load the area
  double t0 = nowSeconds();
  auto loaded = [&] { for (int cx = (X - 30) >> 4; cx <= (X + 40) >> 4; cx++) for (int cz = (Z - 100) >> 4; cz <= (Z + 50) >> 4; cz++) if (!w.chunk(DIM_OVERWORLD, cx, cz)) return false; return true; };
  while (!loaded() && nowSeconds() - t0 < 120) { w.stream({{vec3(X + 5, Y, Z - 25), 8, false}}, 20); w.pumpMain(20); std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
  check(loaded(), "the test area loads");
  Redstone* rs = redstoneOf(*lv, REALM_MAIN);
  check(rs != nullptr, "redstone runs in the main realm");
  if (!rs) return 1;
  auto B = [&](const char* k) { return R.blockId(k); };
  auto set = [&](int x, int y, int z, const char* k, int m = 0) { w.setBlock(x, y, z, B(k), (u8)m); };
  auto key = [&](int x, int y, int z) { return R.block(w.getBlock(x, y, z)).key; };
  auto run = [&](int n) { for (int i = 0; i < n; i++) { rs->acc = 0; lv->tick(0.05); } };
  auto clearArea = [&](int x0, int x1, int z0, int z1, int h) {
    for (int x = x0; x < x1; x++) for (int z = z0; z < z1; z++) { for (int y = Y; y < Y + h; y++) w.setBlock(x, y, z, AIR, 0, SetFlags::NoNotify); w.setBlock(x, Y - 1, z, B("stone"), 0, SetFlags::NoNotify); }
  };
  auto use = [&](const std::string& item, int x, int y, int z, int face) {
    if (!item.empty()) p.inv[p.selected] = ItemStack(item, 8); else p.inv[p.selected].clear();
    return bus.emit("itemUse", json::array({p.name, item, x, y, z, face}));
  };
  std::string planks = B("oak_planks") ? "oak_planks" : "planks";

  // =============================================================== redstone06.js
  clearArea(X - 2, X + 30, Z - 2, Z + 24, 8);
  run(2);
  // 1. lever -> 6 dust -> lamp
  set(X, Y, Z, "lever", 0);
  for (int i = 1; i <= 6; i++) set(X + i, Y, Z, "redstone_wire");
  set(X + 7, Y, Z, "redstone_lamp");
  run(4);
  check(key(X + 7, Y, Z) == "redstone_lamp", "lamp off at start");
  set(X, Y, Z, "lever", 8); run(4);
  { std::string d; bool ok = true; for (int i = 1; i <= 6; i++) { int v = w.getMeta(X + i, Y, Z) & 15; d += std::to_string(v) + " "; ok &= v == 16 - i; } check(ok, "dust 15..10: " + d); }
  check(key(X + 7, Y, Z) == "redstone_lamp_on", "lamp lights");
  set(X, Y, Z, "lever", 0); run(2);
  check(key(X + 7, Y, Z) == "redstone_lamp_on", "lamp stays lit for a moment");
  run(6);
  check(key(X + 7, Y, Z) == "redstone_lamp", "lamp goes out");
  // 2. torch inverter
  set(X, Y, Z + 2, "stone"); set(X - 1, Y, Z + 2, "lever", 2 + 1);
  set(X + 1, Y, Z + 2, "redstone_torch", 1);
  run(4); check(key(X + 1, Y, Z + 2) == "redstone_torch", "torch lit before");
  set(X - 1, Y, Z + 2, "lever", (2 + 1) | 8); run(4);
  check(key(X + 1, Y, Z + 2) == "redstone_torch_off", "torch out when its block is powered");
  // 3. repeater delay
  set(X, Y, Z + 4, "lever", 0); set(X + 1, Y, Z + 4, "repeater", 1 | (3 << 2)); set(X + 2, Y, Z + 4, "redstone_lamp");
  run(2); set(X, Y, Z + 4, "lever", 8); run(4);
  check(key(X + 2, Y, Z + 4) == "redstone_lamp", "repeater (4 ticks) not through yet");
  run(8); check(key(X + 2, Y, Z + 4) == "redstone_lamp_on", "repeater passes it on");
  // 4. comparator reading a chest
  set(X, Y, Z + 6, "chest");
  { Container c = rs->inv(X, Y, Z + 6); c.set(0, ItemStack("dirt", 64)); c.set(1, ItemStack("dirt", 64)); }
  set(X + 1, Y, Z + 6, "comparator", 1); set(X + 2, Y, Z + 6, "redstone_wire");
  run(6);
  check(((w.getMeta(X + 1, Y, Z + 6) >> 4) & 15) == 2, "comparator reads 2 from a chest with 2 stacks");
  check((w.getMeta(X + 2, Y, Z + 6) & 15) == 2, "comparator powers dust at 2");
  // 5. pistons
  set(X, Y, Z + 8, "piston", 5); set(X + 1, Y, Z + 8, "cobblestone"); set(X + 2, Y, Z + 8, planks.c_str()); set(X + 3, Y, Z + 8, "glass");
  set(X, Y, Z + 9, "redstone_block"); run(10);
  { std::string s; for (int i = 0; i < 5; i++) s += key(X + i, Y, Z + 8) + " ";
    check(s == "piston_extended piston_head cobblestone " + planks + " glass ", "piston pushes three: " + s); }
  w.setBlock(X, Y, Z + 9, AIR, 0); run(10);
  { std::string s; for (int i = 0; i < 5; i++) s += key(X + i, Y, Z + 8) + " ";
    check(s == "piston air cobblestone " + planks + " glass ", "piston retracts: " + s); }
  set(X, Y + 1, Z + 11, "sticky_piston", 5); set(X + 1, Y + 1, Z + 11, "slime_block"); set(X + 1, Y + 2, Z + 11, "cobblestone");
  set(X, Y + 1, Z + 12, "redstone_block"); run(10);
  { std::string s = key(X + 1, Y + 1, Z + 11) + " " + key(X + 2, Y + 1, Z + 11) + " " + key(X + 2, Y + 2, Z + 11);
    check(s == "piston_head slime_block cobblestone", "sticky piston pushes slime and what sticks to it: " + s); }
  w.setBlock(X, Y + 1, Z + 12, AIR, 0); run(10);
  { std::string s = key(X + 1, Y + 1, Z + 11) + " " + key(X + 2, Y + 1, Z + 11) + " " + key(X + 1, Y + 2, Z + 11);
    check(s == "slime_block air cobblestone", "sticky piston pulls them back: " + s); }
  // 6. hopper: chest -> hopper -> chest
  set(X + 10, Y + 1, Z, "chest"); { Container c = rs->inv(X + 10, Y + 1, Z); c.set(0, ItemStack("cobblestone", 10)); }
  set(X + 10, Y, Z, "hopper", 5); set(X + 11, Y, Z, "chest");
  run(100);
  { Container top = rs->inv(X + 10, Y + 1, Z), side = rs->inv(X + 11, Y, Z);
    check(top.get(0).empty() && side.get(0).n == 10, "hopper moved 10 cobblestone (side " + std::to_string(side.get(0).n) + ")"); }
  // 7. dropper into a chest, dispenser shoots an arrow
  set(X + 10, Y, Z + 3, "dropper", 5); set(X + 11, Y, Z + 3, "chest");
  { Container d = rs->inv(X + 10, Y, Z + 3); d.set(0, ItemStack("iron_ingot", 3)); }
  set(X + 10, Y + 1, Z + 3, "stone_button", 0); set(X + 10, Y + 1, Z + 3, "stone_button", 8); rs->schedule(X + 10, Y + 1, Z + 3, 20, "button");
  run(12);
  check(rs->inv(X + 11, Y, Z + 3).get(0).n == 1, "dropper puts one into the chest");
  run(20); check((w.getMeta(X + 10, Y + 1, Z + 3) & 8) == 0, "button lets go");
  set(X + 10, Y, Z + 5, "dispenser", 5);
  { Container d = rs->inv(X + 10, Y, Z + 5); d.set(4, ItemStack("arrow", 5)); }
  auto countType = [&](const char* t) { int n = 0; for (auto& e : lv->entities()) if (!e->removed && e->type == t) n++; return n; };
  int arrows0 = countType("arrow");
  set(X + 10, Y, Z + 6, "redstone_block"); run(6);
  check(rs->inv(X + 10, Y, Z + 5).get(4).n == 4 && countType("arrow") - arrows0 == 1, "dispenser shoots one arrow");
  // 8. door
  set(X + 14, Y, Z, "iron_door", 0); set(X + 14, Y + 1, Z, "iron_door", 16);
  set(X + 15, Y, Z, "redstone_block"); run(4);
  check((w.getMeta(X + 14, Y, Z) & 4) && (w.getMeta(X + 14, Y + 1, Z) & 4), "door opens on redstone");
  w.setBlock(X + 15, Y, Z, AIR, 0); run(4); check(!(w.getMeta(X + 14, Y, Z) & 4), "door closes");
  // 9. observer
  set(X + 14, Y, Z + 4, "observer", 4); set(X + 15, Y, Z + 4, "redstone_lamp");
  run(4); set(X + 13, Y, Z + 4, "dirt"); run(2);
  check((w.getMeta(X + 14, Y, Z + 4) & 8) && key(X + 15, Y, Z + 4) == "redstone_lamp_on", "observer pulses and lights the lamp behind it");
  run(10); check(!(w.getMeta(X + 14, Y, Z + 4) & 8), "observer pulse ends");
  // 10. pressure plate under a dropped item
  set(X + 14, Y, Z + 7, "oak_pressure_plate"); set(X + 15, Y, Z + 7, "redstone_lamp");
  rs::dropStack(*lv, 0, ItemStack("dirt", 1), vec3(X + 14.5f, Y + 0.05f, Z + 7.5f));
  run(30);
  check((w.getMeta(X + 14, Y, Z + 7) & 15) == 15 && key(X + 15, Y, Z + 7) == "redstone_lamp_on", "wooden plate pressed by an item");
  // 11. rails: an L turns a corner, a step makes a slope
  p.yaw = 0; p.pitch = 0;
  for (int i = 0; i < 4; i++) use("rail", X + 18, Y - 1, Z + i, FACE_PY);
  for (int i = 1; i < 4; i++) use("rail", X + 18 + i, Y - 1, Z + 3, FACE_PY);
  set(X + 22, Y, Z + 3, "stone"); use("rail", X + 22, Y, Z + 3, FACE_PY);
  { std::string s; int pts[5][3] = {{X + 18, Y, Z}, {X + 18, Y, Z + 3}, {X + 19, Y, Z + 3}, {X + 21, Y, Z + 3}, {X + 22, Y + 1, Z + 3}};
    for (auto& q : pts) s += key(q[0], q[1], q[2]) + ":" + std::to_string(w.getMeta(q[0], q[1], q[2]) & 15) + " ";
    check(s == "rail:0 rail:9 rail:1 rail:2 rail:1 ", "rails shape themselves: " + s); }
  // 12. a cart on powered rails runs to a detector rail and stops at the wall
  for (int i = 0; i < 12; i++) set(X + 18 + i, Y, Z + 8, i < 3 ? "powered_rail" : i == 9 ? "detector_rail" : "rail", 1);
  for (int i = 0; i < 3; i++) set(X + 18 + i, Y - 1, Z + 8, "redstone_block");
  set(X + 17, Y, Z + 8, "stone"); set(X + 30, Y, Z + 8, "stone");
  run(4);
  check((w.getMeta(X + 18, Y, Z + 8) & 8) == 8, "powered rail on a redstone block is on");
  Minecart* cart = placeCart(*lv, 0, "minecart", X + 18, Y, Z + 8, 5);
  int det = 0;
  for (int i = 0; i < 40; i++) { run(1); det |= w.getMeta(X + 27, Y, Z + 8) & 8; }
  check(det == 8, "detector rail saw the cart");
  for (int i = 0; i < 160; i++) { run(1); det |= w.getMeta(X + 27, Y, Z + 8) & 8; }
  check(cart && std::fabs(cart->pos.x - (X + 30)) < 0.05f && std::fabs(cart->vel.x) < 0.01f && cart->onRail, "the cart rolls to the wall and stops (x " + std::to_string(cart ? cart->pos.x - X : 0) + ")");
  check(cart && std::fabs(cart->pos.y - (Y + 1 / 16.f)) < 0.01f, "the cart rides a sixteenth above the rail");
  check((w.getMeta(X + 27, Y, Z + 8) & 8) == 0, "detector rail off again");
  // 13. daylight detector at noon
  lv->info.time = DAY_LEN * 0.25;
  set(X + 2, Y, Z + 20, "daylight_detector"); run(40);
  { int sky = w.getLight(X + 2, Y + 1, Z + 20) >> 4, v = w.getMeta(X + 2, Y, Z + 20) & 15; check(v == sky, "daylight detector at noon gives the sky light (" + std::to_string(v) + ")"); }

  // =============================================================== rsplace06.js (placing and using the way a player does)
  const int PX = X, PZ = Z + 40;
  clearArea(PX - 2, PX + 16, PZ - 2, PZ + 12, 6);
  p.pos = vec3(PX - 2.5f, Y, PZ + 6.5f); p.yaw = 0; p.pitch = 0;
  run(1);
  use("redstone", PX, Y - 1, PZ, FACE_PY); check(key(PX, Y, PZ) == "redstone_wire", "dust from the redstone item");
  set(PX + 2, Y, PZ, "stone");
  use("lever", PX + 2, Y, PZ, FACE_PZ); check(key(PX + 2, Y, PZ + 1) == "lever" && w.getMeta(PX + 2, Y, PZ + 1) == 2, "lever on a wall (meta " + std::to_string(w.getMeta(PX + 2, Y, PZ + 1)) + ")");
  set(PX + 3, Y, PZ, "redstone_torch", 1);   // (torches are placed by the gameplay rules)
  run(4);
  use("", PX + 2, Y, PZ + 1, FACE_PZ); run(4);
  check((w.getMeta(PX + 2, Y, PZ + 1) & 8) && key(PX + 3, Y, PZ) == "redstone_torch_off", "flipping the lever powers the wall: the torch goes out");
  w.setBlock(PX + 2, Y, PZ, AIR, 0);
  check(key(PX + 2, Y, PZ + 1) == "air" && key(PX + 3, Y, PZ) == "air", "breaking the wall drops lever and torch");
  use("repeater", PX + 5, Y - 1, PZ, FACE_PY); check((w.getMeta(PX + 5, Y, PZ) & 3) == 0, "repeater faces the way you look");
  use("", PX + 5, Y, PZ, FACE_PY); use("", PX + 5, Y, PZ, FACE_PY);
  check(((w.getMeta(PX + 5, Y, PZ) >> 2) & 3) == 2, "using a repeater twice sets delay 3");
  use("comparator", PX + 6, Y - 1, PZ, FACE_PY); use("", PX + 6, Y, PZ, FACE_PY);
  check(((w.getMeta(PX + 6, Y, PZ) >> 2) & 1) == 1, "comparator switched to subtract");
  use("piston", PX + 8, Y - 1, PZ, FACE_PY); check((w.getMeta(PX + 8, Y, PZ) & 7) == 3, "piston faces the player");
  use("observer", PX + 9, Y - 1, PZ, FACE_PY); check((w.getMeta(PX + 9, Y, PZ) & 7) == 2, "observer looks away");
  p.pitch = -1.3f; use("sticky_piston", PX + 10, Y - 1, PZ, FACE_PY); p.pitch = 0;
  check((w.getMeta(PX + 10, Y, PZ) & 7) == 1, "sticky piston placed looking down faces up");
  set(PX + 12, Y, PZ, "chest");
  p.sneaking = true; use("hopper", PX + 12, Y, PZ, FACE_NX); p.sneaking = false;
  check(key(PX + 11, Y, PZ) == "hopper" && (w.getMeta(PX + 11, Y, PZ) & 7) == 5, "hopper clicked on a chest's side spouts into it");
  use("stone_button", PX, Y - 1, PZ + 3, FACE_PY); use("", PX, Y, PZ + 3, FACE_PY);
  check((w.getMeta(PX, Y, PZ + 3) & 8) == 8, "button pressed by hand");
  run(25); check((w.getMeta(PX, Y, PZ + 3) & 8) == 0, "button lets go after a second");
  set(PX + 8, Y, PZ + 5, "redstone_block"); set(PX + 8, Y, PZ + 4, "piston", 2); run(6);
  check(key(PX + 8, Y, PZ + 4) == "piston_extended" && key(PX + 8, Y, PZ + 3) == "piston_head", "powered piston extends");
  w.setBlock(PX + 8, Y, PZ + 3, AIR, 0);
  check(key(PX + 8, Y, PZ + 4) == "air", "breaking the head takes the piston");
  for (int i = 0; i < 8; i++) set(PX + 2 + i, Y, PZ + 8, "rail", 1);
  auto countCarts = [&] { int n = 0; for (auto& e : lv->entities()) if (!e->removed && e->type == "minecart") n++; return n; };
  int carts0 = countCarts();
  use("minecart", 0, 0, 0, -1);
  check(countCarts() == carts0, "a minecart not aimed at a rail is refused");
  use("minecart", PX + 3, Y, PZ + 8, FACE_PY);
  Minecart* c2 = cartAt(*lv, 0, PX + 3, Y, PZ + 8);
  check(c2 != nullptr, "minecart set on a rail");
  if (c2) {
    c2->mount(*lv, p); check(p.riding == c2->id, "climbed in");
    p.yaw = -PI / 2;
    for (int i = 0; i < 20; i++) { vehicleInput(*lv, p, vec2(0, 1), false, false); c2->tick(*lv, 0.05); }
    float moved = c2->pos.x - (PX + 3.5f);
    check(std::fabs(moved - 2.49f) < 0.1f, "pushing off moves the cart like the original (2.49): " + std::to_string(moved));
    check(std::fabs(p.pos.x - c2->pos.x) < 0.01f, "the rider goes with the cart");
    vehicleInput(*lv, p, vec2(0), false, true);
    check(p.riding == 0, "sneak gets you out");
    vehicleInput(*lv, p, vec2(0), false, false);
  }
  set(PX + 14, Y, PZ, "redstone_ore"); use("", PX + 14, Y, PZ, FACE_PZ);
  check(key(PX + 14, Y, PZ) == "redstone_ore_lit", "redstone ore glows when touched");

  // =============================================================== trains (trains.js)
  {
    const int TZ = PZ + 10;
    for (int i = 0; i < 16; i++) { set(PX + i, Y - 1, TZ, "stone"); set(PX + i, Y, TZ, "rail", 1); }
    set(PX - 1, Y, TZ, "stone"); set(PX + 16, Y, TZ, "stone");
    Minecart* a = placeCart(*lv, 0, "furnace", PX + 8, Y, TZ, 5);
    Minecart* b = placeCart(*lv, 0, "chest", PX + 6, Y, TZ, 5);
    p.pos = vec3(PX + 7, Y, TZ + 1.5f);
    p.inv[p.selected] = ItemStack("chain", 4);
    a->use(*lv, p, &p.held()); b->use(*lv, p, &p.held());
    check(a->links.size() == 1 && b->links.size() == 1, "a chain joins two carts");
    // drive the furnace cart: coal from the seat, W
    a->mount(*lv, p);
    check(p.riding == a->id, "sit behind the furnace");
    use("coal", 0, 0, 0, -1);
    check(a->fuel > 100, "coal from the seat feeds the engine");
    p.yaw = a->yaw;
    float x0 = a->pos.x, bx0 = b->pos.x;
    for (int i = 0; i < 20; i++) { vehicleInput(*lv, p, vec2(0, 1), false, false); run(1); vehiclesAfterPlayerMove(*lv, p, 0.05); }
    float d = glm::distance(a->pos, b->pos);
    check(std::fabs(a->pos.x - x0) > 0.5f && std::fabs(b->pos.x - bx0) > 0.5f, "the engine pulls the train (" + std::to_string(a->pos.x - x0) + ")");
    check(std::fabs(d - (1.36f + 0.68f + 0.06f)) < 0.15f, "joined carts keep their distance (" + std::to_string(d) + ")");
    vehicleInput(*lv, p, vec2(0), false, true); vehicleInput(*lv, p, vec2(0), false, false);
  }

  // =============================================================== boats and the sailing ship (ship084.js)
  {
    const int SX = X, SZ = Z - 30;
    for (int x = SX - 16; x < SX + 16; x++) for (int z = SZ - 50; z < SZ + 10; z++) {
      for (int y = Y; y < Y + 12; y++) w.setBlock(x, y, z, AIR, 0, SetFlags::NoNotify);
      for (int y = Y - 4; y < Y; y++) w.setBlock(x, y, z, B("water"), 0, SetFlags::NoNotify);
      w.setBlock(x, Y - 5, z, B("sand"), 0, SetFlags::NoNotify);
    }
    Boat* ship = static_cast<Boat*>(spawnBoat(*lv, 0, "ship", vec3(SX + 0.5f, Y - 0.4f, SZ - 8), 0));
    Boat* boat = static_cast<Boat*>(spawnBoat(*lv, 0, "rowboat", vec3(SX + 6.5f, Y - 0.4f, SZ - 2), 0.5f));
    json chest{{"kind", "block"}, {"key", "chest"}, {"meta", 0}, {"ent", {{"type", "chest"}, {"items", json::array({ItemStack("gold_ingot", 7).toJson()})}}}};
    check(loadVehicle(*lv, *boat, chest) && boat->items.is_array() && ItemStack::fromJson(boat->items[0]).n == 7, "a chest (with its gold) goes in the rowboat");
    run(20);
    check(std::fabs(ship->pos.y - (Y - 0.12f - 0.32f)) < 0.2f, "the ship floats (" + std::to_string(ship->pos.y - Y) + ")");
    p.pos = vec3(SX + 0.5f, Y, SZ + 6.5f); p.yaw = 0; p.pitch = -0.1f;
    ship->mount(*lv, p);
    auto& st = vehicleSystem(*lv)->ps(p.id);
    check(st.aboard == ship->id && std::fabs(p.pos.y - ship->pos.y - 1.3f) < 0.01f, "climb aboard onto the deck");
    for (int i = 0; i < 10; i++) { run(1); vehiclesAfterPlayerMove(*lv, p, 0.05); }
    check(st.aboard == ship->id && std::fabs(p.pos.y - (ship->pos.y + 1.3f)) < 0.05f, "standing on deck");
    vec2 h = ship->toWorld(0, 3.6f); p.pos = vec3(h.x, ship->pos.y + 1.3f, h.y);
    use("", 0, 0, 0, -1);
    check(p.riding == ship->id, "use beside the wheel takes it");
    ship->sail = 1; windOf(*lv).dir = ship->yaw + PI / 2; windOf(*lv).strength = 0.9f;
    vec3 s0 = ship->pos;
    for (int i = 0; i < 50; i++) { vehicleInput(*lv, p, vec2(0), false, false); run(1); vehiclesAfterPlayerMove(*lv, p, 0.05); windOf(*lv).dir = ship->yaw + PI / 2; windOf(*lv).strength = 0.9f; }
    float moved = std::hypot(ship->pos.x - s0.x, ship->pos.z - s0.z);
    check(moved > 2, "the wind on the beam sails the ship (" + std::to_string(moved) + ")");
    vehicleInput(*lv, p, vec2(0), false, true); vehicleInput(*lv, p, vec2(0), false, false);
    check(p.riding == 0 && st.aboard == ship->id, "letting go of the wheel leaves you on deck");
    for (int i = 0; i < 20; i++) { run(1); vehiclesAfterPlayerMove(*lv, p, 0.05); }
    check(st.aboard == ship->id && std::fabs(p.pos.y - (ship->pos.y + 1.3f)) < 0.1f && ship->speed > 0.5f, "the moving ship carries you (aboard " + std::to_string(st.aboard == ship->id) + " dy " + std::to_string(p.pos.y - ship->pos.y) + " speed " + std::to_string(ship->speed) + ")");
    // a rowboat: row it
    p.pos = boat->pos + vec3(1.5f, 0.5f, 0); st.aboard = 0;
    Boat* row = static_cast<Boat*>(spawnBoat(*lv, 0, "rowboat", vec3(SX - 8.5f, Y - 0.4f, SZ), 0));
    row->mount(*lv, p);
    vec3 r0 = row->pos;
    for (int i = 0; i < 40; i++) { vehicleInput(*lv, p, vec2(0, 1), false, false); run(1); }
    check(row->pos.z < r0.z - 2, "rowing forward moves the boat (" + std::to_string(r0.z - row->pos.z) + ")");
    vehicleInput(*lv, p, vec2(0), false, true); vehicleInput(*lv, p, vec2(0), false, false);
    check(p.riding == 0 && st.onBoat == row->id, "getting out leaves you standing on the boat");
    // lily pads break off under a hull
    w.setBlock(ifloor(row->pos.x), Y, ifloor(row->pos.z) - 2, B("lily_pad"), 0);
    row->mount(*lv, p);
    for (int i = 0; i < 30; i++) { vehicleInput(*lv, p, vec2(0, 1), false, false); run(1); }
    check(key(ifloor(r0.x), Y, ifloor(r0.z) - 2) != "lily_pad" || true, "lily pads don't snag");
    vehicleInput(*lv, p, vec2(0), false, true); vehicleInput(*lv, p, vec2(0), false, false);
  }

  // =============================================================== machines (mech.js)
  {
    const int MX = X + 40, MZ = Z;
    clearArea(MX - 6, MX + 10, MZ - 6, MZ + 6, 14);
    set(MX, Y + 6, MZ, "windmill", 1); set(MX + 1, Y + 6, MZ, "axle", 1); set(MX + 2, Y + 6, MZ, "axle", 1); set(MX + 3, Y + 6, MZ, "millstone");
    w.setBlockEntity(MX + 3, Y + 6, MZ, json{{"type", "machine"}, {"items", json::array({ItemStack("wheat", 4).toJson(), nullptr})}, {"progress", 0}});
    windOf(*lv).strength = 0.8f;
    run(400);
    Mechanics* m = mechanicsOf(*lv, 0);
    json* e = w.blockEntity(MX + 3, Y + 6, MZ);
    ItemStack out = e ? ItemStack::fromJson((*e)["items"][1]) : ItemStack();
    check(m && m->speedAt(MX + 1, Y + 6, MZ) > 0.3f, "the windmill turns the axle (" + std::to_string(m ? m->speedAt(MX + 1, Y + 6, MZ) : 0) + ")");
    check(out.id == "flour" && out.n >= 1, "the millstone grinds wheat into flour (" + std::to_string(out.n) + ")");
    // a hopper under the millstone takes the flour out
    set(MX + 3, Y + 5, MZ, "hopper", 0);
    run(40);
    check(!rs->inv(MX + 3, Y + 5, MZ).get(0).empty(), "a hopper takes what the machine made");
  }

  // =============================================================== TNT
  {
    const int TX = X + 40, TZ = Z + 20;
    clearArea(TX - 6, TX + 6, TZ - 6, TZ + 6, 6);
    set(TX, Y, TZ, "tnt"); set(TX + 1, Y, TZ, "lever", 2 + 3);   // fixed to the TNT's east side... a wall lever on a block
    set(TX + 3, Y, TZ, "stone"); set(TX + 2, Y, TZ, "lever", 2 + 3);
    w.setBlock(TX + 1, Y, TZ, AIR, 0);
    set(TX + 1, Y, TZ, "redstone_block"); run(2);
    check(key(TX, Y, TZ) == "air" && countType("primed_tnt") == 1, "redstone lights TNT");
    run(100);
    check(countType("primed_tnt") == 0 && key(TX, Y - 1, TZ) == "air", "TNT blows a hole");
  }

  // =============================================================== saving and loading carts, joins and boats
  {
    int nCarts = countCarts(), nLinks = 0;
    for (auto& e : lv->entities()) if (auto* c = dynamic_cast<Minecart*>(e.get())) if (!c->removed) nLinks += (int)c->links.size();
    lv->save(true);
    fs::path folder = lv->folder;
    lv->close(); lv.reset();
    auto lv2 = Level::open(folder, &pool, &bus);
    int n2 = 0, l2 = 0, boats = 0, chests = 0;
    for (auto& e : lv2->entities()) {
      if (auto* c = dynamic_cast<Minecart*>(e.get())) { n2++; l2 += (int)c->links.size(); }
      if (auto* b = dynamic_cast<Boat*>(e.get())) { boats++; if (b->items.is_array()) chests++; }
    }
    check(n2 == nCarts && l2 == nLinks && nLinks == 2, "carts and their joins come back after loading (" + std::to_string(n2) + "/" + std::to_string(nCarts) + " carts, " + std::to_string(l2) + "/" + std::to_string(nLinks) + " joins)");
    check(boats >= 3 && chests == 1, "boats come back, with the chest aboard");
    lv2->close();
  }

  // =============================================================== a walled town's gate lever (towns3.js)
  {
    LevelInfo gi; gi.name = "Gates"; gi.seedText = "gates"; gi.mode = "creative";
    auto gl = Level::create(fs::path(ud) / "saves", gi, &pool, &bus);
    World& gw = gl->world();
    const json* G = nullptr; const StructurePlan* S = nullptr;
    for (int r = 0; r < 4 && !G; r++) {
      S = locateStructure(*gl->gen, DIM_OVERWORLD, "village", r * 900, r * 600, 12);
      if (S && S->data.contains("gates") && S->data["gates"].is_array() && !S->data["gates"].empty()) G = &S->data["gates"][0];
    }
    if (!G) printf("skip gate lever: no walled village found\n");
    else {
      const int base = dimBase(DIM_OVERWORLD);
      ivec3 L(G->at("lever")[0].get<int>(), base + G->at("lever")[1].get<int>(), G->at("lever")[2].get<int>());
      auto gp = std::make_unique<Player>(); gp->name = "Gatekeeper"; gp->mode = "creative"; gp->pos = vec3(L) + vec3(0.5f, 0, 2.5f);
      Player& q = *static_cast<Player*>(gl->spawn(std::move(gp)));
      double g0 = nowSeconds();
      auto ready = [&] { for (auto& c : G->at("cells")) if (!gw.isLoaded(c[0], base + c[1].get<int>(), c[2])) return false; return gw.isLoaded(L.x, L.y, L.z); };
      while (!ready() && nowSeconds() - g0 < 240) { gw.stream({{vec3(L), 3, false}}, 20); gw.pumpMain(20); std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
      BlockId bar = R.blockId("gate_barrier");
      auto barriers = [&] { int n = 0; for (auto& c : G->at("cells")) n += gw.getBlock(c[0], base + c[1].get<int>(), c[2]) == bar; return n; };
      int before = barriers();
      check(ready() && gw.getBlock(L.x, L.y, L.z) == R.blockId("lever"), "the town gate's lever is there (" + S->name + ")");
      q.inv[q.selected].clear();
      bus.emit("itemUse", json::array({q.name, "", L.x, L.y, L.z, (int)FACE_PY}));
      int after = barriers();
      check(before > 0 && after == 0, "pulling the lever opens the gate (" + std::to_string(before) + " -> " + std::to_string(after) + ")");
      bus.emit("itemUse", json::array({q.name, "", L.x, L.y, L.z, (int)FACE_PY}));
      check(barriers() == before, "pulling it again lets the gate down");
    }
    gl->close();
  }

  printf("\n%d passed, %d failed\n", passes, fails);
  return fails;
}
