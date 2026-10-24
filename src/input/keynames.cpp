// input/keynames.cpp — SDL scancodes as the browser key codes the original game saves ("KeyW",
// "ShiftLeft", "Mouse0"...), and their display names (controls.js keyName).
#include "input/input.h"

namespace dq {

namespace {
struct CodePair { SDL_Scancode sc; const char* code; };
const std::vector<CodePair>& codeTable() {
  static std::vector<CodePair> t;
  if (!t.empty()) return t;
  static char letters[26][5], digits[10][7], fkeys[12][4], numpad[10][8];
  for (int i = 0; i < 26; i++) { snprintf(letters[i], 5, "Key%c", 'A' + i); t.push_back({(SDL_Scancode)(SDL_SCANCODE_A + i), letters[i]}); }
  for (int i = 0; i < 9; i++) { snprintf(digits[i + 1], 7, "Digit%d", i + 1); t.push_back({(SDL_Scancode)(SDL_SCANCODE_1 + i), digits[i + 1]}); }
  t.push_back({SDL_SCANCODE_0, "Digit0"});
  for (int i = 0; i < 12; i++) { snprintf(fkeys[i], 4, "F%d", i + 1); t.push_back({(SDL_Scancode)(SDL_SCANCODE_F1 + i), fkeys[i]}); }
  for (int i = 0; i < 9; i++) { snprintf(numpad[i + 1], 8, "Numpad%d", i + 1); t.push_back({(SDL_Scancode)(SDL_SCANCODE_KP_1 + i), numpad[i + 1]}); }
  t.push_back({SDL_SCANCODE_KP_0, "Numpad0"});
  const CodePair rest[] = {
    {SDL_SCANCODE_SPACE, "Space"}, {SDL_SCANCODE_LSHIFT, "ShiftLeft"}, {SDL_SCANCODE_RSHIFT, "ShiftRight"},
    {SDL_SCANCODE_LCTRL, "ControlLeft"}, {SDL_SCANCODE_RCTRL, "ControlRight"}, {SDL_SCANCODE_LALT, "AltLeft"}, {SDL_SCANCODE_RALT, "AltRight"},
    {SDL_SCANCODE_UP, "ArrowUp"}, {SDL_SCANCODE_DOWN, "ArrowDown"}, {SDL_SCANCODE_LEFT, "ArrowLeft"}, {SDL_SCANCODE_RIGHT, "ArrowRight"},
    {SDL_SCANCODE_RETURN, "Enter"}, {SDL_SCANCODE_KP_ENTER, "NumpadEnter"}, {SDL_SCANCODE_ESCAPE, "Escape"}, {SDL_SCANCODE_BACKSPACE, "Backspace"},
    {SDL_SCANCODE_DELETE, "Delete"}, {SDL_SCANCODE_TAB, "Tab"}, {SDL_SCANCODE_CAPSLOCK, "CapsLock"}, {SDL_SCANCODE_SLASH, "Slash"},
    {SDL_SCANCODE_BACKSLASH, "Backslash"}, {SDL_SCANCODE_MINUS, "Minus"}, {SDL_SCANCODE_EQUALS, "Equal"}, {SDL_SCANCODE_LEFTBRACKET, "BracketLeft"},
    {SDL_SCANCODE_RIGHTBRACKET, "BracketRight"}, {SDL_SCANCODE_SEMICOLON, "Semicolon"}, {SDL_SCANCODE_APOSTROPHE, "Quote"}, {SDL_SCANCODE_COMMA, "Comma"},
    {SDL_SCANCODE_PERIOD, "Period"}, {SDL_SCANCODE_GRAVE, "Backquote"}, {SDL_SCANCODE_HOME, "Home"}, {SDL_SCANCODE_END, "End"},
    {SDL_SCANCODE_PAGEUP, "PageUp"}, {SDL_SCANCODE_PAGEDOWN, "PageDown"}, {SDL_SCANCODE_INSERT, "Insert"}, {SDL_SCANCODE_LGUI, "MetaLeft"},
    {SDL_SCANCODE_RGUI, "MetaRight"}, {SDL_SCANCODE_KP_PLUS, "NumpadAdd"}, {SDL_SCANCODE_KP_MINUS, "NumpadSubtract"},
    {SDL_SCANCODE_KP_MULTIPLY, "NumpadMultiply"}, {SDL_SCANCODE_KP_DIVIDE, "NumpadDivide"}, {SDL_SCANCODE_KP_PERIOD, "NumpadDecimal"},
  };
  for (auto& r : rest) t.push_back(r);
  return t;
}
}

std::string scancodeToCode(SDL_Scancode sc) {
  for (auto& p : codeTable()) if (p.sc == sc) return p.code;
  const char* n = SDL_GetScancodeName(sc);
  return n && *n ? std::string("SDL_") + n : "";
}
SDL_Scancode codeToScancode(const std::string& code) {
  for (auto& p : codeTable()) if (code == p.code) return p.sc;
  if (startsWith(code, "SDL_")) return SDL_GetScancodeFromName(code.c_str() + 4);
  return SDL_SCANCODE_UNKNOWN;
}

std::string keyName(const std::string& code) {
  if (code.empty()) return "\xE2\x80\x94";   // —
  static const std::pair<const char*, const char*> M[] = {
    {"Mouse0", "Left click"}, {"Mouse1", "Middle click"}, {"Mouse2", "Right click"}, {"Mouse3", "Mouse 4"}, {"Mouse4", "Mouse 5"},
    {"Space", "Space"}, {"ShiftLeft", "L Shift"}, {"ShiftRight", "R Shift"}, {"ControlLeft", "L Ctrl"}, {"ControlRight", "R Ctrl"},
    {"AltLeft", "L Alt"}, {"AltRight", "R Alt"}, {"ArrowUp", "\xE2\x86\x91"}, {"ArrowDown", "\xE2\x86\x93"}, {"ArrowLeft", "\xE2\x86\x90"},
    {"ArrowRight", "\xE2\x86\x92"}, {"Enter", "Enter"}, {"NumpadEnter", "Num Enter"}, {"Slash", "/"}, {"Backslash", "\\"}, {"Minus", "-"},
    {"Equal", "="}, {"BracketLeft", "["}, {"BracketRight", "]"}, {"Semicolon", ";"}, {"Quote", "'"}, {"Comma", ","}, {"Period", "."},
    {"Backquote", "`"}, {"Tab", "Tab"}, {"CapsLock", "Caps Lock"},
  };
  for (auto& [c, n] : M) if (code == c) return n;
  if (code.size() == 4 && startsWith(code, "Key")) return code.substr(3);
  if (code.size() == 6 && startsWith(code, "Digit")) return code.substr(5);
  if (startsWith(code, "Numpad") && code.size() == 7 && isdigit((unsigned char)code[6])) return "Num " + code.substr(6);
  if (startsWith(code, "SDL_")) return code.substr(4);
  return code;
}

const char* const PAD_BUTTON_NAMES[17] = {"A", "B", "X", "Y", "LB", "RB", "LT", "RT", "Back", "Start", "L3", "R3",
  "D-pad \xE2\x86\x91", "D-pad \xE2\x86\x93", "D-pad \xE2\x86\x90", "D-pad \xE2\x86\x92", "Guide"};

std::string padButtonName(int k, const std::string& layout) {
  (void)layout;   // the original names buttons the Xbox way whatever the layout setting
  if (k < 0 || k > 16) return "Button " + std::to_string(k);
  return PAD_BUTTON_NAMES[k];
}

} // namespace dq
