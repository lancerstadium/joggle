#ifndef JOGGLE_RESEARCH_RESIDUAL_EXPLORATION_H
#define JOGGLE_RESEARCH_RESIDUAL_EXPLORATION_H

#include "residual/oracle.h"

#include <cstdint>
#include <iosfwd>
#include <vector>

namespace residual {

struct ExplorationResult {
  Realization realization;
  std::uint32_t performance_trials = 0;
  std::uint32_t trial_cost_sum = 0;
};

[[nodiscard]] std::vector<std::uint8_t> serialize_f1_exploration_space();
[[nodiscard]] ExplorationResult explore_f1(const Facts& facts);
void write_exploration_csv(std::ostream& output);
[[nodiscard]] bool exploration_self_test(std::ostream& errors);

}  // namespace residual

#endif
