/* tools/sample_plugin.c — a tiny native plugin (mod/dq_plugin.h), used by test_net and as an example.
 * Build: any C compiler, as a shared library, into userDir()/mods/<id>/ (CMake target dq_sample_plugin). */
#include "mod/dq_plugin.h"
#include <string.h>

static const DqApi* A;

static const char* onPing(void* user, const char* playerJson, const char* argsJson) { (void)user; (void)playerJson; (void)argsJson; return "pong"; }
static int onChat(void* user, const char* ev, const char* args) { (void)user; (void)ev; return strstr(args, "badword") ? 0 : 1; }   /* 0 cancels */
static int onJoin(void* user, const char* ev, const char* args) { (void)user; (void)ev; A->log(A->host, args); return 1; }

DQ_PLUGIN_EXPORT int dq_plugin_init(const DqApi* api) {
  if (api->abi < 1) return 1;
  A = api;
  api->command(api->host, "ping", "Answers pong", onPing, 0);
  api->on(api->host, "chat", onChat, 0);
  api->on(api->host, "join", onJoin, 0);
  api->registerItem(api->host, "{\"key\":\"native_gem\",\"name\":\"Native Gem\"}");
  return 0;
}
