#ifndef JOGGLE_RESEARCH_RESIDUAL_BRANCH_ORACLE_H
#define JOGGLE_RESEARCH_RESIDUAL_BRANCH_ORACLE_H

#include "residual/oracle.h"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace residual {

struct BranchFacts {
  Shape shape = Shape::Small;
  bool tensor = false;
  bool copy = false;
  std::uint8_t vec_lanes = 1;
  bool fine_events = false;
  Objective objective = Objective::Latency;

  [[nodiscard]] std::string canonical_id() const;
};

struct BranchOracleResult {
  std::size_t assignments_visited = 0;
  std::vector<Realization> legal;
  Realization optimum;
};

[[nodiscard]] std::vector<BranchFacts> exhaustive_branch_facts();
[[nodiscard]] std::uint8_t f2_fact_bits(const BranchFacts& facts);
[[nodiscard]] std::vector<Realization> f2_realization_universe();
[[nodiscard]] std::optional<Realization> evaluate_f2(
    const Realization& realization, const BranchFacts& facts);
[[nodiscard]] BranchOracleResult solve_f2(const BranchFacts& facts);
void write_f2_csv(std::ostream& output);
[[nodiscard]] bool f2_oracle_self_test(std::ostream& errors);

}  // namespace residual

#endif
