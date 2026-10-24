/* mod/dq_plugin.h — the native plugin ABI (Forge-style power): a shared library in userDir()/mods/<id>/
 * (any *.so / *.dll / *.dylib there) that exports dq_plugin_init. Plain C, so a plugin built with any compiler
 * keeps working: the struct only ever grows at the end, and `abi`/`size` say what this game offers.
 *
 *   #include "dq_plugin.h"
 *   static int onBreak(void* user, const char* ev, const char* args) { ... return 1; }   // 0 cancels
 *   DQ_PLUGIN_EXPORT int dq_plugin_init(const DqApi* api) {
 *     api->on(api->host, "blockBreak", onBreak, NULL);
 *     api->command(api->host, "ping", "Answers pong", onPing, NULL);
 *     return 0;   // anything else = failed (the reason can go to api->log first)
 *   }
 *
 * Events and their JSON argument arrays are the DQ mod events (sim/events.h): worldOpen [name], tick [dt],
 * blockBreak [player,x,y,z,key], blockPlace [player,x,y,z,key], mobDeath [info,killer], chat [name,text],
 * join [name], leave [name], serverStart [opts], serverStop [], log [text,kind], and the engine's others.
 * Calls are made on the thread that runs the world. Strings passed in are only valid during the call.
 */
#ifndef DQ_PLUGIN_H
#define DQ_PLUGIN_H

#ifdef _WIN32
#define DQ_PLUGIN_EXPORT __declspec(dllexport)
#else
#define DQ_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define DQ_PLUGIN_ABI 1

typedef int (*DqEventFn)(void* user, const char* event, const char* argsJson);          /* return 0 to cancel */
typedef const char* (*DqCommandFn)(void* user, const char* playerJson, const char* argsJson);   /* reply (plugin-owned, valid until the next call) or NULL */

typedef struct DqApi {
  int abi;                    /* DQ_PLUGIN_ABI */
  int size;                   /* sizeof(DqApi) in this game */
  const char* gameVersion;
  const char* modId;
  const char* modDir;         /* the plugin's folder (data, config...) */
  void* host;                 /* pass back as the first argument */

  void (*log)(void* host, const char* text);
  int (*on)(void* host, const char* event, DqEventFn fn, void* user);
  int (*command)(void* host, const char* name, const char* help, DqCommandFn fn, void* user);
  int (*registerBlock)(void* host, const char* defJson);      /* data-pack block def {key, name, tex, hardness, ...} */
  int (*registerItem)(void* host, const char* defJson);       /* data-pack item def {key, name, icon, block, ...} */
  int (*addRecipe)(void* host, const char* recipeJson);       /* {out, n, shape, key} or {out, n, items} */
  int (*getBlock)(void* host, int x, int y, int z, char* keyOut, int keyCap);   /* 1 = loaded (key written) */
  int (*setBlock)(void* host, int x, int y, int z, const char* key, int meta);
  void (*say)(void* host, const char* text);                  /* to everyone, as the mod */
  int (*give)(void* host, const char* player, const char* item, int count);
  unsigned (*spawn)(void* host, const char* type, double x, double y, double z);   /* entity id, 0 = no */
  int (*players)(void* host, char* jsonOut, int cap);         /* [{name,x,y,z,dim,realm,hp}]; returns the length needed */
  double (*time)(void* host, double setDayFraction);          /* < 0 only reads; 0..1 of the day */
  int (*emit)(void* host, const char* event, const char* argsJson);   /* raise an event (0 = something cancelled it) */
  int (*serving)(void* host);                                 /* 1 while this game hosts (server / open to LAN) */
} DqApi;

typedef int (*DqPluginInitFn)(const DqApi* api);
typedef void (*DqPluginShutdownFn)(void);
/* exported by the plugin: */
/* int dq_plugin_init(const DqApi* api);    required */
/* void dq_plugin_shutdown(void);           optional, when the game closes */

#ifdef __cplusplus
}
#endif
#endif
