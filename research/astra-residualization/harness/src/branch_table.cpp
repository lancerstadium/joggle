#include "residual/branch_table.h"

#include <algorithm>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <string>

namespace residual {
namespace {

void append_u8(std::vector<std::uint8_t>* bytes, const std::uint8_t value) {
  bytes->push_back(value);
}

void append_string(std::vector<std::uint8_t>* bytes, const std::string& value) {
  if (value.size() > std::numeric_limits<std::uint8_t>::max()) {
    throw std::length_error("F2 artifact string exceeds u8 length");
  }
  append_u8(bytes, static_cast<std::uint8_t>(value.size()));
  for (const char character : value) {
    append_u8(bytes, static_cast<std::uint8_t>(character));
  }
}

[[nodiscard]] std::uint8_t terminal_index(
    const std::vector<Realization>& terminals, const std::string& id) {
  const auto found = std::lower_bound(
      terminals.begin(), terminals.end(), id,
      [](const Realization& realization, const std::string& value) {
        return realization.canonical_id() < value;
      });
  if (found == terminals.end() || found->canonical_id() != id) {
    throw std::logic_error("F2 winner is absent from table terminals");
  }
  return static_cast<std::uint8_t>(
      static_cast<std::size_t>(found - terminals.begin()));
}

}  // namespace

std::vector<std::uint8_t> BranchTableArtifact::serialize() const {
  if (terminals.size() > std::numeric_limits<std::uint8_t>::max()) {
    throw std::length_error("too many F2 table terminals");
  }
  std::vector<std::uint8_t> bytes{'J', 'F', '2', 'T'};
  append_u8(&bytes, 1U);  // Version.
  append_u8(&bytes, 6U);  // Binary fact dimensions.
  append_u8(&bytes, static_cast<std::uint8_t>(terminals.size()));
  for (const Realization& terminal : terminals) {
    append_string(&bytes, terminal.canonical_id());
  }
  append_u8(&bytes, static_cast<std::uint8_t>(winner.size()));
  for (const std::uint8_t terminal : winner) {
    append_u8(&bytes, terminal);
  }
  append_u8(&bytes, terminal_index(terminals,
                                    f2_realization_universe().front()
                                        .canonical_id()));
  return bytes;
}

BranchTableArtifact synthesize_f2_table() {
  BranchTableArtifact artifact;
  for (const BranchFacts& facts : exhaustive_branch_facts()) {
    artifact.terminals.push_back(solve_f2(facts).optimum);
  }
  artifact.terminals.push_back(f2_realization_universe().front());
  std::sort(artifact.terminals.begin(), artifact.terminals.end(),
            [](const Realization& lhs, const Realization& rhs) {
              return lhs.canonical_id() < rhs.canonical_id();
            });
  artifact.terminals.erase(
      std::unique(artifact.terminals.begin(), artifact.terminals.end(),
                  [](const Realization& lhs, const Realization& rhs) {
                    return lhs.canonical_id() == rhs.canonical_id();
                  }),
      artifact.terminals.end());
  for (const BranchFacts& facts : exhaustive_branch_facts()) {
    artifact.winner[f2_fact_bits(facts)] = terminal_index(
        artifact.terminals, solve_f2(facts).optimum.canonical_id());
  }
  return artifact;
}

MaterializationResult materialize_f2(const BranchTableArtifact& artifact,
                                     const BranchFacts& facts) {
  MaterializationResult result;
  const std::uint8_t terminal = artifact.winner[f2_fact_bits(facts)];
  if (terminal >= artifact.terminals.size()) {
    throw std::logic_error("invalid F2 table terminal");
  }
  result.steps = 2U;  // Fact-vector index and table lookup/return.
  result.arena_bytes = 1U;
  result.realization = artifact.terminals[terminal];
  const std::optional<Realization> evaluated = evaluate_f2(result.realization, facts);
  if (evaluated) {
    result.realization.cost = evaluated->cost;
  }
  return result;
}

void write_f2_table_csv(std::ostream& output) {
  const BranchTableArtifact artifact = synthesize_f2_table();
  output << "bytes,terminals,facts,steps,arena,trials,cost,realization\n";
  for (const BranchFacts& facts : exhaustive_branch_facts()) {
    const MaterializationResult result = materialize_f2(artifact, facts);
    output << artifact.serialize().size() << ',' << artifact.terminals.size()
           << ',' << facts.canonical_id() << ',' << result.steps << ','
           << result.arena_bytes << ',' << result.performance_trials << ','
           << result.realization.cost << ','
           << result.realization.canonical_id() << '\n';
  }
}

bool f2_table_self_test(std::ostream& errors) {
  const BranchTableArtifact first = synthesize_f2_table();
  const BranchTableArtifact second = synthesize_f2_table();
  if (first.serialize() != second.serialize()) {
    errors << "F2 table synthesis is non-deterministic\n";
    return false;
  }
  for (const BranchFacts& facts : exhaustive_branch_facts()) {
    const MaterializationResult selected = materialize_f2(first, facts);
    const BranchOracleResult oracle = solve_f2(facts);
    if (selected.realization.canonical_id() !=
            oracle.optimum.canonical_id() ||
        selected.realization.cost != oracle.optimum.cost ||
        selected.performance_trials != 0 ||
        !evaluate_f2(selected.realization, facts)) {
      errors << "F2 table differs from oracle for " << facts.canonical_id()
             << '\n';
      return false;
    }
  }
  return true;
}

}  // namespace residual
