package org.dokuquest.app;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Set;

import android.content.Intent;

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

    // The game's start options, for tests (the Android workflow's emulator run):
    //   adb shell am start -n org.dokuquest.app/.MainActivity --esa args --create,Test,--smoke-frames,300
    // Only these, each with its value: any app can start this activity, so nothing that opens a port or reads a
    // script from elsewhere.
    private static final Set<String> ALLOWED = new HashSet<>(Arrays.asList(
        "--create", "--seed", "--world", "--world-type", "--smoke-frames", "--smoke-shot"));

    @Override
    protected String[] getArguments() {
        Intent intent = getIntent();
        String[] given = intent != null ? intent.getStringArrayExtra("args") : null;
        if (given == null) return new String[0];
        ArrayList<String> out = new ArrayList<>();
        for (int i = 0; i + 1 < given.length; i += 2) {
            if (!ALLOWED.contains(given[i])) break;
            out.add(given[i]);
            out.add(given[i + 1]);
        }
        return out.toArray(new String[0]);
    }
}
