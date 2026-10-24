// mod/register.cpp — called from initCore(): the chat commands of the network side (/list /kick /ban /op /msg...).
// JS mods and native plugins register their own commands when they load (mod/modloader.cpp).
#include "core/init.h"
#include "net/server_host.h"
namespace dq {
void registerModApi() { net::registerNetCommands(); }
} // namespace dq
