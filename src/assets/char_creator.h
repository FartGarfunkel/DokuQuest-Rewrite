// assets/char_creator.h — the voxel character creator's data (port of ui/creator.js: the look, its option
// tables, ccDefaultLook and the skin painter ccPaintSkin; plus gear3d.js's "3D character" look in the skin's
// spare pixels), character model files and skin references.
//
// A character is a CharLook (everything the creator picks) painted into a 64x64 skin; a 3D character also
// writes its gear look (hair, beards, ears, horns, hats, robes, cloaks ...) into the skin's spare corner
// (gearWrite), so the skin alone rebuilds the whole voxel character anywhere (other players, exports).
// The body shape sliders (height, build, head, legs, head shape) travel in the character file.
//
// Files: userDir()/characters/<name>.json = {"format":"dokuquest-character","version":1,"name",
//   "look":{...CharLook}, "body":{...BodyShape}, "skin":"<base64 png>"}; Settings::skin = "creator:<name>".
#pragma once
#include "assets/skin.h"
#include "assets/gear3d.h"
#include <set>

namespace dq {

struct CharLook {
  // creator.js ccDefaultLook fields
  std::string species = "human", sex = "m", body = "broad", skin = "#d0a07c";
  std::string hair = "short", hairCol = "#4a2c18", beardCol, beard = "none";
  std::string eyes = "round", eyeCol = "#3a5ab0", eyeCol2, mouth = "plain", marks = "none";
  std::string horns = "none", hornCol = "#2a2a30", ears = "round";
  bool tusks = false, snout = false, scales = false, tail = false, nose = false;
  std::string outfit = "traveller";
  std::array<std::string, 4> cols{"#7a6a4a", "#5a7a5a", "#4a4a3a", "#3a2414"};
  bool hood = false, cloak = false;
  std::string hat = "none", helm = "show";
  u32 seed = 1;
  std::string shoes = "boots", belt = "plain", gloves = "none", neck = "none", eyewear = "none";
  std::set<std::string> accSet;        // accessories picked by hand (worn over your own skin too)
  bool model3d = true, own = false;    // a 3D character (gear shapes) / your own skin under it
  // the voxel creator's additions
  std::string brows = "plain";         // plain | thick | thin | none
  std::string cape = "none";           // a cape worn when no cape item is (an item key: red_cape ...)
  BodyShape shape;

  json toJson() const;
  static CharLook fromJson(const json& j);
};

// option tables (key, label) — the original's lists, in its order
using CCList = std::vector<std::pair<std::string, std::string>>;
const CCList& ccList(const std::string& name);   // hair shoes belt gloves neck eyewear beard eyes mouth marks horns ears hat species brows headShape cape
struct CCOutfit { std::string key, name; std::array<std::string, 4> cols; };
const std::vector<CCOutfit>& ccOutfits();
const CCOutfit* ccOutfit(const std::string& key);
const std::vector<std::string>& ccPalette(const std::string& name);   // skin hair eye horn outfit
// CC_FEATS (Plain, Tusks, Snout & scales, Tail, Big nose, Scales)
int ccFeatCount();
const char* ccFeatName(int i);
int ccFeatIndex(const CharLook& L);
void ccSetFeat(CharLook& L, int i);
// a people's usual features (CC_SPECIES), applied when switching species
void ccSetSpecies(CharLook& L, const std::string& species);
// cycle a list option by d (the creator's arrows), with the original's side rules (sex, outfit colours ...)
void ccCycle(CharLook& L, const std::string& key, int d);

CharLook ccDefaultLook(std::optional<u32> seed, const std::string& sex = "");   // "" = either
// paint the look into a skin (creator.js ccPaintSkin + gear3d.js's 3D look); acc (optional, 4096) marks accessory pixels
void ccPaintSkin(const CharLook& L, SkinImage& out, std::vector<u8>* acc = nullptr);
// the look over your own skin: only the accessories picked by hand and the 3D look's corner are written over it
void ccPaintOverOwn(const CharLook& L, const SkinImage& own, SkinImage& out);
GearLook ccGearLook(const CharLook& L);

// character files
fs::path charactersDir();
std::vector<std::string> listCharacters();
bool saveCharacter(const std::string& name, const CharLook& L, const SkinImage& skin);
bool loadCharacter(const std::string& name, CharLook& L, SkinImage& skin);
std::string sanitizeName(const std::string& s);

// a skin reference resolved: the picture and the body shape the renderer builds from
struct CharacterSpec {
  std::string ref;
  SkinImage skin;
  BodyShape body;
  std::string cape;          // the creator's cosmetic cape ("" = none)
  u64 hash = 0;              // skin + body
};
std::shared_ptr<const CharacterSpec> resolveCharacter(const std::string& ref);   // cached; never null (falls back to the default skin)
void invalidateCharacter(const std::string& ref = "");                          // "" = all (after saving a skin or character)

} // namespace dq
