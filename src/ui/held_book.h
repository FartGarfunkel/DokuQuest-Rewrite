// ui/held_book.h — a book read (and written in) held open in both hands, as a real book: an old book of the world, a
// written book, a book and quill (ui/book_pages.h). Its pages turn over as you go (the arrows, A / D, Q / E, LB / RB,
// the wheel, a click on a page); in a book and quill you write on the page with the pen (the keys), add pages as it
// fills, and sign it with a title to make it a written book. The hand view holds it (render/item_layers.cpp).
#pragma once
#include "render/journal_model.h"

namespace dq {
class App;
// the book held open now (the hand view's): its spread, the one it was (the page turning over: leaf 0..1, -1 none)
bool heldBookView(JournalPic& cur, JournalPic& prev, float& leaf, int& dir);
bool heldBookOpen();
} // namespace dq
