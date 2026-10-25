// DokuQuest — game entry point (normally started by the launcher with --renderer and --launcher).
#include "client/app.h"
#include <SDL_main.h>

int main(int argc, char** argv) {
  dq::installCrashHandler();
  dq::App app;
  return app.run(argc, argv);
}
