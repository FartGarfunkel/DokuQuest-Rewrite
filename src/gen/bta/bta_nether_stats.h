// Optional per-decoration counters (validation).
#pragma once
namespace bta {
struct DecoStats {
  long long attempts[64] = {};
};
}  // namespace bta
