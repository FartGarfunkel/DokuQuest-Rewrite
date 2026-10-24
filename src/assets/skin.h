// assets/skin.h — player skins (port of the data side of render/skins.js and the skin helpers of
// ui/skineditor.js): the 64x64 Minecraft layout, slim/classic arms, the original's painted default skin,
// legacy 64x32 skins, where skins live and how a skin is named.
//
// Skin references (Settings::skin, Player::skin):
//   ""                       the default skin (the Emberwild wanderer, painted)
//   "<file>.png"             userDir()/skins/<file>.png; "#slim" / "#classic" on the end forces the arms
//   "creator:<name>"         a voxel character made in the creator (userDir()/characters/<name>.json)
//   anything else            a skin registered at run time with registerSkin (e.g. "net:<hash>" from another player)
// resolveCharacter (assets/char_creator.h) turns a reference into the picture + body shape the renderer builds.
#pragma once
#include "common/fs.h"
#include <array>
#include <memory>

namespace dq {

// a skin, always 64x64 RGBA (64x32 pictures are converted on load: left limbs mirrored from the right ones)
struct SkinImage {
  std::vector<u8> px;        // 64 * 64 * 4
  bool slim = false;         // Alex arms (3 px)
  bool legacy = false;       // came from a 64x32 picture
  SkinImage() : px(64 * 64 * 4, 0) {}
  bool valid() const { return px.size() == 64 * 64 * 4; }
  u8* at(int x, int y) { return px.data() + ((size_t)y * 64 + x) * 4; }
  const u8* at(int x, int y) const { return px.data() + ((size_t)y * 64 + x) * 4; }
  u64 hash() const;          // pixels + arms
};

// How a voxel character is proportioned (the voxel creator's sliders; 1 = Minecraft proportions).
struct BodyShape {
  float height = 1, build = 1, head = 1, legs = 1;   // overall height, breadth, head size, leg length
  std::string headShape = "square";                 // square | round | long | wide
  bool operator==(const BodyShape& o) const { return height == o.height && build == o.build && head == o.head && legs == o.legs && headShape == o.headShape; }
  bool plain() const { return *this == BodyShape(); }
  json toJson() const;
  static BodyShape fromJson(const json& j);
  u64 hash() const;
};

// the six faces of a box in the Minecraft net, in FACES order (0..5 = +x right, -x left, +y top, -y bottom, +z back, -z front)
struct SkinRect { int x, y, w, h; };
std::array<SkinRect, 6> skinRects(int u, int v, int w, int h, int d);
// the skin pixel of face f at (s, t) in 0..1 of that face (skins.js addSkinBox's mapping), c = position in the box 0..1
ivec2 skinFacePixel(const std::array<SkinRect, 6>& R, int f, vec3 c, bool mirror = false);

// the body parts (ui/skineditor.js skeParts): base and overlay nets, boxes in model pixels (feet at y 0, head top 32)
struct SkinPartDef { const char* id; const char* name; const char* oname; vec3 mn, mx; ivec2 uv, ov; ivec3 sz; float infl; };
const std::array<SkinPartDef, 6>& skinParts(bool slim);   // head, body, armR, armL, legR, legL
// which part / layer / face each of the 4096 pixels belongs to (-1: none) — skeMap
struct SkinMapEntry { int part, layer, face; SkinRect rect; };
struct SkinMap { std::vector<SkinMapEntry> entries; std::array<i16, 4096> map; int idx[6][2][6]; };
const SkinMap& skinMap(bool slim);
int skinMirrorPixel(const SkinMap& M, int x, int y);   // the pixel on the other side of the body, or -1
bool skinIsBasePixel(int x, int y);                    // the inner layer (always solid)

SkinImage paintDefaultSkin();                          // skins.js paintDefaultSkin (the Emberwild wanderer)
// any skin picture (64n x 64n or 64n x 32n): scaled to 64, legacy converted, slim detected (normalizeSkinImage)
bool normalizeSkin(const u8* rgba, int w, int h, SkinImage& out, std::string* err = nullptr);
bool loadSkinFile(const fs::path& p, SkinImage& out, std::string* err = nullptr);
bool loadSkinPngData(const u8* data, size_t n, SkinImage& out, std::string* err = nullptr);
bool saveSkinPng(const fs::path& p, const SkinImage& s);
std::vector<u8> encodeSkinPng(const SkinImage& s);
void skinConvertArms(SkinImage& s, bool toSlim);       // classic <-> slim arms on all four arm nets (skeConvertArms)
void skinFromLegacy(u8* px64x64);                      // a 64x32 picture in the top half -> full 64x64 (skeFromLegacy)
// the average colour of a part's front (for the first-person arm etc.), 0 if none
u32 skinAverage(const SkinImage& s, int x, int y, int w, int h);

fs::path skinsDir();                                   // userDir()/skins
std::vector<std::string> listSkins();                  // *.png file names there
// "name.png#slim" -> ("name.png", "slim")
std::pair<std::string, std::string> skinArms(const std::string& ref);
// a skin known by name at run time (network, previews); resolveCharacter finds it
void registerSkin(const std::string& ref, const SkinImage& s);
std::shared_ptr<const SkinImage> registeredSkin(const std::string& ref);

} // namespace dq
