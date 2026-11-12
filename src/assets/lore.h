// assets/lore.h — the old books of the world (lecterns, "Old Book: ..." items): title + pages by lore key.
#pragma once
#include "common/common.h"

namespace dq {
struct LoreBook { std::string title; std::vector<std::string> pages; };
const LoreBook* loreBook(const std::string& key);   // nullptr when there is no such book
} // namespace dq
