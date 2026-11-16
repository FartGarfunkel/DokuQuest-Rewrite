package org.dokuquest.app;

import org.libsdl.app.SDLActivity;

/**
 * DokuQuest on Android. SDL's activity (third_party/SDL2/android-project, built in by tools/android/build_apk.sh)
 * owns the window, the input and the controllers, and runs the game's main() (src/client/main.cpp) as SDL_main.
 */
public class MainActivity extends SDLActivity {
    // The whole game, SDL included, is one library: libmain.so (CMakeLists.txt, ANDROID).
    @Override
    protected String[] getLibraries() {
        return new String[] { "main" };
    }
}
