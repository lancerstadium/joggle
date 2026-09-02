#ifndef JOGGLE_RESEARCH_RESIDUAL_DECISION_DIAGRAM_H
#define JOGGLE_RESEARCH_RESIDUAL_DECISION_DIAGRAM_H

#include "residual/oracle.h"

#include <array>
#include <cstdint>
#include <iosfwd>
#include <vector>

namespace residual {

struct DecisionNode {
  std::uint8_t variable = 0;
  std::uint32_t low = 0;
  std::uint32_t high = 0;
};

// Reduced ordered multi-terminal binary decision diagram. This is the binary
// specialization of the MDD baseline in the D3 preregistration.
struct DecisionDiagramArtifact {
  std::vector<Realization> terminals;
  std::vector<DecisionNode> nodes;
  std::uint32_t root = 0;
  std::array<std::uint8_t, 5> variable_order{};

  [[nodiscard]] std::vector<std::uint8_t> serialize() const;
  [[nodiscard]] std::uint32_t declared_step_bound() const;
};

struct DiagramSelection {
  Realization realization;
  std::uint32_t steps = 0;
};

[[nodiscard]] DecisionDiagramArtifact synthesize_optimal_mtbdd_f1();
[[nodiscard]] DiagramSelection select_mtbdd_f1(
    const DecisionDiagramArtifact& artifact, const Facts& facts);
void write_mtbdd_csv(std::ostream& output);
[[nodiscard]] bool mtbdd_self_test(std::ostream& errors);

}  // namespace residual

#endif
