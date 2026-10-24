// launcher/launcher_main.cpp — DokuQuest Launcher (news, play, profile/skins, options with renderer
// choice, mods, the dedicated server and the Mod Studio). Replaced by the full launcher.
#include "core/init.h"
#include <SDL_main.h>
#include <cstdio>
int main(int argc, char** argv) {
  dq::CoreOptions co;
  if (!dq::initCore(co)) return 1;
  std::printf("DokuQuest launcher placeholder\n");
  return 0;
}
