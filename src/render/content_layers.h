// render/content_layers.h — registers the content features' client side (the float and line, the hook and rope,
// campfire smoke, the live instruments, the map in your hands, the elytra glide, the M and H keys and screens).
#pragma once

namespace dq {

void registerContentLayers();   // once, before a GameSession builds its layers (client/session.cpp)

} // namespace dq
