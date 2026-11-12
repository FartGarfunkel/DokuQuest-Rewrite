// render/lectern_layer.cpp — a lectern's book (game/entities.js blockEntityDraws 'lectern', 0.9.0.65): a plain book
// lies tilted on top; an old book ("lore" key) lies open at the page it was left on (page chosen in
// ui/lectern_screen.cpp): the title and page number on the left, the words on the right, drawn here as voxel text.
#include "render/item_layers.h"
#include "render/be_scan.h"
#include "assets/lore.h"
#include "assets/voxel_paint.h"
#include "render/journal_model.h"
#include "render/content_draw.h"
#include "ui/book_pages.h"
#include <map>

namespace dq {
namespace {

// the open book as a voxel model: leather cover, two parchment pages, ink lines (a different pattern per page)
const VoxelModel& openBook(const std::string& key, int page) {
  static auto& cache = *new std::map<std::string, std::unique_ptr<VoxelModel>>();
  std::string k = key + "#" + std::to_string(page);
  auto it = cache.find(k);
  if (it != cache.end()) return *it->second;
  if (cache.size() > 48) cache.clear();
  const int W = 26, H = 3, D = 13;
  VoxelGrid g(ivec3(W, H, D));
  u32 cover = voxRGBA(92, 58, 30), paper = voxRGBA(239, 226, 192), paperB = voxRGBA(225, 208, 168), ink = voxRGBA(59, 42, 24), spine = voxRGBA(150, 112, 70);
  for (int z = 0; z < D; z++) for (int x = 0; x < W; x++) g.set(x, 0, z, cover);
  for (int z = 1; z < D - 1; z++) for (int x = 1; x < W - 1; x++) {
    bool gutter = x == W / 2 - 1 || x == W / 2;
    g.set(x, 1, z, gutter ? spine : ((x + z) & 1 ? paper : paperB));
    g.set(x, 2, z, gutter ? paperB : paper);
  }
  u32 h = 2166136261u;
  for (char ch : key) h = (h ^ (u8)ch) * 16777619u;
  h = (h ^ (u32)page * 2654435761u) * 16777619u;
  auto rnd = [&] { h ^= h << 13; h ^= h >> 17; h ^= h << 5; return h; };
  const LoreBook* b = loreBook(key);
  int pages = b ? (int)b->pages.size() : 1;
  // left page: the title (two heavy lines), the page number as dots, a rule
  for (int x = 2; x <= 10; x++) { g.set(x, 2, 2, ink); if (x < 9) g.set(x, 2, 3, ink); }
  for (int i = 0; i <= page && i < 5; i++) g.set(2 + i * 2, 2, 6, ink);
  for (int i = 0; i < pages - 1 - page && i < 5; i++) g.set(2 + i * 2, 2, 8, voxShade(ink, 1.9f));
  for (int x = 2; x <= 10; x++) g.set(x, 2, 10, voxShade(ink, 1.7f));
  // right page: lines of words
  for (int z = 2; z <= 10; z += 2) {
    int len = 6 + (int)(rnd() % 6);
    for (int x = 0; x < len; x++) if ((rnd() % 7) != 0 || x == 0) g.set(14 + x, 2, z, ink);
  }
  auto m = std::make_unique<VoxelModel>();
  VoxelMeshData md; buildVoxelMesh(g, md);
  m->gl.upload(md);
  m->frame.voxel = 1.f / 32; m->frame.origin = -vec3(W, H, D) * (0.5f / 32);
  m->size = g.size; m->kind = VoxelModel::Flat; m->built = true; m->key = "lectern_book";
  return *(cache[k] = std::move(m));
}

class LecternLayer : public RenderLayer {
public:
  const char* name() const override { return "lectern-books"; }
  int order() const override { return 120; }
  void drawOpaque(GameSession& s, const FrameCtx& f) override {
    if (!s.player) return;
    World& w = s.world();
    VoxelRenderer& VR = voxelRenderer();
    bool begun = false;
    static const float TURN[4] = {0, -PI / 2, PI, PI / 2};
    forBlockEntities(w, f, 40.f, [&](const Chunk& c, json& e, int x, int y, int z) {
      if (e.value("type", "") != "lectern" || !e.value("book", false)) return;
      vec3 rel = f.cam.rel(glm::dvec3(x, y, z));
      if (glm::dot(rel, rel) > 40.f * 40.f || !f.cam.frustum.sphere(rel + vec3(0.5f), 1.f)) return;
      int meta = w.getMeta(x, y, z) & 3;
      u8 l = w.getLight(x, y + 1, z);
      VoxelLight L{(l >> 4) / 15.f, (l & 15) / 15.f};
      mat4 m = glm::translate(mat4(1), rel);
      BookText bt;
      if (bookTextOfLectern(e, bt)) {   // a book with words: open on the slope at its page, as a real book (render/journal_model.h)
        if (!begun) { VR.begin(f, f.cam.viewProj); begun = true; }
        // toward its reader: meta is the way they looked setting it down (sim/build.cpp lookDir: 0 N, 1 E, 2 S, 3 W), so
        // they stand on the other side of it
        static const vec3 READER[4] = {vec3(0, 0, 1), vec3(-1, 0, 0), vec3(0, 0, -1), vec3(1, 0, 0)};
        const vec3 d = READER[meta];
        const vec3 xr = glm::cross(-d, vec3(0, 1, 0));                // across the pages, as the reader sees them
        const vec3 zn = glm::normalize(vec3(0, 1, 0) + d * 0.55f);    // out of the pages (the slope)
        const vec3 yu = glm::cross(zn, xr);
        // (on the top, its boards clear of it: the cover's thickness and the curl of the pages above the lectern)
        mat4 F(1); F[0] = vec4(xr, 0); F[1] = vec4(yu, 0); F[2] = vec4(zn, 0); F[3] = vec4(rel + vec3(0.5f, 1.f + 0.11f, 0.5f), 1);
        const float jw = 0.78f, jh = 0.49f, bend = 0.6f;
        drawJournalBody(VR, F, jw, jh, bend, L, false);
        // its pages (after the voxels), a page turning over when the page changes
        const std::string key = std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z);
        const int spread = std::clamp(e.value("page", 0), 0, bookSpreadCount(bt) - 1);
        Turn& t = turns[key];
        if (t.spread < 0) t.spread = spread;
        if (spread != t.spread) { t.prev = t.spread; t.dir = spread > t.spread ? 1 : -1; t.spread = spread; t.at = f.time; }
        pages.push_back({F, jw, jh, bend, bt, key, spread, f.time - t.at < 0.6 ? (float)((f.time - t.at) / 0.6) : -1.f, t.prev, t.dir, vec2(L.sky, L.block)});
      } else {
        const VoxelModel& im = itemModel("book");
        if (!im.ready()) return;
        if (!begun) { VR.begin(f, f.cam.viewProj); begun = true; }
        m = glm::translate(m, vec3(0.5f, 15.4f / 16, 0.5f));
        m = glm::rotate(m, TURN[meta], vec3(0, 1, 0));
        m = glm::rotate(m, -PI / 2 + 0.35f, vec3(1, 0, 0));
        m = glm::scale(m, vec3(0.6f));
        VR.draw(im.gl, m * im.matrix(), L);
      }
    });
    if (begun) VR.end();
    if (!pages.empty()) {
      ContentDraw& cd = contentDraw();
      if (cd.begin(f, f.cam.viewProj)) {
        for (auto& pg : pages) {
          const CanvasPic c = paintBookSpread("lectern/" + pg.key + "/" + std::to_string(pg.spread), pg.bt, pg.spread);
          JournalPic cur; cur.tex = c.tex; cur.texRect = vec4(0, 1, 1, 0);
          JournalPic prev;
          if (pg.leaf >= 0) { const CanvasPic pc = paintBookSpread("lectern/" + pg.key + "/" + std::to_string(pg.prev), pg.bt, pg.prev); prev.tex = pc.tex; prev.texRect = vec4(0, 1, 1, 0); }
          drawJournalPages(cd, pg.F, pg.jw, pg.jh, pg.bend, cur, f.time, pg.light, true, pg.leaf >= 0 ? &prev : nullptr, pg.dir);
          if (pg.leaf >= 0) drawJournalLeaf(cd, pg.F, pg.jw, pg.jh, pg.bend, prev, cur, pg.leaf, pg.dir, pg.light, true);
        }
        cd.end();
      }
      pages.clear();
    }
  }
  struct Turn { int spread = -1, prev = 0, dir = 1; double at = -10; };
  std::map<std::string, Turn> turns;
  struct Page { mat4 F; float jw, jh, bend; BookText bt; std::string key; int spread; float leaf; int prev, dir; vec2 light; };
  std::vector<Page> pages;
};

} // namespace

void registerLecternLayer() { registerRenderLayer([](GameSession&) { return std::make_shared<LecternLayer>(); }); }

} // namespace dq
