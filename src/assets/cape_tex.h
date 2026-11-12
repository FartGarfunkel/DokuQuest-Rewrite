// assets/cape_tex.h — a cape you painted yourself (the cape editor): a Minecraft-layout 64x32 PNG in
// userDir()/capes/<name>.png. The renderer refers to it by the cape key "cape@<name>" (capeMesh, char_model.cpp);
// the editor shows work that isn't saved yet under "cape@~edit" (setLiveCape). Settings::extra["cape"] names the one you wear.
//
// Minecraft cape layout (64x32, x y w h):  top 1,0,10,1   bottom 11,0,10,1   outside (back) 1,1,10,16
//                                          inside 12,1,10,16   right edge 0,1,1,16   left edge 11,1,1,16
#pragma once
#include "common/common.h"
#include "common/fs.h"

namespace dq {

struct CapePixels {
  int w = 64, h = 32;          // 64x32 (or a multiple: 128x64 ...)
  std::vector<u8> rgba;        // w*h*4
  CapePixels() : rgba(64 * 32 * 4, 0) {}
  u8* at(int x, int y) { return rgba.data() + ((size_t)y * w + x) * 4; }
  const u8* at(int x, int y) const { return rgba.data() + ((size_t)y * w + x) * 4; }
};
struct CapeRect { int x, y, w, h; const char* name; bool inner; };
const std::vector<CapeRect>& capeRects();            // the six parts of the layout above
bool capePixelUsed(int x, int y);                    // is (x,y) in the layout at all (64x32 units)

constexpr const char* CAPE_KEY_PREFIX = "cape@";
inline bool isCustomCape(const std::string& key) { return key.compare(0, 5, CAPE_KEY_PREFIX) == 0; }
inline std::string customCapeKey(const std::string& name) { return CAPE_KEY_PREFIX + name; }

CapePixels defaultCapePixels();                      // a plain red cape
CapePixels blankCapePixels();                        // one flat grey
fs::path capesDir();                                 // userDir()/capes
std::vector<std::string> listCapes();                // names (without .png)
bool loadCapePng(const fs::path& p, CapePixels& out, std::string* err = nullptr);   // 64x32 (or 2x, 4x), or the old 22x17
bool saveCapePng(const fs::path& p, const CapePixels& c);
bool loadCapeNamed(const std::string& name, CapePixels& out);    // userDir()/capes/<name>.png
bool saveCapeNamed(const std::string& name, const CapePixels& c);

// the cape the local player wears: Settings::extra["cape"] (a name in capes/, "" = none)
struct Settings;
std::string wornCapeName(const Settings& s);
void setWornCapeName(Settings& s, const std::string& name);   // saves the settings

// what the renderer reads: the live copy of that name if the editor set one, else the saved file
bool loadCustomCape(const std::string& key, CapePixels& out);
void setLiveCape(const std::string& name, const CapePixels& c);   // also drops the cached 3D cape of that key
void dropLiveCape(const std::string& name);

} // namespace dq
