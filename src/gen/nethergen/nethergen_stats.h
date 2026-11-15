// Optional per-decoration counters (validation).
#pragma once
namespace nethergen {
struct DecoStats {
  long long attempts[64] = {};
};
}  // namespace nethergen
