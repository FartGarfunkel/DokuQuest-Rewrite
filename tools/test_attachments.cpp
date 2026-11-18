// tools/test_attachments.cpp — headless checks for attachments (world/attach.h, sim/attach_play.h): small parts that
// share a cell with its main block. Placing (and refusing by overlap), the ladder shaft with a torch on its wall, the
// trapdoor flipped up behind a chest that then can't swing down, light from a torch attachment, breaking each part on
// its own, a part popping off when its support goes, saving and loading (and old saves), a lever attachment powering.
// Run: build/test_attachments (exit code = failures)
#include "core/init.h"
#include "core/registry.h"
#include "sim/level.h"
#include "sim/player.h"
#include "sim/events.h"
#include "sim/controller.h"
#include "sim/physics.h"
#include "sim/redstone.h"
#include "sim/attach_play.h"
#include "world/attach.h"
#include "world/light.h"
#include "common/threadpool.h"
#include <thread>
#include <cstdio>
#include <cstring>

using namespace dq;
namespace dq {   // (sim/villagers_internal.h: the townsfolk's way finding)
struct NpcStep { ivec3 at{0}; float floor = 0; bool climb = false; vec2 c{0}; };
std::vector<NpcStep> npcPath(const World& w, vec3 from, vec3 to, float bodyW, float bodyH, bool* partial, int maxNodes, bool* snapped);
float npcStandAt(const World& w, int x, int y, int z, float bodyW, float bodyH);
}
static std::vector<NpcStep> npcPath(const World& w, vec3 from, vec3 to, float bw, float bh, bool* partial) { return dq::npcPath(w, from, to, bw, bh, partial, 2400, nullptr); }
static float npcStandAt(const World& w, int x, int y, int z) { return dq::npcStandAt(w, x, y, z, 0.3f, 1.8f); }
static int fails = 0, passes = 0;
static void check(bool ok, const std::string& what) { printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str()); (ok ? passes : fails)++; }

int main(int argc, char** argv) {
  std::string ud = (fs::temp_directory_path() / "dq-test-attachments").string();
  for (int i = 1; i < argc; i++) if (std::string(argv[i]) == "--userdir" && i + 1 < argc) ud = argv[++i];
  std::error_code ec; fs::remove_all(ud, ec);
  CoreOptions co; co.server = true; co.userDirOverride = ud;
  if (!initCore(co)) { printf("FAIL initCore\n"); return 1; }
  const Registry& R = reg();
  auto B = [&](const char* k) { return R.blockId(k); };

  printf("[which blocks]\n");
  check(attachable(B("torch")) && attachable(B("ladder")) && attachable(B("oak_trapdoor")) && attachable(B("lever")) && attachable(B("stone_button"))
        && attachable(B("oak_sign")) && attachable(B("item_frame")) && attachable(B("glow_lichen")) && attachable(B("stone_pressure_plate")),
        "torches, ladders, trapdoors, levers, buttons, signs, item frames, wall covers and plates may be attachments");
  check(!attachable(B("chest")) && !attachable(B("stone")) && !attachable(B("oak_fence")), "chests, whole blocks and fences are main blocks only");

  ThreadPool pool(2); EventBus bus;
  LevelInfo li; li.name = "AttachTest"; li.seedText = "attach"; li.worldType = "debug";
  auto lvp = Level::create(userSub("saves"), li, &pool, &bus);
  Level* L = lvp.get();
  World& w = L->world();
  const int Y = dimBase(DIM_OVERWORLD) + 200;
  Player* P; { auto p = std::make_unique<Player>(); p->name = "Tester"; p->mode = "survival"; p->pos = vec3(8.5f, Y + 1, 8.5f); P = static_cast<Player*>(L->spawn(std::move(p))); }
  { double t0 = nowSeconds();
    while (nowSeconds() - t0 < 60) {
      w.stream({{P->pos, 2, false}}, 20);
      bool all = true;
      for (int cx = -1; cx <= 2; cx++) for (int cz = -1; cz <= 1; cz++) if (!w.chunk(DIM_OVERWORLD, cx, cz)) all = false;
      if (all) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    } }
  check(w.chunk(DIM_OVERWORLD, 0, 0) != nullptr, "the test chunks load");
  const BlockId stone = B("stone"), ladder = B("ladder"), torch = B("torch"), chest = B("chest"), trap = B("oak_trapdoor"), lever = B("lever"), lamp = B("redstone_lamp");
  for (int x = -4; x < 30; x++) for (int z = -4; z < 14; z++) { w.setBlock(x, Y, z, stone); for (int y = Y + 1; y < Y + 8; y++) w.setBlock(x, y, z, AIR); }
  PlayerController pc(*L, *P);
  P->flying = true;
  auto tick = [&](int n) { for (int i = 0; i < n; i++) L->tick(0.05); if (auto* le = w.system<LightEngine>()) for (int k = 0; k < 4000 && le->busy(); k++) le->flush(w); };
  tick(2);
  std::vector<std::string> toasts;
  bus.on("toast", [&](const json& a) { if (a.is_array() && a.size() > 1 && a[1].is_string()) toasts.push_back(a[1].get<std::string>()); return true; });
  auto saw = [&](const std::string& m) { for (auto& t : toasts) if (t == m) return true; return false; };
  // stand at `from`, look at `at`, aim (the controller's own crosshair ray)
  auto aim = [&](vec3 from, vec3 at) {
    P->pos = from; P->vel = vec3(0);
    const vec3 eye = from + vec3(0, P->eye, 0), d = glm::normalize(at - eye);
    P->yaw = std::atan2(-d.x, -d.z); P->pitch = std::asin(d.y);
    pc.target = raycastBlocks(w, glm::dvec3(eye), lookVector(P->yaw, P->pitch), 6);
    return pc.target;
  };
  auto useWith = [&](const std::string& item) { P->held() = item.empty() ? ItemStack() : ItemStack(item, 8); pc.use(false, PlayerInput{}); };
  auto itemsNear = [&](vec3 at, const std::string& key) {
    int n = 0;
    for (auto& e : L->entities()) if (!e->removed && e->type == "item" && glm::distance(e->pos, at) < 2.5f) {
      const json& st = e->data.contains("stack") ? e->data["stack"] : e->data.value("item", json());
      if (st.is_object() && st.value("id", "") == key) n += st.value("n", 1);
    }
    return n;
  };
  auto clearItems = [&] { for (auto& e : L->entities()) if (e->type == "item") e->removed = true; tick(1); };

  printf("[the ladder shaft with a torch on its wall]\n");
  const ivec3 S(4, Y + 2, 4);
  for (int y = Y + 1; y <= Y + 5; y++) { w.setBlock(4, y, 3, stone); w.setBlock(5, y, 4, stone); w.setBlock(3, y, 4, stone); }
  RayHit h = aim(vec3(4.5f, Y + 1, 6.5f), vec3(4.5f, S.y + 0.5f, 4.0f));
  check(h.hit && h.block == ivec3(4, S.y, 3) && h.normal == ivec3(0, 0, 1), "looking at the shaft's north wall");
  useWith("ladder");
  check(w.getBlock(S.x, S.y, S.z) == ladder && !w.hasAttachments(S.x, S.y, S.z), "a ladder on the north wall is the cell's main block, as ever");
  for (int y = Y + 1; y <= Y + 4; y++) if (y != S.y) w.setBlock(4, y, 4, ladder, w.getMeta(S.x, S.y, S.z));   // the rest of the shaft
  h = aim(vec3(4.5f, Y + 1, 6.5f), vec3(5.0f, S.y + 0.5f, 4.6f));
  check(h.hit && h.block == ivec3(5, S.y, 4) && h.normal == ivec3(-1, 0, 0), "looking past the ladder at the east wall of its cell");
  useWith("torch");
  const auto* parts = w.attachments(S.x, S.y, S.z);
  check(parts && parts->size() == 1 && (*parts)[0].id == torch && ((*parts)[0].meta & 7) == 2, "the torch goes on the east wall in the ladder's cell, as an attachment");
  check(w.getBlock(S.x, S.y, S.z) == ladder, "the ladder stays the main block (getBlock)");
  tick(2);
  check((w.getLight(S.x, S.y, S.z) & 15) >= 13, "the torch attachment lights the cell (" + std::to_string(w.getLight(S.x, S.y, S.z) & 15) + ")");
  check((w.getLight(S.x, S.y + 2, S.z) & 15) >= 11, "and the shaft round it");
  check(placeAttachment(*L, P, w, S.x, S.y, S.z, torch, 3) == 0 && saw("There's no room for it there"), "a torch on the north wall would be inside the ladder: refused");
  check(placeAttachment(*L, P, w, S.x, S.y, S.z, torch, 4) == 0 && saw("It needs something to hold on to"), "a torch toward the open south side is refused for want of support");
  check(placeAttachment(*L, P, w, S.x, S.y, S.z, torch, 1) == 1 && w.attachments(S.x, S.y, S.z)->size() == 2, "a second torch on the west wall fits too");
  w.removeAttachment(S.x, S.y, S.z, 1);
  // the crosshair picks each part's own box
  h = aim(vec3(4.5f, Y + 1, 6.5f), vec3(4.5f, S.y + 0.5f, 4.05f));
  check(h.hit && h.block == S && h.part == -1 && h.id == ladder, "aiming at the ladder picks the ladder");
  { std::vector<BoxF> bx; partWorldBoxes(w, S.x, S.y, S.z, 0, ShapePurpose::Select, bx);
    const vec3 tc = (bx[0].lo + bx[0].hi) * 0.5f;
    h = aim(vec3(4.5f, Y + 1, 6.5f), tc);
    check(h.hit && h.block == S && h.part == 0 && h.id == torch, "aiming at the torch picks the torch");
    check(bx.size() == 1 && bx[0].hi.x - bx[0].lo.x < 0.5f && bx[0].hi.y - bx[0].lo.y < 0.8f, "and its outline is the torch's own box, not the cell"); }
  check(!collides(w, S.x + 0.3, S.y + 0.1, S.z + 0.3, S.x + 0.7, S.y + 0.9, S.z + 0.7) && collides(w, S.x + 0.1, S.y + 0.1, S.z + 0.01, S.x + 0.9, S.y + 0.9, S.z + 0.15), "you bump into the ladder's box only (the torch has none)");
  { Entity e; e.pos = vec3(S.x + 0.5f, S.y, S.z + 0.5f); e.w = 0.3f; e.h = 1.8f; check(sampleContacts(w, e).ladder, "you climb the ladder"); }

  printf("[breaking each part on its own]\n");
  clearItems();
  h = aim(vec3(4.5f, Y + 1, 6.5f), [&] { std::vector<BoxF> bx; partWorldBoxes(w, S.x, S.y, S.z, 0, ShapePurpose::Select, bx); return (bx[0].lo + bx[0].hi) * 0.5f; }());
  pc.breakBlock(S.x, S.y, S.z); tick(1);
  check(!w.hasAttachments(S.x, S.y, S.z) && w.getBlock(S.x, S.y, S.z) == ladder, "breaking the torch takes the torch only: the ladder stays");
  check(itemsNear(vec3(S) + vec3(0.5f), "torch") == 1 && itemsNear(vec3(S) + vec3(0.5f), "ladder") == 0, "and drops a torch");
  check(placeAttachment(*L, P, w, S.x, S.y, S.z, torch, 2) == 1, "the torch goes back");
  clearItems();
  h = aim(vec3(4.5f, Y + 1, 6.5f), vec3(4.5f, S.y + 0.5f, 4.05f));
  pc.breakBlock(S.x, S.y, S.z); tick(1);
  check(w.getBlock(S.x, S.y, S.z) == torch && (w.getMeta(S.x, S.y, S.z) & 7) == 2 && !w.hasAttachments(S.x, S.y, S.z), "breaking the ladder leaves the torch (it takes the cell)");
  check(itemsNear(vec3(S) + vec3(0.5f), "ladder") == 1 && itemsNear(vec3(S) + vec3(0.5f), "torch") == 0, "and drops a ladder");
  w.setBlock(S.x, S.y, S.z, ladder, w.getMeta(S.x, S.y + 1, S.z));
  check(w.getBlock(S.x, S.y, S.z) == ladder, "the ladder back in");
  placeAttachment(*L, P, w, S.x, S.y, S.z, torch, 2);
  clearItems();
  printf("[support]\n");
  w.setBlock(5, S.y, 4, AIR); tick(1);
  check(!w.hasAttachments(S.x, S.y, S.z) && w.getBlock(S.x, S.y, S.z) == ladder, "the east wall gone, the torch on it pops off; the ladder stays");
  check(itemsNear(vec3(S) + vec3(0.5f), "torch") == 1, "as an item");
  w.setBlock(5, S.y, 4, stone);
  placeAttachment(*L, P, w, S.x, S.y, S.z, torch, 2);

  printf("[saving]\n");
  {
    Chunk* c = w.chunkAtBlock(S.x, S.y, S.z);
    c->savedAt = 1234;
    std::vector<u8> blob = encodeChunk(*c);
    Chunk back(c->dim, c->cx, c->cz);
    check(decodeChunk(back, blob.data(), blob.size()), "a chunk with attachments saves and loads");
    auto it = back.attach.find(colIdx(S.x & 15, S.y & 255, S.z & 15));
    check(it != back.attach.end() && it->second.size() == 1 && it->second[0].id == torch && it->second[0].meta == 2, "the torch comes back as an attachment of the ladder's cell");
    check(back.get(S.x & 15, S.y & 255, S.z & 15) == ladder, "beside its ladder");
    // a chunk without attachments saves exactly as before (no new fields), and an old save loads with none
    Chunk plain(c->dim, c->cx + 5, c->cz);
    plain.set(1, 10, 1, stone); plain.savedAt = 1;
    std::vector<u8> a = encodeChunk(plain);
    plain.attach[colIdx(1, 11, 1)].push_back({torch, 0, json()});
    plain.attach.clear();
    std::vector<u8> b = encodeChunk(plain);
    check(a == b, "a chunk with no attachments saves byte for byte as before");
    std::vector<u8> raw;
    u32 rs = a[4] | a[5] << 8 | a[6] << 16 | (u32)a[7] << 24;
    zDecompress(a.data() + 8, a.size() - 8, raw, rs);
    check(std::string((const char*)raw.data(), raw.size()).find("\"at\"") == std::string::npos, "(no attachment fields in it)");
    Chunk old(c->dim, c->cx + 5, c->cz);
    check(decodeChunk(old, a.data(), a.size()) && old.attach.empty() && old.get(1, 10, 1) == stone, "an old chunk loads unchanged, with no attachments");
  }
  { // through the region store: save the world, forget the chunk, load it again
    w.saveDirty(true); w.flush();
    Chunk* c = w.chunkAtBlock(S.x, S.y, S.z);
    std::vector<u8> blob = encodeChunk(*c);
    Chunk back(c->dim, c->cx, c->cz);
    decodeChunk(back, blob.data(), blob.size());
    check(back.attach.size() == c->attach.size(), "every attachment of the chunk saved");
  }

  printf("[the trapdoor flipped up behind a chest]\n");
  const ivec3 T(10, Y + 1, 4);
  for (int y = Y + 1; y <= Y + 3; y++) w.setBlock(T.x, y, T.z - 1, stone);
  h = aim(vec3(T.x + 0.5f, Y + 1, T.z + 3.5f), vec3(T.x + 0.5f, T.y + 0.3f, T.z + 0.0f));
  check(h.hit && h.block == ivec3(T.x, T.y, T.z - 1), "looking at the wall where the trapdoor goes");
  useWith("oak_trapdoor");
  check(w.getBlock(T.x, T.y, T.z) == trap, "the trapdoor hangs on the wall (shut, on the floor half)");
  h = aim(vec3(T.x + 0.5f, Y + 1, T.z + 3.5f), vec3(T.x + 0.5f, T.y + 0.1f, T.z + 0.5f));
  useWith("");
  check(w.getBlock(T.x, T.y, T.z) == trap && (w.getMeta(T.x, T.y, T.z) & 4), "flipped up against the wall");
  h = aim(vec3(T.x + 0.5f, Y + 1, T.z + 3.5f), vec3(T.x + 0.5f, T.y + 0.0f, T.z + 0.7f));
  check(h.hit && h.block == ivec3(T.x, T.y - 1, T.z) && h.normal == ivec3(0, 1, 0), "looking at the floor of its cell, in front of it");
  useWith("chest");
  parts = w.attachments(T.x, T.y, T.z);
  check(w.getBlock(T.x, T.y, T.z) == chest && parts && parts->size() == 1 && (*parts)[0].id == trap && ((*parts)[0].meta & 4),
        "the chest goes in the cell: the trapdoor steps aside, flipped up behind it (" + R.block(w.getBlock(T.x, T.y, T.z)).key + ")");
  check(w.blockEntity(T.x, T.y, T.z) && w.blockEntity(T.x, T.y, T.z)->value("type", "") == "chest", "the chest has its contents record");
  {
    RayHit th; th.hit = true; th.block = T; th.part = 0; th.id = trap; th.normal = ivec3(0, 0, 1); th.point = vec3(T) + vec3(0.5f, 0.5f, 0.1f);
    pc.target = th; toasts.clear();
    const u8 m0 = (*w.attachments(T.x, T.y, T.z))[0].meta;
    useWith("");
    check((*w.attachments(T.x, T.y, T.z))[0].meta == m0 && saw("It's blocked"), "it can't swing down onto the chest: \"It's blocked\"");
  }
  check(placeAttachment(*L, P, w, T.x, T.y, T.z, torch, 0) == 0, "no torch on the floor under the chest");
  // a second chest can't go in the trapdoor's cell any other way round
  w.setBlock(T.x, T.y, T.z, AIR);
  check(w.getBlock(T.x, T.y, T.z) == trap && !w.hasAttachments(T.x, T.y, T.z), "with the chest gone the trapdoor holds the cell again");
  { const u8 m = w.getMeta(T.x, T.y, T.z); RayHit th; th.hit = true; th.block = T; th.part = -1; th.id = trap; th.normal = ivec3(0, 0, 1); pc.target = th; useWith(""); check(!(w.getMeta(T.x, T.y, T.z) & 4) && m != w.getMeta(T.x, T.y, T.z), "and swings down freely"); }

  printf("[a lever attachment powers]\n");
  const ivec3 V(16, Y + 1, 4);
  w.setBlock(V.x, V.y, V.z - 1, stone);
  w.setBlock(V.x, V.y, V.z, ladder, 0);
  w.setBlock(V.x + 1, V.y, V.z, lamp);
  tick(4);
  h = aim(vec3(V.x + 0.5f, Y + 1, V.z + 3.5f), vec3(V.x + 1.0f, V.y + 0.5f, V.z + 0.6f));
  check(h.hit && h.block == ivec3(V.x + 1, V.y, V.z) && h.normal == ivec3(-1, 0, 0), "looking at the lamp's west face, past the ladder");
  useWith("lever");
  parts = w.attachments(V.x, V.y, V.z);
  check(parts && parts->size() == 1 && (*parts)[0].id == lever, "the lever goes on the lamp in the ladder's cell");
  tick(4);
  check(w.getBlock(V.x + 1, V.y, V.z) == lamp, "the lamp is dark while the lever is off");
  { std::vector<BoxF> bx; partWorldBoxes(w, V.x, V.y, V.z, 0, ShapePurpose::Select, bx); h = aim(vec3(V.x + 0.5f, Y + 1, V.z + 3.5f), (bx[0].lo + bx[0].hi) * 0.5f); }
  check(h.part == 0 && h.id == lever, "aiming at the lever");
  useWith("");
  tick(6);
  check(w.attachments(V.x, V.y, V.z) && ((*w.attachments(V.x, V.y, V.z))[0].meta & 8), "pulled on");
  check(R.block(w.getBlock(V.x + 1, V.y, V.z)).key == "redstone_lamp_on", "the lamp lights from the lever attachment (" + R.block(w.getBlock(V.x + 1, V.y, V.z)).key + ")");
  useWith(""); tick(12);
  check(w.getBlock(V.x + 1, V.y, V.z) == lamp, "and goes out when it's pulled off");
  // a trapdoor attachment swings with power: a floor torch holds the cell, the trapdoor shut against its ceiling
  {
    const ivec3 Q(22, Y + 1, 4);
    w.setBlock(Q.x, Q.y, Q.z - 1, stone);
    w.setBlock(Q.x, Q.y, Q.z, torch, 0);
    check(placeAttachment(*L, P, w, Q.x, Q.y, Q.z, trap, 0 | 8) == 1, "a trapdoor (shut, top half) shares a floor torch's cell");
    w.setBlock(Q.x + 2, Q.y, Q.z, stone);
    w.setBlock(Q.x + 1, Q.y, Q.z, lever, (2 + 1) | 8);   // a lever on the stone east of the cell, on
    tick(6);
    const auto* qp = w.attachments(Q.x, Q.y, Q.z);
    check(qp && ((*qp)[0].meta & 4), "a lever beside the cell powers the trapdoor attachment open");
    w.setBlock(Q.x + 1, Q.y, Q.z, lever, 2 + 1); tick(6);
    qp = w.attachments(Q.x, Q.y, Q.z);
    check(qp && !((*qp)[0].meta & 4), "and shut again when it's off");
  }

  printf("[explosions and mods]\n");
  check(w.hasAttachments(S.x, S.y, S.z), "(the shaft's torch is there)");
  dropAllAttachments(*L, w, S.x, S.y, S.z, false);
  check(!w.hasAttachments(S.x, S.y, S.z) && w.getBlock(S.x, S.y, S.z) == ladder, "a blast takes the parts off a cell");

  printf("[townsfolk's way finding sees the parts as you find them]\n");
  {
    const BlockId planks = B("planks"), button = B("stone_button"), plate = B("stone_pressure_plate");
    for (int x = 31; x < 47; x++) for (int z = 10; z < 31; z++) { w.setBlock(x, Y, z, stone); for (int y = Y + 1; y < Y + 10; y++) w.setBlock(x, y, z, AIR); }
    // a body's footing as the planner gives it is where you'd stand: on something, nothing in the way above
    auto standsTrue = [&](int x, int y, int z) {
      const float f = npcStandAt(w, x, y, z);
      if (std::isnan(f)) return false;
      return collides(w, x + 0.3, f - 0.06, z + 0.3, x + 0.7, f - 0.01, z + 0.7) && !collides(w, x + 0.3, f + 0.01, z + 0.3, x + 0.7, f + 1.8, z + 0.7);
    };
    // a shaft with a torch on its west wall and the ladder put up after it: in two cells the torch is the main block and
    // the ladder an attachment beside it
    for (int y = Y + 1; y <= Y + 9; y++) for (int x = 33; x <= 39; x++) w.setBlock(x, y, 19, stone);   // (the wall)
    for (int x = 33; x <= 39; x++) for (int z = 14; z <= 18; z++) w.setBlock(x, Y + 6, z, planks);   // (the loft)
    for (int y = Y + 1; y <= Y + 5; y++) { w.setBlock(35, y, 18, stone); w.setBlock(37, y, 18, stone); }
    for (int y = Y + 1; y <= Y + 6; y++) w.setBlock(36, y, 18, ladder, 2);
    for (int y : {Y + 2, Y + 4}) {
      w.setBlock(36, y, 18, torch, 1);
      check(attachRoomFree(w, 36, y, 18, ladder, 2) && w.addAttachment(36, y, 18, ladder, 2), "a ladder attachment beside a wall torch in the shaft (y+" + std::to_string(y - Y) + ")");
    }
    { Entity e; e.pos = vec3(36.5f, Y + 2, 18.5f); e.w = 0.3f; e.h = 1.8f; check(sampleContacts(w, e).ladder, "you climb the ladder attachment"); }
    bool part = true;
    auto way = npcPath(w, vec3(36.5f, Y + 1, 12.5f), vec3(36.5f, Y + 7, 15.5f), 0.3f, 1.8f, &part);
    int climbs = 0; for (auto& s : way) climbs += s.climb;
    check(!part && climbs >= 4, "a townsperson's way goes up the shaft past the torch (" + std::to_string(climbs) + " rungs" + (part ? ", partial" : "") + ")");
    way = npcPath(w, vec3(36.5f, Y + 7, 15.5f), vec3(36.5f, Y + 1, 12.5f), 0.3f, 1.8f, &part);
    check(!part, "and back down it");
    // a walkway with a pressure plate, a torch and a button in it: nothing there you'd bump into
    for (int x = 31; x < 47; x++) for (int y = Y + 1; y <= Y + 3; y++) { w.setBlock(x, y, 23, stone); w.setBlock(x, y, 25, stone); }
    w.setBlock(37, Y + 1, 24, plate, 0);
    w.addAttachment(37, Y + 1, 24, torch, 3);
    w.setBlock(40, Y + 2, 24, torch, 4);
    int bm = -1; for (int m = 0; m < 8 && bm < 0; m++) if (attachSupported(w, 40, Y + 2, 24, button, (u8)m) && attachRoomFree(w, 40, Y + 2, 24, button, (u8)m)) bm = m;
    check(bm >= 0 && w.addAttachment(40, Y + 2, 24, button, (u8)bm), "a button beside a torch at head height in the walkway");
    way = npcPath(w, vec3(32.5f, Y + 1, 24.5f), vec3(45.5f, Y + 1, 24.5f), 0.3f, 1.8f, &part);
    bool straight = !way.empty(); for (auto& s : way) if (s.at.z != 24 || s.floor > Y + 1.2f) straight = false;
    check(!part && straight, "a townsperson walks straight along it (" + std::to_string(way.size()) + " steps" + (part ? ", partial" : "") + ")");
    check(standsTrue(37, Y + 1, 24) && standsTrue(40, Y + 1, 24), "and stands where you would by the parts");
    // a hatch: a closed trapdoor attachment (top half, flush with the floor) over a hole in a walkway, a torch on the
    // hole's wall its main block
    for (int x = 31; x < 47; x++) for (int y = Y + 1; y <= Y + 3; y++) { w.setBlock(x, y, 27, stone); w.setBlock(x, y, 29, stone); }
    for (int y = Y - 3; y <= Y; y++) for (int x = 37; x <= 39; x++) for (int z = 27; z <= 29; z++) w.setBlock(x, y, z, stone);
    for (int y = Y - 2; y <= Y; y++) w.setBlock(38, y, 28, AIR);
    w.setBlock(38, Y, 28, torch, 1);
    check(attachRoomFree(w, 38, Y, 28, trap, 8) && w.addAttachment(38, Y, 28, trap, 8), "a shut trapdoor over the hole beside the torch");
    check(!collides(w, 38.3, Y + 1.01, 28.3, 38.7, Y + 2.8, 28.7) && collides(w, 38.3, Y + 0.9, 28.3, 38.7, Y + 0.99, 28.7), "you walk on it");
    check(standsTrue(38, Y + 1, 28) && std::fabs(npcStandAt(w, 38, Y + 1, 28) - (Y + 1)) < 0.01f, "a townsperson stands on it as you do (" + std::to_string(npcStandAt(w, 38, Y + 1, 28) - Y) + ")");
    way = npcPath(w, vec3(32.5f, Y + 1, 28.5f), vec3(45.5f, Y + 1, 28.5f), 0.3f, 1.8f, &part);
    straight = !way.empty(); for (auto& s : way) if (s.floor < Y + 0.9f) straight = false;
    check(!part && straight, "and walks across it, not down the hole (" + std::to_string(way.size()) + " steps" + (part ? ", partial" : "") + ")");
    w.setAttachmentMeta(38, Y, 28, 0, 8 | 4);
    check(std::isnan(npcStandAt(w, 38, Y + 1, 28)) && !collides(w, 38.3, Y + 0.5, 28.3, 38.7, Y + 0.99, 28.7), "open, it's a hole to them as to you");
    w.setAttachmentMeta(38, Y, 28, 0, 0);   // (shut, the bottom half: a step down into the hatch's cell)
    check(standsTrue(38, Y, 28) && std::fabs(npcStandAt(w, 38, Y, 28) - (Y + 3 / 16.f)) < 0.01f, "shut at the bottom, they stand on it down in the cell, as you do");
  }

  printf("\n%d passed, %d failed\n", passes, fails);
  return fails;
}
