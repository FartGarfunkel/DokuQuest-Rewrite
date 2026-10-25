// assets/char_model.h — voxel characters: a skin (+ the gear look it carries, + the body shape) built into
// voxel meshes, one per body part, the way render/skins.js buildSkinParts builds its boxes.
//
// Every skin pixel is a 2x2 patch of voxels on its body-part box (CHAR_VPP voxels per pixel, interior
// filled, faces coloured per face so edges show the right pixel); the outer layer is a raised shell of
// voxels wherever its pixels aren't see-through (a real 3D hat, jacket, sleeves and trousers). Arms and
// legs come in two halves (upper/lower) that overlap at the joint so elbows and knees can bend. A 3D
// character's gear (gear3d: hair, beards, ears, horns, hats, robes, cloaks) is voxelised per part.
// Meshes are built once per skin+shape (CharacterSpec::hash) and animated by part transforms.
//
// Units: model pixels (feet at y 0, top of the head at 32, chest toward -z, right arm at +x). A mesh's
// grid -> part space is CharMesh::local() (part space = model pixels relative to the part's pivot).
#pragma once
#include "assets/voxel.h"
#include "assets/item_models.h"
#include "assets/char_creator.h"
#include <map>

namespace dq {

struct TileSet;

constexpr int CHAR_VPP = 2;                  // voxels per skin pixel
constexpr float CHAR_PX = 0.9375f / 16.f;    // one skin pixel in blocks (the model is 1.875 blocks tall)

enum CharPart { CP_HEAD, CP_BODY, CP_ARMR_U, CP_ARMR_D, CP_ARML_U, CP_ARML_D, CP_LEGR_U, CP_LEGR_D, CP_LEGL_U, CP_LEGL_D, CP_COUNT };
const char* charPartName(int p);            // head body armR_u armR_d armL_u armL_d legR_u legR_d legL_u legL_d

struct CharMesh {
  VoxelMeshGL gl;
  vec3 origin{0};          // the grid's corner in part space (model pixels from the pivot)
  float voxel = 1.f / CHAR_VPP;
  mat4 local() const { return glm::scale(glm::translate(mat4(1), origin), vec3(voxel)); }
  bool ready() const { return gl.ready(); }
};

// skins.js skinFace: the face that moves. The lid is the cheek row drawn up over the eye row (a blink, and shut in sleep);
// villagers (and anyone whose skin has one) also get a mouth that opens while they talk. Thin voxel slabs just off the face.
struct CharFace { CharMesh lid, mouth; float seed = 0, period = 4; bool has = false; };

struct CharModel {
  u64 hash = 0;
  bool slim = false;
  float aw = 4;                                // arm width (pixels)
  std::optional<GearLook> look;                // a 3D character's look (gearRead of the skin)
  BodyShape body;
  vec2 stature{1, 1};                          // [height, breadth] of its people (gearStature) times the body shape's
  vec3 pivot[CP_COUNT];
  CharMesh base[CP_COUNT], ovl[CP_COUNT];      // the skin's inner layer / the 3D outer layer (hidden under armour)
  std::map<std::string, vec3> gearPivot;       // gear3d parts: head neck body skirtF skirtB armR foreR ... cape capeLo tail
  // the look's own shapes for (helmet worn, chestplate worn), built when first asked
  const std::map<std::string, CharMesh>& gear(bool helm, bool chest, const TileSet* ts) const;
  CharFace face;
  u32 skinColor = 0, sleeveColor = 0;          // the right arm's hand and shoulder colours
  mutable std::map<std::string, CharMesh> gearSets[4];
  mutable bool gearBuilt[4] = {false, false, false, false};
};

// the model for a character (GL thread; cached by spec hash, the oldest dropped past a few dozen)
const CharModel& charModel(const CharacterSpec& spec);

// worn armour: the pieces for one item on one slot (0 helmet .. 3 boots), by part (charPartName), in the
// resource pack's pictures when it has that material, else the game's armour tile. look3d adds gear3d's
// pauldrons, crests, tassets... (3D characters only). dye "rrggbb" for leather ("" = none).
struct ArmorSet { std::map<std::string, std::vector<const CharMesh*>> byPart; std::vector<std::unique_ptr<CharMesh>> meshes; bool pack = false; };
const ArmorSet& armorMeshes(const std::string& itemKey, const std::string& dye, int slot, bool slim, bool look3d, const TileSet& ts);

// a worn cape (game/capes.js): seg 0 = the whole cape (10 x 16 x 1 px, hung from its top edge at the origin,
// toward +z behind the body), 1 = its top 11 rows lying under a backpack, 2 = the 5-row hem below it
const CharMesh* capeMesh(const std::string& capeKey, int seg, const TileSet& ts);

// game/elytra.js: one folded wing (side 0 = right, 1 = left) as a voxel panel 9 x 21 x ~1.2 px, hung from its top inner corner at the
// origin (x outward, y up, z back); the caller turns it about that corner (sim/elytra.h elytraWings) and scales z by 0.8
const CharMesh* elytraWingMesh(int side, const TileSet& ts);

// how a backpack sits over what's worn under it (gear3d.js gearPackOver): po further out, lift higher straps,
// fz where the straps lie down the chest, capeZ where a cape's top lies under it
struct PackOver { float po = 0, lift = 0, fz = -2.32f, capeZ = -1; bool any = false; };
PackOver gearPackOver(const GearLook* L, const std::string& chestKey, const std::string& helmKey, bool cape);
// the backpack as worn (body part space: x across the shoulders, y up from the waist pivot, z out from the back);
// straps = only the straps down your chest (first person)
const CharMesh* packWornMesh(const std::string& key, bool straps, const PackOver& ov, const TileSet& ts);

// the right arm of a skin as a voxel model in the first-person arm's frame (render/item_layers firstPersonArm)
const VoxelModel* skinArmModel(const CharacterSpec& spec);

void charModelsReset();   // after a resource pack change (armour, capes and packs take their pictures from it)

} // namespace dq
