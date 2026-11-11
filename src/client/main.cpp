// DokuQuest — game entry point (normally started by the launcher with --renderer and --launcher).
#include "client/app.h"
#include <SDL_main.h>

#ifdef _WIN32
// Windows laptops: ask the NVIDIA (Optimus) and AMD (PowerXpress) drivers for the dedicated card. They only look for
// these in the .exe itself, so they live here and not in a library. (render/gpu_select.cpp does the same on Linux.)
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

int main(int argc, char** argv) {
  dq::installCrashHandler();
  dq::App app;
  return app.run(argc, argv);
}
