// tools/test_light.cpp — skylight in caves and modular dungeon rooms. Run: build/test_light
#include "core/init.h"
#include "core/registry.h"
#include "world/chunk.h"
#include "world/dims.h"
#include "world/light.h"
#include <cstdio>
#include <string>

using namespace dq;
static int fails = 0, passes = 0;
static void check(bool ok, const std::string& what) { printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str()); (ok ? passes : fails)++; }

int main(int argc, char** argv) {
  std::string ud = (fs::temp_directory_path() / "dq-test-light").string();
  for (int i = 1; i < argc; i++) if (std::string(argv[i]) == "--userdir" && i + 1 < argc) ud = argv[++i];
  std::error_code ec; fs::remove_all(ud, ec);
  CoreOptions co; co.server = true; co.userDirOverride = ud;
  if (!initCore(co)) { printf("FAIL initCore\n"); return 1; }
  const BlockId ST = reg().blockId("stone");
  auto sky = [](const Chunk& c, int x, int y, int z) { return c.getLight(x, y, z) >> 4; };

  printf("[open sky after lighting]\n");
  {
    Chunk c(DIM_OVERWORLD, 0, 0);
    for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) c.set(x, 40, z, ST);
    c.recomputeMaxY();
    initialChunkLight(c);
    c.compact();
    check(sky(c, 8, 80, 8) == 15, strFormat("open air above stone is full sky (sky %d)", sky(c, 8, 80, 8)));
    check((c.airLight[8] >> 4) == 15, "an all-air sky section stores full sun");
  }
  printf("[nether has no sky]\n");
  {
    Chunk c(DIM_NETHER, 0, 0);
    c.recomputeMaxY();
    initialChunkLight(c);
    check(sky(c, 8, 80, 8) == 0, strFormat("the Ember Depths have no sunlight (sky %d)", sky(c, 8, 80, 8)));
    check(!cellOpenToSky(c, 8, 80, 8), "a Nether column is not open to the sky");
  }

  printf("[unlit cave section]\n");
  {
    Chunk c(DIM_OVERWORLD, 0, 0);
    for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++)
      for (int y = 32; y <= 80; y++) c.set(x, y, z, ST);
    check(sky(c, 8, 20, 8) < 4, strFormat("before lighting, an empty section under stone is not noon (sky %d)", sky(c, 8, 20, 8)));
  }

  printf("[sealed dungeon room]\n");
  {
    Chunk c(DIM_OVERWORLD, 0, 0);
    for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) for (int y = 0; y <= 80; y++) c.set(x, y, z, ST);
    for (int z = 2; z <= 10; z++) for (int x = 2; x <= 10; x++) for (int y = 20; y <= 24; y++) c.set(x, y, z, AIR);
    c.recomputeMaxY();
    initialChunkLight(c);
    c.compact();
    check(sky(c, 6, 22, 6) < 4, strFormat("a sealed room has no sunlight (sky %d)", sky(c, 6, 22, 6)));
    check(!cellOpenToSky(c, 6, 22, 6), "a sealed room has no sky column");
  }

  printf("[shaft beside a roofed room]\n");
  {
    Chunk c(DIM_OVERWORLD, 0, 0);
    for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) for (int y = 0; y <= 80; y++) c.set(x, y, z, ST);
    for (int z = 2; z <= 10; z++) for (int x = 2; x <= 10; x++) for (int y = 20; y <= 24; y++) c.set(x, y, z, AIR);
    for (int y = 20; y <= 81; y++) c.set(6, y, 6, AIR);
    c.recomputeMaxY();
    initialChunkLight(c);
    c.compact();
    check(cellOpenToSky(c, 6, 22, 6), "the shaft itself is open to the sky");
    check(!cellOpenToSky(c, 5, 22, 6), "the cell beside the shaft still has a stone roof");
    check(!cellOpenToSky(c, 2, 22, 10), "the far corner of the room has a stone roof");
  }

  printf("[chest lid light: skip unloaded noon]\n");
  {
    // a dark cave chest (sky 0) beside a missing chunk (getLight would return 0xF0): the lid must
    // stay dark, not pick up phantom daylight
    const u8 packed[2] = {0x00, 0xF0};
    const bool loaded[2] = {true, false};
    const u8 got = packedLightMaxLoaded(packed, loaded, 2);
    check((got >> 4) == 0, strFormat("an unloaded neighbour is not noon on a dark chest (sky %d)", got >> 4));
    const bool both[2] = {true, true};
    const u8 sun = packedLightMaxLoaded(packed, both, 2);
    check((sun >> 4) == 15, "real daylight in a loaded cell still lights it");
    const u8 room[2] = {0x03, 0x08};
    const u8 mix = packedLightMaxLoaded(room, both, 2);
    check((mix >> 4) == 0 && (mix & 15) == 8, strFormat("a torch next to a dark chest is block 8 (got %d)", mix & 15));
  }

  printf("%d passed, %d failed\n", passes, fails);
  return fails ? 1 : 0;
}
