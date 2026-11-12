// assets/pack_fs.h — read any file of a resource pack (Java edition layout), whether the pack is a
// folder or a .zip in userDir()/resourcepacks/. Paths are relative to the pack's root (the folder that
// holds pack.mcmeta and assets/), e.g. "assets/minecraft/textures/block/stone.png" — a zip that wraps
// everything in a folder of its own is handled. Thread-safe (reads are serialised per pack).
//
// The pack applied by applyResourcePack() stays open as activePack(): the audio (sounds), creatures
// (entity textures) and UI (gui textures) code read from it.
#pragma once
#include "common/fs.h"
#include <mutex>

namespace dq {

class PackFS {
public:
  ~PackFS();
  static std::unique_ptr<PackFS> open(const fs::path& path);   // folder or .zip; nullptr if unreadable
  const std::string& name() const { return name_; }            // file/folder name without .zip
  const fs::path& path() const { return path_; }
  bool exists(const std::string& rel) const;
  std::optional<std::vector<u8>> read(const std::string& rel) const;
  std::optional<json> readJson(const std::string& rel) const;
  std::vector<std::string> list(const std::string& prefix) const;   // every file whose path starts with prefix
  // where the textures are: "assets/minecraft/textures/" (or "textures/" for pre-1.13 packs: legacy)
  std::string texRoot;
  bool legacy = false;
  int packFormat = 0;            // pack.mcmeta pack.pack_format (0 = unknown)
  std::string description;       // pack.mcmeta description, § codes removed
  bool hasTextures() const { return !texRoot.empty(); }
private:
  PackFS() = default;
  std::string name_; fs::path path_;
  bool zip = false;
  std::string base;              // prefix inside the zip (a wrapping folder) or ""
  std::vector<std::string> files;   // every file, relative to the pack root (sorted)
  void* za = nullptr;            // mz_zip_archive*
  std::unordered_map<std::string, unsigned> zidx;   // rel path -> zip file index
  mutable std::mutex mx;
};

PackFS* activePack();                       // nullptr = built-in tiles only
void setActivePack(std::unique_ptr<PackFS> p);

} // namespace dq
