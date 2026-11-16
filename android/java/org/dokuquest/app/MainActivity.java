package org.dokuquest.app;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Set;

import android.content.Intent;
import android.net.Uri;

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
    // script from elsewhere. --smoke-script takes a bare file name, read from the game's own folder only.
    private static final Set<String> ALLOWED = new HashSet<>(Arrays.asList(
        "--create", "--seed", "--world", "--world-type", "--smoke-frames", "--smoke-shot", "--renderer"));

    @Override
    protected String[] getArguments() {
        Intent intent = getIntent();
        String[] given = intent != null ? intent.getStringArrayExtra("args") : null;
        if (given == null) return new String[0];
        ArrayList<String> out = new ArrayList<>();
        for (int i = 0; i + 1 < given.length; i += 2) {
            if (given[i].equals("--smoke-script")) {
                String name = given[i + 1];
                java.io.File dir = getExternalFilesDir(null);
                if (dir == null || !name.matches("[A-Za-z0-9_.-]+") || name.contains("..")) break;
                out.add(given[i]);
                out.add(new java.io.File(dir, name).getAbsolutePath());
                continue;
            }
            if (!ALLOWED.contains(given[i])) break;
            out.add(given[i]);
            out.add(given[i + 1]);
        }
        return out.toArray(new String[0]);
    }

    // ---- choosing a file (a skin or cape PNG, a resource pack): Android's own file chooser ----
    // src/ui/file_pick.cpp calls pickFile() from a worker thread and polls pickResultNow(): null while the chooser
    // is up, "" when it was cancelled, else the path of a copy in the game's cache (the chooser hands out a content
    // URI, not a file the game can open).
    private static final int PICK_FILE = 4711;
    private static volatile String pickResult = "";
    private static volatile String pickExt = "";

    public void pickFile(String mime, String ext) {
        pickResult = null;
        pickExt = ext;
        runOnUiThread(() -> {
            try {
                Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
                i.addCategory(Intent.CATEGORY_OPENABLE);
                i.setType(mime);
                startActivityForResult(i, PICK_FILE);
            } catch (Exception e) {
                pickResult = "";
            }
        });
    }

    public String pickResultNow() {
        return pickResult;
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode != PICK_FILE) {
            super.onActivityResult(requestCode, resultCode, data);
            return;
        }
        String out = "";
        Uri uri = data != null ? data.getData() : null;
        if (resultCode == RESULT_OK && uri != null) {
            java.io.File f = new java.io.File(getCacheDir(), "picked_" + System.currentTimeMillis() + (pickExt.isEmpty() ? "" : "." + pickExt));
            try (java.io.InputStream in = getContentResolver().openInputStream(uri); java.io.OutputStream os = new java.io.FileOutputStream(f)) {
                byte[] buf = new byte[65536];
                int n;
                while (in != null && (n = in.read(buf)) > 0) os.write(buf, 0, n);
                out = f.getAbsolutePath();
            } catch (Exception e) {
                out = "";
            }
        }
        pickResult = out;
    }

    // ---- keeping the game up to date (the title screen's Update button: src/client/android_update.cpp) ----
    // The newest phone build is the android-latest release; its notes say "build <n>", the same number as this APK's
    // version code. A higher one is offered; Update downloads it with Android's downloader and opens the installer
    // (the same signing key installs over this one and keeps the worlds).
    private static final String RELEASE_API = "https://api.github.com/repos/FartGarfunkel/DokuQuest-Rewrite/releases/tags/android-latest";
    private static volatile String updState = "";
    private static volatile String updUrl = "";
    private static volatile long updDownload = -1;
    private static volatile boolean updChecked = false;

    private long ownBuild() {
        try {
            android.content.pm.PackageInfo pi = getPackageManager().getPackageInfo(getPackageName(), 0);
            return android.os.Build.VERSION.SDK_INT >= 28 ? pi.getLongVersionCode() : pi.versionCode;
        } catch (Exception e) {
            return Long.MAX_VALUE;
        }
    }

    public void updateCheck() {
        if (updChecked) return;
        updChecked = true;
        new Thread(() -> {
            try {
                java.net.HttpURLConnection c = (java.net.HttpURLConnection) new java.net.URL(RELEASE_API).openConnection();
                c.setRequestProperty("Accept", "application/vnd.github+json");
                c.setRequestProperty("User-Agent", "DokuQuest-Android-updater");
                c.setConnectTimeout(8000);
                c.setReadTimeout(8000);
                if (c.getResponseCode() != 200) { updState = ""; return; }
                StringBuilder sb = new StringBuilder();
                try (java.io.BufferedReader r = new java.io.BufferedReader(new java.io.InputStreamReader(c.getInputStream(), "UTF-8"))) {
                    String line;
                    while ((line = r.readLine()) != null) sb.append(line).append('\n');
                }
                org.json.JSONObject rel = new org.json.JSONObject(sb.toString());
                java.util.regex.Matcher m = java.util.regex.Pattern.compile("build (\\d+)").matcher(rel.optString("body", ""));
                if (!m.find()) { updState = ""; return; }
                long remote = Long.parseLong(m.group(1));
                String url = "", name = "";
                org.json.JSONArray assets = rel.optJSONArray("assets");
                for (int i = 0; assets != null && i < assets.length(); i++) {
                    org.json.JSONObject a = assets.getJSONObject(i);
                    if (a.optString("name").endsWith(".apk")) { url = a.optString("browser_download_url"); name = a.optString("name"); }
                }
                if (remote > ownBuild() && !url.isEmpty()) {
                    updUrl = url;
                    String ver = name.replace("DokuQuest-", "").replace("-android-arm64.apk", "");
                    updState = "available:" + ver + " build " + remote;
                } else updState = "none";
            } catch (Exception e) {
                updState = "";   // (no network: nothing offered)
            }
        }).start();
    }

    public String updateStateNow() {
        if (updDownload >= 0 && updState.startsWith("downloading")) {
            android.app.DownloadManager dm = (android.app.DownloadManager) getSystemService(DOWNLOAD_SERVICE);
            try (android.database.Cursor cur = dm.query(new android.app.DownloadManager.Query().setFilterById(updDownload))) {
                if (cur != null && cur.moveToFirst()) {
                    int st = cur.getInt(cur.getColumnIndexOrThrow(android.app.DownloadManager.COLUMN_STATUS));
                    long got = cur.getLong(cur.getColumnIndexOrThrow(android.app.DownloadManager.COLUMN_BYTES_DOWNLOADED_SO_FAR));
                    long all = cur.getLong(cur.getColumnIndexOrThrow(android.app.DownloadManager.COLUMN_TOTAL_SIZE_BYTES));
                    if (st == android.app.DownloadManager.STATUS_SUCCESSFUL) { updState = "installing"; install(dm.getUriForDownloadedFile(updDownload)); }
                    else if (st == android.app.DownloadManager.STATUS_FAILED) updState = "error:the download failed";
                    else updState = "downloading:" + (all > 0 ? (int) (got * 100 / all) : 0);
                }
            } catch (Exception e) {
                updState = "error:the download failed";
            }
        }
        return updState;
    }

    public void updateStart() {
        if (updUrl.isEmpty() || updState.startsWith("downloading")) return;
        try {
            java.io.File dir = getExternalFilesDir(android.os.Environment.DIRECTORY_DOWNLOADS);
            if (dir != null) { java.io.File old = new java.io.File(dir, "DokuQuest-update.apk"); if (old.exists()) old.delete(); }
            android.app.DownloadManager.Request req = new android.app.DownloadManager.Request(Uri.parse(updUrl));
            req.setTitle("DokuQuest update");
            req.setMimeType("application/vnd.android.package-archive");
            req.setDestinationInExternalFilesDir(this, android.os.Environment.DIRECTORY_DOWNLOADS, "DokuQuest-update.apk");
            req.setNotificationVisibility(android.app.DownloadManager.Request.VISIBILITY_VISIBLE);
            android.app.DownloadManager dm = (android.app.DownloadManager) getSystemService(DOWNLOAD_SERVICE);
            updDownload = dm.enqueue(req);
            updState = "downloading:0";
        } catch (Exception e) {
            updState = "error:the download couldn't start";
        }
    }

    private void install(Uri apk) {
        runOnUiThread(() -> {
            try {
                Intent i = new Intent(Intent.ACTION_VIEW);
                i.setDataAndType(apk, "application/vnd.android.package-archive");
                i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_ACTIVITY_NEW_TASK);
                startActivity(i);   // (the first time Android asks to allow installing apps from DokuQuest)
            } catch (Exception e) {
                updState = "error:the installer couldn't open";
            }
        });
    }
}
