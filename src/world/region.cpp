#include "world/region.h"
#include <cstring>

namespace dq {

fs::path RegionStore::path(const RKey& rk) const {
  return dir / "region" / ("d" + std::to_string(rk.dim)) / ("r." + std::to_string(rk.rx) + "." + std::to_string(rk.rz) + ".dqr");
}

RegionStore::Region& RegionStore::region(const RKey& rk) {
  auto it = regions.find(rk);
  if (it != regions.end()) return it->second;
  Region& r = regions[rk];
  if (auto bin = readBinary(path(rk))) {
    const auto& b = *bin;
    if (b.size() >= 4 + 8 * 1024 && std::memcmp(b.data(), "DQR1", 4) == 0) {
      for (int i = 0; i < 1024; i++) {
        const u8* e = b.data() + 4 + i * 8;
        u32 off = e[0] | e[1] << 8 | e[2] << 16 | (u32)e[3] << 24, len = e[4] | e[5] << 8 | e[6] << 16 | (u32)e[7] << 24;
        if (len && (size_t)off + len <= b.size()) r.blobs[i].assign(b.begin() + off, b.begin() + off + len);
      }
    } else DQ_WARN("region file %s is damaged; ignoring it", path(rk).string().c_str());
  }
  return r;
}

bool RegionStore::load(const ChunkKey& k, std::vector<u8>& out) {
  std::lock_guard<std::mutex> lk(mx);
  Region& r = region({k.dim, k.cx >> 5, k.cz >> 5});
  auto& b = r.blobs[(k.cz & 31) * 32 + (k.cx & 31)];
  if (b.empty()) return false;
  out = b;
  return true;
}
bool RegionStore::exists(const ChunkKey& k) {
  std::lock_guard<std::mutex> lk(mx);
  return !region({k.dim, k.cx >> 5, k.cz >> 5}).blobs[(k.cz & 31) * 32 + (k.cx & 31)].empty();
}
void RegionStore::store(const ChunkKey& k, std::vector<u8> data) {
  std::lock_guard<std::mutex> lk(mx);
  Region& r = region({k.dim, k.cx >> 5, k.cz >> 5});
  r.blobs[(k.cz & 31) * 32 + (k.cx & 31)] = std::move(data);
  r.dirty = true;
}
void RegionStore::flush() {
  std::lock_guard<std::mutex> lk(mx);
  for (auto& [rk, r] : regions) {
    if (!r.dirty) continue;
    std::vector<u8> out(4 + 8 * 1024, 0);
    std::memcpy(out.data(), "DQR1", 4);
    for (int i = 0; i < 1024; i++) {
      u32 off = (u32)out.size(), len = (u32)r.blobs[i].size();
      u8* e = out.data() + 4 + i * 8;
      for (int b = 0; b < 4; b++) { e[b] = (u8)((len ? off : 0) >> (b * 8)); e[4 + b] = (u8)(len >> (b * 8)); }
      out.insert(out.end(), r.blobs[i].begin(), r.blobs[i].end());
    }
    if (writeFileAtomic(path(rk), out.data(), out.size())) r.dirty = false;
    else DQ_ERR("could not write %s", path(rk).string().c_str());
  }
}
std::vector<ChunkKey> RegionStore::listStored(int dim) {
  std::vector<ChunkKey> out;
  std::error_code ec;
  fs::path d = dir / "region" / ("d" + std::to_string(dim));
  if (!fs::exists(d, ec)) return out;
  for (auto& f : fs::directory_iterator(d, ec)) {
    auto parts = splitStr(f.path().filename().string(), '.');
    if (parts.size() != 4 || parts[0] != "r") continue;
    int rx = std::stoi(parts[1]), rz = std::stoi(parts[2]);
    std::lock_guard<std::mutex> lk(mx);
    Region& r = region({dim, rx, rz});
    for (int i = 0; i < 1024; i++) if (!r.blobs[i].empty()) out.push_back({dim, rx * 32 + (i & 31), rz * 32 + (i >> 5)});
  }
  return out;
}

} // namespace dq
