// client/android_update.cpp — see android_update.h.
#include "client/android_update.h"
#ifdef __ANDROID__
#include <SDL.h>
#include <jni.h>
#include <filesystem>
#include "common/fs.h"
#endif

namespace dq {

#ifdef __ANDROID__
namespace {
// call a method of the game's activity: void name() or String name()
std::string callActivity(const char* name, bool returnsString) {
  JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
  jobject act = (jobject)SDL_AndroidGetActivity();
  if (!env || !act) return "";
  std::string out;
  jclass cls = env->GetObjectClass(act);
  jmethodID m = env->GetMethodID(cls, name, returnsString ? "()Ljava/lang/String;" : "()V");
  if (m && !env->ExceptionCheck()) {
    if (returnsString) {
      jstring r = (jstring)env->CallObjectMethod(act, m);
      if (r && !env->ExceptionCheck()) { const char* c = env->GetStringUTFChars(r, nullptr); if (c) { out = c; env->ReleaseStringUTFChars(r, c); } }
      if (r) env->DeleteLocalRef(r);
    } else env->CallVoidMethod(act, m);
  }
  if (env->ExceptionCheck()) env->ExceptionClear();
  env->DeleteLocalRef(cls); env->DeleteLocalRef(act);
  return out;
}
}
bool androidUpdateSupported() { return true; }
void androidUpdateCheck() { callActivity("updateCheck", false); }
std::string androidUpdateState() { return callActivity("updateStateNow", true); }
void androidUpdateStart() {   // (the game closes to be updated: not a Vulkan start that failed, so its mark goes first)
  std::error_code ec; std::filesystem::remove(userDir() / "vulkan_starting", ec);
  callActivity("updateStart", false);
}
#else
bool androidUpdateSupported() { return false; }
void androidUpdateCheck() {}
std::string androidUpdateState() { return ""; }
void androidUpdateStart() {}
#endif

} // namespace dq
