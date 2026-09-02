#ifndef JOGGLE_RESEARCH_RESIDUAL_CONDITIONED_H
#define JOGGLE_RESEARCH_RESIDUAL_CONDITIONED_H

#include "residual/oracle.h"

#include <cstdint>
#include <iosfwd>
#include <vector>

namespace residual {

struct GuardedVariant {
  // Bit order is shape/tensor/scratch/objective/resident.
  std::uint8_t specified_mask = 0;
  std::uint8_t value_mask = 0;
  Realization realization;
};

struct ConditionedArtifact {
  std::vector<GuardedVariant> entries;

  [[nodiscard]] std::vector<std::uint8_t> serialize() const;
  [[nodiscard]] std::uint32_t declared_step_bound() const;
};

struct ConditionedSelection {
  Realization realization;
  std::uint32_t steps = 0;
  bool fallback = false;
};

[[nodiscard]] ConditionedArtifact synthesize_exact_conditioned_f1(
    const std::vector<Facts>& training);
[[nodiscard]] ConditionedSelection select_conditioned_f1(
    const ConditionedArtifact& artifact, const Facts& facts);
void write_conditioned_csv(std::ostream& output);
[[nodiscard]] bool conditioned_self_test(std::ostream& errors);

}  // namespace residual

#endif
