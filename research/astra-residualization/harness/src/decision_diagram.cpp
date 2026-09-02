#include "residual/decision_diagram.h"

#include "residual/artifact_encoding.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <ostream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace residual {
namespace {

struct DiagramBuilder {
  std::vector<Realization> terminals;
  std::array<std::uint8_t, 32> winners{};
  std::array<std::uint8_t, 5> order{};
  std::vector<DecisionNode> nodes;
  std::map<std::tuple<std::uint8_t, std::uint32_t, std::uint32_t>,
           std::uint32_t>
      unique;

  [[nodiscard]] std::uint32_t build(
      const std::size_t level, const std::vector<std::uint8_t>& assignments) {
    if (assignments.empty()) {
      throw std::logic_error("empty decision-diagram partition");
    }
    const std::uint8_t first = winners[assignments.front()];
    const bool constant =
        std::all_of(assignments.begin(), assignments.end(),
                    [this, first](const std::uint8_t assignment) {
                      return winners[assignment] == first;
                    });
    if (constant) {
      return first;
    }
    if (level >= order.size()) {
      throw std::logic_error("nonconstant terminal partition");
    }

    const std::uint8_t variable = order[level];
    std::vector<std::uint8_t> low_assignments;
    std::vector<std::uint8_t> high_assignments;
    for (const std::uint8_t assignment : assignments) {
      if ((assignment & static_cast<std::uint8_t>(1U << variable)) == 0U) {
        low_assignments.push_back(assignment);
      } else {
        high_assignments.push_back(assignment);
      }
    }
    const std::uint32_t low = build(level + 1U, low_assignments);
    const std::uint32_t high = build(level + 1U, high_assignments);
    if (low == high) {
      return low;
    }
    const auto key = std::make_tuple(variable, low, high);
    const auto found = unique.find(key);
    if (found != unique.end()) {
      return found->second;
    }
    const std::uint32_t reference = static_cast<std::uint32_t>(
        terminals.size() + nodes.size());
    nodes.push_back({variable, low, high});
    unique.emplace(key, reference);
    return reference;
  }
};

[[nodiscard]] std::vector<Realization> terminal_catalog() {
  std::vector<Realization> terminals = f1_realization_universe();
  std::sort(terminals.begin(), terminals.end(),
            [](const Realization& lhs, const Realization& rhs) {
              return lhs.canonical_id() < rhs.canonical_id();
            });
  terminals.erase(
      std::unique(terminals.begin(), terminals.end(),
                  [](const Realization& lhs, const Realization& rhs) {
                    return lhs.canonical_id() == rhs.canonical_id();
                  }),
      terminals.end());
  return terminals;
}

[[nodiscard]] std::uint8_t terminal_index(
    const std::vector<Realization>& terminals, const std::string& id) {
  const auto found = std::lower_bound(
      terminals.begin(), terminals.end(), id,
      [](const Realization& realization, const std::string& value) {
        return realization.canonical_id() < value;
      });
  if (found == terminals.end() || found->canonical_id() != id) {
    throw std::logic_error("oracle winner is absent from terminal catalog");
  }
  return static_cast<std::uint8_t>(
      static_cast<std::size_t>(found - terminals.begin()));
}

[[nodiscard]] DecisionDiagramArtifact build_for_order(
    const std::array<std::uint8_t, 5>& order) {
  DiagramBuilder builder;
  builder.terminals = terminal_catalog();
  builder.order = order;
  for (const Facts& facts : exhaustive_facts()) {
    builder.winners[f1_fact_bits(facts)] = terminal_index(
        builder.terminals, solve_f1(facts).optimum.canonical_id());
  }
  std::vector<std::uint8_t> assignments(32);
  for (std::uint8_t index = 0; index < 32U; ++index) {
    assignments[index] = index;
  }
  DecisionDiagramArtifact artifact;
  artifact.terminals = builder.terminals;
  artifact.variable_order = order;
  artifact.root = builder.build(0U, assignments);
  artifact.nodes = std::move(builder.nodes);
  return artifact;
}

[[nodiscard]] bool better_diagram(const DecisionDiagramArtifact& candidate,
                                  const DecisionDiagramArtifact& current) {
  if (current.terminals.empty()) {
    return true;
  }
  if (candidate.serialize().size() != current.serialize().size()) {
    return candidate.serialize().size() < current.serialize().size();
  }
  if (candidate.declared_step_bound() != current.declared_step_bound()) {
    return candidate.declared_step_bound() < current.declared_step_bound();
  }
  return candidate.variable_order < current.variable_order;
}

[[nodiscard]] std::uint32_t reference_bound(
    const DecisionDiagramArtifact& artifact, const std::uint32_t reference,
    std::vector<std::uint32_t>* memo) {
  if (reference < artifact.terminals.size()) {
    return 1U;
  }
  const std::size_t node_index =
      static_cast<std::size_t>(reference) - artifact.terminals.size();
  if (node_index >= artifact.nodes.size()) {
    throw std::logic_error("invalid decision-diagram reference");
  }
  if ((*memo)[node_index] != 0U) {
    return (*memo)[node_index];
  }
  const DecisionNode& node = artifact.nodes[node_index];
  const std::uint32_t child =
      std::max(reference_bound(artifact, node.low, memo),
               reference_bound(artifact, node.high, memo));
  (*memo)[node_index] = 2U + child;
  return (*memo)[node_index];
}

}  // namespace

std::vector<std::uint8_t> DecisionDiagramArtifact::serialize() const {
  EncodedPaths encoded = encode_f1_paths(terminals, false);
  if (encoded.canonical_ids.size() != terminals.size()) {
    throw std::logic_error("terminal catalog differs from encoded paths");
  }
  append_u8(&encoded.bytes, 2U);  // Reduced MTBDD selector tag.
  for (const std::uint8_t variable : variable_order) {
    append_u8(&encoded.bytes, variable);
  }
  append_u32(&encoded.bytes, static_cast<std::uint32_t>(nodes.size()));
  for (const DecisionNode& node : nodes) {
    append_u8(&encoded.bytes, node.variable);
    append_u32(&encoded.bytes, node.low);
    append_u32(&encoded.bytes, node.high);
  }
  append_u32(&encoded.bytes, root);
  return encoded.bytes;
}

std::uint32_t DecisionDiagramArtifact::declared_step_bound() const {
  std::vector<std::uint32_t> memo(nodes.size(), 0U);
  return reference_bound(*this, root, &memo);
}

DecisionDiagramArtifact synthesize_optimal_mtbdd_f1() {
  std::array<std::uint8_t, 5> order{0, 1, 2, 3, 4};
  DecisionDiagramArtifact best;
  do {
    DecisionDiagramArtifact candidate = build_for_order(order);
    if (better_diagram(candidate, best)) {
      best = std::move(candidate);
    }
  } while (std::next_permutation(order.begin(), order.end()));
  return best;
}

DiagramSelection select_mtbdd_f1(const DecisionDiagramArtifact& artifact,
                                 const Facts& facts) {
  DiagramSelection result;
  std::uint32_t reference = artifact.root;
  const std::uint8_t bits = f1_fact_bits(facts);
  while (reference >= artifact.terminals.size()) {
    const std::size_t node_index =
        static_cast<std::size_t>(reference) - artifact.terminals.size();
    if (node_index >= artifact.nodes.size()) {
      throw std::logic_error("invalid decision-diagram reference");
    }
    const DecisionNode& node = artifact.nodes[node_index];
    result.steps += 2U;
    const bool high =
        (bits & static_cast<std::uint8_t>(1U << node.variable)) != 0U;
    reference = high ? node.high : node.low;
  }
  ++result.steps;
  result.realization = artifact.terminals[reference];
  return result;
}

void write_mtbdd_csv(std::ostream& output) {
  const DecisionDiagramArtifact artifact = synthesize_optimal_mtbdd_f1();
  output << "nodes,bytes,declared_steps,order,facts,steps,cost,realization\n";
  std::string order;
  for (const std::uint8_t variable : artifact.variable_order) {
    order += static_cast<char>('0' + variable);
  }
  for (const Facts& facts : exhaustive_facts()) {
    const DiagramSelection selection = select_mtbdd_f1(artifact, facts);
    const std::optional<Realization> evaluated =
        evaluate_f1(selection.realization, facts);
    output << artifact.nodes.size() << ',' << artifact.serialize().size() << ','
           << artifact.declared_step_bound() << ',' << order << ','
           << facts.canonical_id() << ',' << selection.steps << ','
           << (evaluated ? evaluated->cost : 0U) << ','
           << selection.realization.canonical_id() << '\n';
  }
}

bool mtbdd_self_test(std::ostream& errors) {
  const DecisionDiagramArtifact first = synthesize_optimal_mtbdd_f1();
  const DecisionDiagramArtifact second = synthesize_optimal_mtbdd_f1();
  if (first.serialize() != second.serialize()) {
    errors << "MTBDD synthesis is non-deterministic\n";
    return false;
  }
  for (const Facts& facts : exhaustive_facts()) {
    const DiagramSelection selection = select_mtbdd_f1(first, facts);
    const OracleResult oracle = solve_f1(facts);
    if (selection.realization.canonical_id() !=
        oracle.optimum.canonical_id()) {
      errors << "MTBDD differs from oracle for " << facts.canonical_id()
             << '\n';
      return false;
    }
    if (selection.steps > first.declared_step_bound()) {
      errors << "MTBDD exceeds step bound for " << facts.canonical_id() << '\n';
      return false;
    }
    if (!evaluate_f1(selection.realization, facts)) {
      errors << "MTBDD selected an illegal realization for "
             << facts.canonical_id() << '\n';
      return false;
    }
  }
  return true;
}

}  // namespace residual
