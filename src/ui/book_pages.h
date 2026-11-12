// ui/book_pages.h — the books that are read and written in the world, as text, and their pages painted (two at a time,
// an open spread) for the 3D books: in your hands (ui/held_book.cpp) and on lecterns (render/lectern_layer.cpp). The old
// books of the world (assets/lore.h), written books (tag {title, author, pages}) and books and quills being written
// (tag {pages}).
#pragma once
#include "ui/world_canvas.h"
#include "core/itemstack.h"
#include <vector>

namespace dq {

struct BookText {
  std::string title, author;
  std::vector<std::string> pages;
  bool titlePage = true;   // a title page before the words (old and written books)
  bool writable = false;   // a book and quill: being written
};
bool bookTextOf(const ItemStack& s, BookText& out);          // false: not a book with pages
bool bookTextOfLectern(const json& be, BookText& out);       // a lectern's book (its record)
int bookPageCount(const BookText& b);                        // the pages shown (the title page counted)
int bookSpreadCount(const BookText& b);
// the spread's picture (its left page 2*spread, its right 2*spread+1, of the pages shown); caretPage: a book being
// written, the page the words go onto (-1 none)
CanvasPic paintBookSpread(const std::string& key, const BookText& b, int spread, int caretPage = -1);
constexpr int BOOK_PAGE_CHARS = 320;   // a book and quill's page holds about this much

} // namespace dq
