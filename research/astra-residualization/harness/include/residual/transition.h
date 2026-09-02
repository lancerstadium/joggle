#ifndef JOGGLE_RESEARCH_RESIDUAL_TRANSITION_H
#define JOGGLE_RESEARCH_RESIDUAL_TRANSITION_H

#include "residual/oracle.h"
#include "residual/branch_table.h"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace residual {

struct ResidentState {
  Realization realization;
  std::uint64_t generation = 0;
};

struct TransitionPlan {
  ResidentState before;
  Realization after;
  std::vector<std::string> add;
  std::vector<std::string> remove;
  std::uint32_t steps = 0;
  std::uint32_t arena_bytes = 0;
  std::uint32_t write_bound = 0;
};

struct TransitionOutcome {
  ResidentState resident;
  std::size_t staged_writes = 0;
  bool published = false;
};

inline constexpr std::uint32_t kF1TransitionStepBound = 40;
inline constexpr std::uint32_t kF1TransitionArenaBound = 21;

[[nodiscard]] TransitionPlan plan_f1_transition(
    const ResidentState& before, const Facts& after_facts);
[[nodiscard]] TransitionOutcome apply_transition(
    const TransitionPlan& plan, bool validation_passes,
    std::optional<std::size_t> interrupt_after_writes = std::nullopt);
[[nodiscard]] TransitionPlan plan_f2_transition(
    const ResidentState& before, const BranchTableArtifact& artifact,
    const BranchFacts& after_facts);
void write_transition_summary(std::ostream& output);
void write_f2_transition_summary(std::ostream& output);
[[nodiscard]] bool transition_self_test(std::ostream& errors);
[[nodiscard]] bool f2_transition_self_test(std::ostream& errors);

}  // namespace residual

#endif
