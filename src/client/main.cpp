// DokuQuest — game entry point (normally started by the launcher with --renderer and --launcher).
#include "client/app.h"
#include <SDL_main.h>
#ifdef __ANDROID__
#include "client/android_boot.h"
#include <unistd.h>
#endif

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
#ifdef __ANDROID__
  // Android: SDL's activity (android/) calls this as SDL_main. The game's files are unpacked from the APK first.
  std::string why;
  if (!dq::androidPrepare(why)) {
    DQ_ERR("android: %s", why.c_str());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "DokuQuest", why.c_str(), nullptr);
    _exit(1);
  }
  int rc;
  {
    dq::App app;
    rc = app.run(argc, argv);
  }
  // A new process for the next start: the activity can come back in this same process, where the game's statics
  // (settings, caches, the GPU state) would still hold this run's values.
  _exit(rc);
#else
  dq::App app;
  return app.run(argc, argv);
#endif
}
