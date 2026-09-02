#ifndef JOGGLE_RESEARCH_RESIDUAL_MATERIALIZER_H
#define JOGGLE_RESEARCH_RESIDUAL_MATERIALIZER_H

#include "residual/oracle.h"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <vector>

namespace residual {

struct MaterializationResult {
  Realization realization;
  std::uint32_t steps = 0;
  std::uint32_t arena_bytes = 0;
  std::uint32_t performance_trials = 0;
};

inline constexpr std::uint32_t kF1DeclaredStepBound = 26;
inline constexpr std::uint32_t kF1DeclaredArenaBound = 5;

[[nodiscard]] MaterializationResult materialize_f1(const Facts& facts);
[[nodiscard]] std::vector<std::uint8_t> serialize_f1_materializer();
void write_materializer_csv(std::ostream& output);
[[nodiscard]] bool materializer_self_test(std::ostream& errors);

}  // namespace residual

#endif
