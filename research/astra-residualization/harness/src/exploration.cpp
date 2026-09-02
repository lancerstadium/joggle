#include "residual/exploration.h"

#include "residual/artifact_encoding.h"

#include <limits>
#include <ostream>

namespace residual {

std::vector<std::uint8_t> serialize_f1_exploration_space() {
  const std::vector<Realization> paths = f1_realization_universe();
  EncodedPaths encoded = encode_f1_paths(paths, false);
  append_u8(&encoded.bytes, 3U);  // Astra-style exhaustive exploration tag.
  append_u8(&encoded.bytes, static_cast<std::uint8_t>(paths.size()));
  for (const Realization& path : paths) {
    append_u8(&encoded.bytes, encoded.index_of(path));
  }
  return encoded.bytes;
}

ExplorationResult explore_f1(const Facts& facts) {
  ExplorationResult result;
  result.realization.cost = std::numeric_limits<std::uint32_t>::max();
  for (const Realization& path : f1_realization_universe()) {
    const std::optional<Realization> measured = evaluate_f1(path, facts);
    if (!measured) {
      continue;
    }
    ++result.performance_trials;
    result.trial_cost_sum += measured->cost;
    if (measured->cost < result.realization.cost ||
        (measured->cost == result.realization.cost &&
         measured->canonical_id() < result.realization.canonical_id())) {
      result.realization = *measured;
    }
  }
  return result;
}

void write_exploration_csv(std::ostream& output) {
  output << "bytes,facts,trials,trial_cost_sum,cost,realization\n";
  for (const Facts& facts : exhaustive_facts()) {
    const ExplorationResult result = explore_f1(facts);
    output << serialize_f1_exploration_space().size() << ','
           << facts.canonical_id() << ',' << result.performance_trials << ','
           << result.trial_cost_sum << ',' << result.realization.cost << ','
           << result.realization.canonical_id() << '\n';
  }
}

bool exploration_self_test(std::ostream& errors) {
  if (serialize_f1_exploration_space() != serialize_f1_exploration_space()) {
    errors << "exploration-space encoding is non-deterministic\n";
    return false;
  }
  for (const Facts& facts : exhaustive_facts()) {
    const ExplorationResult result = explore_f1(facts);
    const OracleResult oracle = solve_f1(facts);
    if (result.performance_trials != oracle.legal.size() ||
        result.realization.canonical_id() !=
            oracle.optimum.canonical_id() ||
        result.realization.cost != oracle.optimum.cost) {
      errors << "exploration baseline differs from oracle for "
             << facts.canonical_id() << '\n';
      return false;
    }
  }
  return true;
}

}  // namespace residual
