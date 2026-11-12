#include "sim/events.h"

namespace dq {

int EventBus::on(const std::string& name, Handler h, int priority) {
  auto& v = handlers[name];
  v.push_back({nextId, priority, std::move(h)});
  std::stable_sort(v.begin(), v.end(), [](const H& a, const H& b) { return a.pri > b.pri; });
  return nextId++;
}
void EventBus::off(int id) {
  for (auto& [n, v] : handlers) v.erase(std::remove_if(v.begin(), v.end(), [&](const H& h) { return h.id == id; }), v.end());
}
bool EventBus::emit(const std::string& name, const json& args) {
  auto it = handlers.find(name);
  if (it == handlers.end()) return true;
  auto copy = it->second;   // handlers may subscribe/unsubscribe while running
  bool ok = true;
  for (auto& h : copy) {
    try { if (!h.fn(args)) ok = false; }
    catch (std::exception& e) { DQ_WARN("event %s handler failed: %s", name.c_str(), e.what()); }
  }
  return ok;
}

} // namespace dq
