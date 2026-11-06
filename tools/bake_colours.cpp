// tools/bake_colours.cpp — fold the block, item and creature pictures into one colour table.
// Run from the build: the table is linked into dokuquest, and that program then paints from it.
#include "assets/baked_colours.h"
#include "assets/tileset.h"
#include "render/tile_anim.h"
#include "common/common.h"
#include "common/fs.h"
#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace {

void collect(const dq::fs::path& root, const std::string& prefix, std::unordered_map<std::string, dq::BakedImage>& out) {
  std::error_code ec;
  if (!dq::fs::exists(root, ec)) return;
  for (auto it = dq::fs::recursive_directory_iterator(root, ec); it != dq::fs::recursive_directory_iterator(); ++it) {
    if (ec || !it->is_regular_file(ec)) continue;
    const dq::fs::path& p = it->path();
    std::string ext = p.extension().string();
    for (char& c : ext) c = (char)std::tolower((unsigned char)c);
    if (ext != ".png") continue;
    dq::fs::path rel = dq::fs::relative(p, root, ec);
    if (ec) continue;
    std::string key = prefix + rel.generic_string();
    if (key.size() < 5) continue;
    key.resize(key.size() - 4);
    // Block and item pictures are already in the tile table. Keep the pictures the game samples by name:
    // creatures, worn armour, the elytra and the GUI sheets.
    bool keep = key.rfind("entity/", 0) == 0 || key.rfind("models/armor/", 0) == 0
      || key == "gui/widgets" || key == "gui/icons" || key.rfind("gui/sprites/", 0) == 0 || key.rfind("gui/container/", 0) == 0;
    if (!keep) continue;
    int w = 0, h = 0;
    std::vector<dq::u8> px;
    if (!dq::loadPng(p, w, h, px) || w <= 0 || h <= 0 || w > 2048 || h > 8192) continue;
    if (px.size() < (size_t)w * h * 4) continue;
    dq::BakedImage im;
    im.key = key;
    im.w = w;
    im.h = h;
    im.rgba = std::move(px);
    out[key] = std::move(im);
  }
}

} // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    DQ_ERR("bake-colours: pass the output path");
    return 1;
  }
  dq::setUserDirOverride("/tmp/dq-bake-user");
  dq::TileSet ts;
  if (!dq::loadBuiltinTiles(ts)) {
    DQ_ERR("bake-colours: the built-in tiles did not load");
    return 1;
  }
  dq::addBuiltinTileAnims(ts);
  std::string pack = dq::applyResourcePack(ts, "DokuQuest Base");
  if (pack.empty()) DQ_WARN("bake-colours: DokuQuest Base was not applied");
  std::unordered_map<std::string, dq::BakedImage> images;
  collect(dq::dataDir() / "textures" / "entity", "entity/", images);
  collect(dq::dataDir() / "resourcepacks" / "DokuQuest Base" / "assets" / "minecraft" / "textures", "", images);
  std::vector<dq::BakedImage> list;
  list.reserve(images.size());
  for (auto& kv : images) list.push_back(std::move(kv.second));
  std::sort(list.begin(), list.end(), [](const dq::BakedImage& a, const dq::BakedImage& b) { return a.key < b.key; });
  std::vector<dq::u8> grass, foliage;
  int gw = 0, gh = 0, fw = 0, fh = 0;
  dq::copyPackColormaps(grass, gw, gh, foliage, fw, fh);
  if (!dq::writeBakedColours(argv[1], ts, list, gw, gh, grass.empty() ? nullptr : grass.data(), fw, fh, foliage.empty() ? nullptr : foliage.data())) {
    DQ_ERR("bake-colours: could not write %s", argv[1]);
    return 1;
  }
  DQ_LOG("bake-colours: %d tiles, %d pictures -> %s", ts.count, (int)list.size(), argv[1]);
  return 0;
}
