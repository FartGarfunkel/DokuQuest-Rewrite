// assets/char_creator.cpp — the creator's option tables and starting looks (ui/creator.js), character files and
// skin references.
#include "assets/char_creator.h"
#include "common/noise.h"
#include <mutex>
#include <map>

namespace dq {

// ------------------------------------------------------------------ tables (creator.js CC_*)
const CCList& ccList(const std::string& name) {
  static const std::map<std::string, CCList> T = {
    {"hair", {{"bald", "Bald"}, {"short", "Short"}, {"crop", "Cropped"}, {"long", "Long"}, {"tail", "Ponytail"}, {"braids", "Braids"}, {"mohawk", "Crest"}, {"bun", "Topknot"},
              {"curly", "Curly"}, {"sides", "Shaved sides"}, {"wild", "Wild mane"}, {"bob", "Bob"}, {"pixie", "Pixie cut"}, {"afro", "Big curls"}, {"dreads", "Locs"},
              {"undercut", "Undercut"}, {"pigtails", "Pigtails"}, {"spiky", "Spiky"}, {"slicked", "Slicked back"}, {"twinbuns", "Twin buns"}}},
    {"shoes", {{"boots", "Boots"}, {"tall", "Tall boots"}, {"shoes", "Buckled shoes"}, {"sandals", "Sandals"}, {"fur", "Fur boots"}, {"barefoot", "Barefoot"}}},
    {"belt", {{"plain", "Leather belt"}, {"studded", "Studded belt"}, {"sash", "Sash"}, {"rope", "Rope"}, {"gold", "Gilded belt"}, {"pouch", "Belt & pouch"}, {"bandolier", "Bandolier"}, {"none", "None"}}},
    {"gloves", {{"none", "Bare hands"}, {"leather", "Leather gloves"}, {"gauntlets", "Gauntlets"}, {"fingerless", "Fingerless"}, {"bracers", "Bracers"}, {"silk", "Silk gloves"}}},
    {"neck", {{"none", "None"}, {"scarf", "Scarf"}, {"collar", "Fur mantle"}, {"amulet", "Ruby amulet"}, {"pendant", "Sapphire pendant"}, {"torc", "Gold torc"}}},
    {"eyewear", {{"none", "None"}, {"glasses", "Spectacles"}, {"monocle", "Monocle"}, {"eyepatch", "Eyepatch"}, {"mask", "Face scarf"}}},
    {"beard", {{"none", "None"}, {"stubble", "Stubble"}, {"short", "Short"}, {"long", "Long"}, {"wizard", "Wizard (long)"}, {"forked", "Forked"}, {"braided", "Braided"}, {"moustache", "Moustache"}}},
    {"eyes", {{"round", "Round"}, {"narrow", "Narrow"}, {"wide", "Wide"}, {"slit", "Slit"}, {"glow", "Glowing"}}},
    {"mouth", {{"plain", "Plain"}, {"smile", "Smile"}, {"stern", "Stern"}, {"grin", "Grin"}}},
    {"marks", {{"none", "None"}, {"freckles", "Freckles"}, {"scar", "Scar"}, {"paint", "War paint"}, {"tattoo", "Markings"}, {"blush", "Rosy cheeks"}}},
    {"horns", {{"none", "None"}, {"curled", "Curled"}, {"ram", "Ram"}, {"straight", "Straight"}, {"small", "Nubs"}}},
    {"ears", {{"round", "Round"}, {"pointed", "Pointed"}, {"long", "Long pointed"}}},
    {"hat", {{"none", "None"}, {"hood", "Hood"}, {"wizard", "Wizard hat"}, {"feather", "Feathered cap"}, {"circlet", "Circlet"}, {"crown", "Crown"}, {"straw", "Straw hat"},
             {"cap", "Cap"}, {"bucket", "Bucket hat"}, {"chef", "Chef's hat"}, {"bandana", "Bandana"}, {"flowers", "Flower crown"}}},
    {"species", {{"human", "Human"}, {"elf", "Elf"}, {"dwarf", "Dwarf"}, {"halfling", "Halfling"}, {"gnome", "Gnome"}, {"orc", "Orc"}, {"goliath", "Goliath"},
                 {"tiefling", "Tiefling"}, {"dragonborn", "Dragonborn"}}},
    // the voxel creator's additions
    {"brows", {{"plain", "Plain"}, {"thick", "Thick"}, {"thin", "Thin"}, {"none", "None"}}},
    {"headShape", {{"square", "Square"}, {"round", "Rounded"}, {"long", "Long"}, {"wide", "Wide"}}},
    {"cape", {{"none", "None"}, {"red_cape", "Red"}, {"blue_cape", "Blue"}, {"green_cape", "Green"}, {"black_cape", "Black"}, {"white_cape", "White"}, {"purple_cape", "Purple"},
              {"yellow_cape", "Yellow"}, {"orange_cape", "Orange"}, {"cyan_cape", "Cyan"}, {"brown_cape", "Brown"}, {"ember_cape", "Ember"}, {"frost_cape", "Frost"}}},
  };
  static const CCList empty;
  auto it = T.find(name);
  return it == T.end() ? empty : it->second;
}

const std::vector<CCOutfit>& ccOutfits() {
  static const std::vector<CCOutfit> O = {
    {"gambeson", "Gambeson & tabard", {"#8a6a4a", "#3a5a9a", "#4a3a2a", "#3a2414"}}, {"furs", "Furs & leathers", {"#8a6a42", "#5a4030", "#4a3424", "#2a1a10"}},
    {"doublet", "Doublet & sash", {"#9a3a6a", "#e8c040", "#3a2a4a", "#3a2414"}}, {"vestments", "Vestments", {"#e8e0c8", "#c8a040", "#7a6a50", "#4a3424"}},
    {"leafrobe", "Leaf robe", {"#4a7a2a", "#8a6a3a", "#3a4a22", "#4a3424"}}, {"wraps", "Monk wraps", {"#c87a2a", "#6a2a1a", "#8a5a2a", "#3a2414"}},
    {"surcoat", "Surcoat over mail", {"#e8e8f0", "#c8a040", "#7a7a84", "#4a4a54"}}, {"ranger", "Hooded ranger", {"#3a5a2a", "#6a4a2a", "#4a3a2a", "#3a2414"}},
    {"shadow", "Shadow leathers", {"#2a2a32", "#6a2a2a", "#1e1e24", "#141418"}}, {"silks", "Silken robe", {"#a02a3a", "#e8c040", "#5a1a2a", "#2a1a10"}},
    {"pact", "Pact robe", {"#2a1a3a", "#8a3ac0", "#1e1428", "#141018"}}, {"archrobe", "Wizard robe", {"#2a3a8a", "#e8d070", "#1e2a5a", "#2a1a10"}},
    {"traveller", "Traveller", {"#7a6a4a", "#5a7a5a", "#4a4a3a", "#3a2414"}}, {"noble", "Fine clothes", {"#4a2a6a", "#e8c040", "#2a2a3a", "#1a1a1a"}},
    {"peasant", "Plain clothes", {"#9a8a6a", "#6a5a3a", "#5a4a3a", "#3a2a1a"}}, {"dress", "Dress", {"#7a3a8a", "#e8d8a0", "#5a2a6a", "#3a2414"}},
    {"gown", "Fine gown", {"#2a5a8a", "#e8c040", "#1e3a5a", "#2a1a10"}}, {"tunic", "Tunic", {"#6a7a3a", "#c8a060", "#5a4a3a", "#3a2414"}},
    {"smith", "Smith's apron", {"#8a7a6a", "#5a3a20", "#3a3a3a", "#2a1a10"}}, {"sailor", "Sailor stripes", {"#e8e8f0", "#2a3a8a", "#2a3a8a", "#2a1a10"}},
    {"pirate", "Pirate shirt & sash", {"#e8e0c8", "#8a2a2a", "#2a2a32", "#1a1a1a"}}, {"hunter", "Hunter's vest", {"#c8b890", "#5a4030", "#4a3a2a", "#3a2414"}},
    {"tabard", "Knight's tabard", {"#9a9aa4", "#8a2a2a", "#7a7a84", "#4a4a54"}}, {"merchant", "Merchant's coat", {"#6a3a2a", "#c8a040", "#3a2a20", "#2a1a10"}},
    {"jester", "Jester motley", {"#c04a2a", "#e8c040", "#c04a2a", "#2a2a32"}},
  };
  return O;
}
const CCOutfit* ccOutfit(const std::string& key) { for (auto& o : ccOutfits()) if (o.key == key) return &o; return nullptr; }

const std::vector<std::string>& ccPalette(const std::string& name) {
  static const std::map<std::string, std::vector<std::string>> T = {
    {"skin", {"#f2d3b8", "#e8bf9a", "#dcae88", "#c99670", "#b07a52", "#8e5a38", "#6e4228", "#52301c"}},   // villagers.js NPC_SKIN_TONES
    {"hair", {"#141414", "#2a1a10", "#4a2c18", "#6a4020", "#8a5a2a", "#b07a3a", "#d8b060", "#f0e0a0", "#e8e8e8", "#9a9a9a", "#8a2a14", "#c84a1a", "#3a5ab0", "#2a8a6a", "#8a3ac0", "#e86aa0"}},
    {"eye", {"#3a5ab0", "#3a7a4a", "#6a4a2a", "#2a2a2a", "#5a8ab0", "#7a6a3a", "#c8a020", "#e04040", "#a060e0", "#e8e8f0"}},
    {"horn", {"#2a2a30", "#5a4a3a", "#8a7a6a", "#d8c8a8", "#6a2a2a", "#3a2a4a"}},
    {"outfit", {"#2a2a32", "#5a4030", "#8a6a42", "#c8a060", "#e8e0c8", "#e8e8f0", "#9a9aa4", "#4a4a54", "#8a2a2a", "#c04a2a", "#e8c040", "#4a7a2a", "#2a5a3a", "#2a3a8a", "#4a6ae0", "#6a2a8a", "#a02a6a", "#3a2414"}},
  };
  static const std::vector<std::string> empty;
  auto it = T.find(name);
  return it == T.end() ? empty : it->second;
}

namespace {
struct Feat { const char* name; bool tusks, snout, scales, tail, nose; };
const Feat FEATS[] = {{"Plain", 0, 0, 0, 0, 0}, {"Tusks", 1, 0, 0, 0, 0}, {"Snout & scales", 0, 1, 1, 0, 0}, {"Tail", 0, 0, 0, 1, 0}, {"Big nose", 0, 0, 0, 0, 1}, {"Scales", 0, 0, 1, 0, 0}};
}
int ccFeatCount() { return 6; }
const char* ccFeatName(int i) { return FEATS[clampv(i, 0, 5)].name; }
int ccFeatIndex(const CharLook& L) {
  for (int i = 0; i < 6; i++) { const Feat& f = FEATS[i]; if (f.tusks == L.tusks && f.snout == L.snout && f.scales == L.scales && f.tail == L.tail && f.nose == L.nose) return i; }
  return 0;
}
void ccSetFeat(CharLook& L, int i) { const Feat& f = FEATS[floormod(i, 6)]; L.tusks = f.tusks; L.snout = f.snout; L.scales = f.scales; L.tail = f.tail; L.nose = f.nose; }

void ccSetSpecies(CharLook& L, const std::string& sp) {
  // undo the old people's usual features, then put on the new one's (CC_SPECIES)
  auto feats = [](const std::string& s, CharLook& T, bool on) {
    if (s == "elf" || s == "gnome") T.ears = on ? "pointed" : "round";
    if (s == "orc") T.tusks = on;
    if (s == "tiefling") { T.horns = on ? "curled" : "none"; T.tail = on; }
    if (s == "dragonborn") { T.snout = on; T.scales = on; T.tail = on; }
  };
  feats(L.species, L, false);
  L.species = sp;
  feats(sp, L, true);
}

void ccCycle(CharLook& L, const std::string& k, int d) {
  auto cyc = [&](const CCList& list, const std::string& v) {
    int i = 0; for (size_t j = 0; j < list.size(); j++) if (list[j].first == v) i = (int)j;
    return list[floormod(i + d, (int)list.size())].first;
  };
  if (k == "sex") {
    L.sex = L.sex == "f" ? "m" : "f";
    if (L.sex == "f") { L.body = "slim"; L.beard = "none"; if (L.hair == "bald" || L.hair == "crop" || L.hair == "short" || L.hair == "sides") L.hair = "long"; }
    else L.body = "broad";
  } else if (k == "body") L.body = L.body == "slim" ? "broad" : "slim";
  else if (k == "feat") ccSetFeat(L, ccFeatIndex(L) + d);
  else if (k == "outfit") {
    auto& O = ccOutfits(); int i = 0;
    for (size_t j = 0; j < O.size(); j++) if (O[j].key == L.outfit) i = (int)j;
    const CCOutfit& o = O[floormod(i + d, (int)O.size())];
    L.outfit = o.key; L.cols = o.cols;
  } else if (k == "hat") { L.hat = cyc(ccList("hat"), L.hat); L.hood = false; }
  else if (k == "cloak") L.cloak = !L.cloak;
  else if (k == "model") L.model3d = !L.model3d;
  else if (k == "own") L.own = !L.own;
  else if (k == "species") ccSetSpecies(L, cyc(ccList("species"), L.species));
  else if (k == "nose") L.nose = !L.nose;
  else if (k == "helm") L.helm = L.helm == "hide" ? "show" : "hide";
  else {
    std::string* f = k == "eyes" ? &L.eyes : k == "mouth" ? &L.mouth : k == "marks" ? &L.marks : k == "ears" ? &L.ears : k == "horns" ? &L.horns : k == "hair" ? &L.hair
                   : k == "beard" ? &L.beard : k == "shoes" ? &L.shoes : k == "belt" ? &L.belt : k == "gloves" ? &L.gloves : k == "neck" ? &L.neck
                   : k == "eyewear" ? &L.eyewear : k == "brows" ? &L.brows : k == "cape" ? &L.cape : k == "headShape" ? &L.shape.headShape : nullptr;
    if (!f) return;
    *f = cyc(ccList(k), *f);
    if (k == "shoes" || k == "belt" || k == "gloves" || k == "neck" || k == "eyewear") L.accSet.insert(k);
  }
}

CharLook ccDefaultLook(std::optional<u32> seed, const std::string& sexIn) {
  u32 s0 = seed ? *seed : (u32)(unixMillis() * 2654435761ull >> 7);
  Mulberry32 R(s0 ? s0 : 1);
  auto pick = [&](const std::vector<std::string>& a) { return a[(size_t)std::floor(R() * a.size())]; };
  bool f = (sexIn.empty() ? (R() < 0.5 ? "f" : "m") : sexIn) == std::string("f");
  CharLook L;
  std::string o = pick({"traveller", "peasant", "gambeson", "ranger", "doublet", "noble", "furs", f ? "dress" : "surcoat"});
  L.species = "human"; L.sex = f ? "f" : "m"; L.body = f ? "slim" : R() < 0.75 ? "broad" : "slim"; L.skin = pick(ccPalette("skin"));
  L.hair = f ? pick({"long", "tail", "braids", "bun", "curly", "bob", "wild"}) : pick({"short", "crop", "long", "tail", "curly", "sides", "bun", "wild", "bald"});
  std::vector<std::string> hc(ccPalette("hair").begin(), ccPalette("hair").begin() + 12);
  L.hairCol = pick(hc); L.beard = !f && R() < 0.4 ? pick({"stubble", "short", "long", "moustache"}) : "none";
  std::vector<std::string> ec(ccPalette("eye").begin(), ccPalette("eye").begin() + 6);
  L.eyes = pick({"round", "narrow", "wide"}); L.eyeCol = pick(ec); L.mouth = pick({"plain", "smile", "stern"});
  L.marks = R() < 0.25 ? pick({"freckles", "scar", "blush"}) : "none"; L.horns = "none"; L.hornCol = pick(ccPalette("horn")); L.ears = "round";
  L.outfit = o; L.cols = ccOutfit(o)->cols; L.cloak = R() < 0.3; L.hat = "none"; L.helm = "show";
  L.seed = (u32)(R() * 1e6); L.shoes = "boots"; L.belt = "plain"; L.gloves = "none"; L.neck = "none"; L.eyewear = "none";
  if (!seed) {   // a fresh random look also tries the newer clothes
    if (R() < 0.35) { L.outfit = pick({"tunic", "smith", "sailor", "pirate", "hunter", "tabard", "merchant"}); L.cols = ccOutfit(L.outfit)->cols; }
    if (R() < 0.3) L.hair = f ? pick({"pixie", "afro", "dreads", "pigtails", "twinbuns", "undercut"}) : pick({"afro", "dreads", "undercut", "spiky", "slicked"});
    L.shoes = R() < 0.6 ? "boots" : pick({"tall", "shoes", "sandals", "fur"});
    L.belt = R() < 0.5 ? "plain" : pick({"studded", "sash", "rope", "gold", "pouch", "bandolier"});
    if (R() < 0.3) L.gloves = pick({"leather", "gauntlets", "fingerless", "bracers", "silk"});
    if (R() < 0.3) L.neck = pick({"scarf", "collar", "amulet", "pendant", "torc"});
    if (R() < 0.1) L.eyewear = pick({"glasses", "monocle", "eyepatch"});
    // the voxel creator: a little variety in build too
    L.shape.height = 0.94f + (float)R() * 0.12f; L.shape.build = f ? 0.92f + (float)R() * 0.08f : 0.98f + (float)R() * 0.1f;
  }
  return L;
}

// ------------------------------------------------------------------ JSON
json CharLook::toJson() const {
  json j = {{"species", species}, {"sex", sex}, {"body", body}, {"skin", skin}, {"hair", hair}, {"hairCol", hairCol}, {"beard", beard}, {"eyes", eyes}, {"eyeCol", eyeCol},
            {"mouth", mouth}, {"marks", marks}, {"horns", horns}, {"hornCol", hornCol}, {"ears", ears}, {"tusks", tusks}, {"snout", snout}, {"scales", scales}, {"tail", tail},
            {"nose", nose}, {"outfit", outfit}, {"cols", json(std::vector<std::string>(cols.begin(), cols.end()))}, {"hood", hood}, {"cloak", cloak}, {"hat", hat}, {"helm", helm},
            {"seed", seed}, {"shoes", shoes}, {"belt", belt}, {"gloves", gloves}, {"neck", neck}, {"eyewear", eyewear}, {"model3d", model3d}, {"own", own},
            {"brows", brows}, {"cape", cape}, {"shape", shape.toJson()}};
  if (!beardCol.empty()) j["beardCol"] = beardCol;
  if (!eyeCol2.empty()) j["eyeCol2"] = eyeCol2;
  if (!accSet.empty()) { json a = json::object(); for (auto& k : accSet) a[k] = 1; j["accSet"] = a; }
  return j;
}

CharLook CharLook::fromJson(const json& j) {
  CharLook L = ccDefaultLook(7u, j.is_object() ? j.value("sex", std::string("m")) : "m");
  if (!j.is_object()) return L;
  auto S = [&](const char* k, std::string& v) { if (j.contains(k) && j[k].is_string()) v = j[k].get<std::string>(); };
  auto B = [&](const char* k, bool& v) { if (j.contains(k) && j[k].is_boolean()) v = j[k].get<bool>(); };
  S("species", L.species); S("sex", L.sex); S("body", L.body); S("skin", L.skin); S("hair", L.hair); S("hairCol", L.hairCol); S("beardCol", L.beardCol); S("beard", L.beard);
  S("eyes", L.eyes); S("eyeCol", L.eyeCol); S("eyeCol2", L.eyeCol2); S("mouth", L.mouth); S("marks", L.marks); S("horns", L.horns); S("hornCol", L.hornCol); S("ears", L.ears);
  B("tusks", L.tusks); B("snout", L.snout); B("scales", L.scales); B("tail", L.tail); B("nose", L.nose); S("outfit", L.outfit); B("hood", L.hood); B("cloak", L.cloak);
  S("hat", L.hat); S("helm", L.helm); S("shoes", L.shoes); S("belt", L.belt); S("gloves", L.gloves); S("neck", L.neck); S("eyewear", L.eyewear);
  B("model3d", L.model3d); B("own", L.own); S("brows", L.brows); S("cape", L.cape);
  if (j.contains("seed") && j["seed"].is_number()) L.seed = j["seed"].get<u32>();
  if (j.contains("cols") && j["cols"].is_array()) {
    for (size_t i = 0; i < 4 && i < j["cols"].size(); i++) if (j["cols"][i].is_string()) L.cols[i] = j["cols"][i];
  } else if (auto* o = ccOutfit(L.outfit)) L.cols = o->cols;
  if (j.contains("accSet") && j["accSet"].is_object()) for (auto& [k, v] : j["accSet"].items()) if (v.is_number() ? v.get<int>() != 0 : v == true) L.accSet.insert(k);
  if (j.contains("shape")) L.shape = BodyShape::fromJson(j["shape"]);
  if (!ccOutfit(L.outfit)) L.outfit = "traveller";
  return L;
}

// ------------------------------------------------------------------ files
fs::path charactersDir() { return userSub("characters"); }

std::string sanitizeName(const std::string& s) {
  std::string o;
  for (char c : s) o += (std::isalnum((unsigned char)c) || c == '-' || c == '_' || c == ' ') ? c : '_';
  while (!o.empty() && o.back() == ' ') o.pop_back();
  while (!o.empty() && o.front() == ' ') o.erase(o.begin());
  if (o.size() > 48) o.resize(48);
  return o.empty() ? "character" : o;
}

std::vector<std::string> listCharacters() {
  std::vector<std::string> out;
  std::error_code ec;
  for (auto& e : fs::directory_iterator(charactersDir(), ec)) {
    if (!e.is_regular_file() || e.path().extension() != ".json") continue;
    out.push_back(e.path().stem().string());
  }
  std::sort(out.begin(), out.end());
  return out;
}

bool saveCharacter(const std::string& nameIn, const CharLook& L, const SkinImage& skin) {
  std::string name = sanitizeName(nameIn);
  auto png = encodeSkinPng(skin);
  json j = {{"format", "dokuquest-character"}, {"version", 1}, {"name", name}, {"look", L.toJson()}, {"body", L.shape.toJson()},
            {"slim", skin.slim}, {"skin", base64Encode(png.data(), png.size())}};
  bool ok = writeJson(charactersDir() / (name + ".json"), j);
  invalidateCharacter("creator:" + name);
  return ok;
}

bool loadCharacter(const std::string& name, CharLook& L, SkinImage& skin) {
  auto j = readJson(charactersDir() / (sanitizeName(name) + ".json"));
  if (!j || !j->is_object()) return false;
  L = CharLook::fromJson(j->value("look", json::object()));
  if (j->contains("body")) L.shape = BodyShape::fromJson((*j)["body"]);
  bool have = false;
  if (j->contains("skin") && (*j)["skin"].is_string()) {
    auto png = base64Decode((*j)["skin"].get<std::string>());
    have = !png.empty() && loadSkinPngData(png.data(), png.size(), skin);
    if (have && j->contains("slim")) skin.slim = j->value("slim", skin.slim);
  }
  if (!have) ccPaintSkin(L, skin);
  return true;
}

// ------------------------------------------------------------------ references
namespace {
std::mutex g_mu;
std::map<std::string, std::shared_ptr<const CharacterSpec>>& cache() { static auto* m = new std::map<std::string, std::shared_ptr<const CharacterSpec>>(); return *m; }
}

std::shared_ptr<const CharacterSpec> resolveCharacter(const std::string& ref) {
  {
    std::lock_guard<std::mutex> l(g_mu);
    auto it = cache().find(ref);
    if (it != cache().end()) return it->second;
  }
  auto spec = std::make_shared<CharacterSpec>();
  spec->ref = ref;
  bool ok = false;
  if (startsWith(ref, "creator:")) {
    CharLook L;
    if (loadCharacter(ref.substr(8), L, spec->skin)) { spec->body = L.shape; spec->cape = L.cape == "none" ? "" : L.cape; ok = true; }
  } else if (auto r = registeredSkin(ref)) { spec->skin = *r; ok = true; }
  else if (!ref.empty()) {
    auto [file, arms] = skinArms(ref);
    std::string err;
    fs::path p = fs::path(file).is_absolute() ? fs::path(file) : skinsDir() / file;
    if (loadSkinFile(p, spec->skin, &err)) {
      if (!arms.empty() && !spec->skin.legacy) spec->skin.slim = arms == "slim";
      ok = true;
    } else DQ_WARN("skin '%s': %s", ref.c_str(), err.c_str());
  }
  if (!ok) spec->skin = paintDefaultSkin();
  spec->hash = spec->skin.hash() ^ (spec->body.hash() * 31) ^ std::hash<std::string>()(spec->cape);
  std::lock_guard<std::mutex> l(g_mu);
  cache()[ref] = spec;
  return spec;
}

void invalidateCharacter(const std::string& ref) {
  std::lock_guard<std::mutex> l(g_mu);
  if (ref.empty()) cache().clear(); else cache().erase(ref);
}

} // namespace dq
