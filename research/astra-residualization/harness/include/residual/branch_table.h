#ifndef JOGGLE_RESEARCH_RESIDUAL_BRANCH_TABLE_H
#define JOGGLE_RESEARCH_RESIDUAL_BRANCH_TABLE_H

#include "residual/branch_oracle.h"
#include "residual/materializer.h"

#include <array>
#include <cstdint>
#include <iosfwd>
#include <vector>

namespace residual {

struct BranchTableArtifact {
  std::vector<Realization> terminals;
  std::array<std::uint8_t, 64> winner{};

  [[nodiscard]] std::vector<std::uint8_t> serialize() const;
};

[[nodiscard]] BranchTableArtifact synthesize_f2_table();
[[nodiscard]] MaterializationResult materialize_f2(
    const BranchTableArtifact& artifact, const BranchFacts& facts);
void write_f2_table_csv(std::ostream& output);
[[nodiscard]] bool f2_table_self_test(std::ostream& errors);

}  // namespace residual

#endif
