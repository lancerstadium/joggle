#include "residual/conditioned.h"

#include "residual/artifact_encoding.h"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <limits>
#include <map>
#include <ostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace residual {
namespace {

struct Cube {
  std::uint8_t specified = 0;
  std::uint8_t values = 0;
  std::uint32_t coverage = 0;
};

struct CoverNode {
  std::uint32_t count = std::numeric_limits<std::uint32_t>::max();
  std::uint32_t prior = 0;
  std::size_t cube = 0;
};

[[nodiscard]] Realization realization_by_id(const std::string& id);

[[nodiscard]] bool matches(const Cube& cube, const Facts& facts) {
  return ((f1_fact_bits(facts) ^ cube.values) & cube.specified) == 0;
}

[[nodiscard]] bool matches(const GuardedVariant& entry,
                           const Facts& facts) {
  return ((f1_fact_bits(facts) ^ entry.value_mask) & entry.specified_mask) == 0;
}

[[nodiscard]] std::vector<Cube> valid_cubes(
    const std::vector<Facts>& facts, const std::vector<std::string>& winners,
    const std::string& target) {
  const Realization action = realization_by_id(target);
  const std::vector<Facts> admissible = exhaustive_facts();
  std::map<std::uint32_t, Cube> best_by_coverage;
  for (std::uint32_t ternary = 0; ternary < 243U; ++ternary) {
    std::uint32_t digits = ternary;
    Cube cube;
    for (std::uint32_t dimension = 0; dimension < 5U; ++dimension) {
      const std::uint32_t digit = digits % 3U;
      digits /= 3U;
      if (digit == 0U) {
        continue;
      }
      cube.specified |= static_cast<std::uint8_t>(1U << dimension);
      if (digit == 2U) {
        cube.values |= static_cast<std::uint8_t>(1U << dimension);
      }
    }

    bool valid = true;
    for (std::size_t index = 0; index < facts.size(); ++index) {
      if (!matches(cube, facts[index])) {
        continue;
      }
      if (winners[index] != target) {
        valid = false;
        break;
      }
      cube.coverage |= static_cast<std::uint32_t>(1U << index);
    }
    if (valid) {
      for (const Facts& point : admissible) {
        if (matches(cube, point) && !evaluate_f1(action, point)) {
          valid = false;
          break;
        }
      }
    }
    if (!valid || cube.coverage == 0U) {
      continue;
    }
    const auto found = best_by_coverage.find(cube.coverage);
    if (found == best_by_coverage.end() ||
        std::popcount(cube.specified) <
            std::popcount(found->second.specified) ||
        (std::popcount(cube.specified) ==
             std::popcount(found->second.specified) &&
         std::pair(cube.specified, cube.values) <
             std::pair(found->second.specified, found->second.values))) {
      best_by_coverage[cube.coverage] = cube;
    }
  }
  std::vector<Cube> result;
  result.reserve(best_by_coverage.size());
  for (const auto& [coverage, cube] : best_by_coverage) {
    (void)coverage;
    result.push_back(cube);
  }
  std::sort(result.begin(), result.end(), [](const Cube& lhs, const Cube& rhs) {
    if (std::popcount(lhs.coverage) != std::popcount(rhs.coverage)) {
      return std::popcount(lhs.coverage) > std::popcount(rhs.coverage);
    }
    if (std::popcount(lhs.specified) != std::popcount(rhs.specified)) {
      return std::popcount(lhs.specified) < std::popcount(rhs.specified);
    }
    return std::pair(lhs.specified, lhs.values) <
           std::pair(rhs.specified, rhs.values);
  });
  return result;
}

[[nodiscard]] std::vector<Cube> minimum_cover(const std::vector<Cube>& cubes,
                                              const std::uint32_t target) {
  std::unordered_map<std::uint32_t, CoverNode> nodes;
  nodes.emplace(0U, CoverNode{0U, 0U, 0U});
  std::vector<std::uint32_t> frontier{0U};
  for (std::size_t cursor = 0; cursor < frontier.size(); ++cursor) {
    const std::uint32_t covered = frontier[cursor];
    const CoverNode current = nodes.at(covered);
    if (covered == target) {
      break;
    }
    const std::uint32_t remaining = target & ~covered;
    const std::uint32_t pivot = remaining & (~remaining + 1U);
    for (std::size_t cube_index = 0; cube_index < cubes.size(); ++cube_index) {
      if ((cubes[cube_index].coverage & pivot) == 0U) {
        continue;
      }
      const std::uint32_t next = covered | cubes[cube_index].coverage;
      const std::uint32_t next_count = current.count + 1U;
      const auto found = nodes.find(next);
      if (found == nodes.end()) {
        nodes.emplace(next, CoverNode{next_count, covered, cube_index});
        frontier.push_back(next);
      } else if (next_count < found->second.count) {
        found->second = {next_count, covered, cube_index};
      }
    }
  }
  const auto final = nodes.find(target);
  if (final == nodes.end()) {
    throw std::runtime_error("conditioned baseline cannot cover winner set");
  }
  std::vector<Cube> result;
  std::uint32_t state = target;
  while (state != 0U) {
    const CoverNode& node = nodes.at(state);
    result.push_back(cubes[node.cube]);
    state = node.prior;
  }
  std::reverse(result.begin(), result.end());
  return result;
}

[[nodiscard]] Realization realization_by_id(const std::string& id) {
  for (const Realization& realization : f1_realization_universe()) {
    if (realization.canonical_id() == id) {
      return realization;
    }
  }
  throw std::invalid_argument("unknown F1 realization: " + id);
}

[[nodiscard]] std::uint32_t guard_steps(const GuardedVariant& entry,
                                        const Facts& facts) {
  std::uint32_t steps = 1U;  // Entry dispatch.
  const std::uint8_t bits = f1_fact_bits(facts);
  for (std::uint32_t dimension = 0; dimension < 5U; ++dimension) {
    const std::uint8_t bit = static_cast<std::uint8_t>(1U << dimension);
    if ((entry.specified_mask & bit) == 0U) {
      continue;
    }
    ++steps;
    if (((bits ^ entry.value_mask) & bit) != 0U) {
      break;
    }
  }
  return steps;
}

}  // namespace

std::vector<std::uint8_t> ConditionedArtifact::serialize() const {
  std::vector<Realization> paths;
  paths.reserve(entries.size());
  for (const GuardedVariant& entry : entries) {
    paths.push_back(entry.realization);
  }
  EncodedPaths encoded = encode_f1_paths(paths, false);
  append_u8(&encoded.bytes, 1U);  // Conditioned-variant selector tag.
  if (entries.size() > std::numeric_limits<std::uint8_t>::max()) {
    throw std::length_error("too many conditioned entries");
  }
  append_u8(&encoded.bytes, static_cast<std::uint8_t>(entries.size()));
  for (const GuardedVariant& entry : entries) {
    append_u8(&encoded.bytes, entry.specified_mask);
    append_u8(&encoded.bytes, entry.value_mask);
    append_u8(&encoded.bytes, encoded.index_of(entry.realization));
  }
  return encoded.bytes;
}

std::uint32_t ConditionedArtifact::declared_step_bound() const {
  std::uint32_t bound = 1U;  // Fallback return.
  for (const GuardedVariant& entry : entries) {
    bound += 1U + static_cast<std::uint32_t>(
                      std::popcount(entry.specified_mask));
  }
  return bound;
}

ConditionedArtifact synthesize_exact_conditioned_f1(
    const std::vector<Facts>& training) {
  if (training.size() > 32U) {
    throw std::invalid_argument("F1 conditioned synthesis supports <=32 facts");
  }
  std::vector<std::string> winners;
  winners.reserve(training.size());
  std::map<std::string, std::uint32_t> winner_masks;
  for (std::size_t index = 0; index < training.size(); ++index) {
    const std::string winner = solve_f1(training[index]).optimum.canonical_id();
    winners.push_back(winner);
    winner_masks[winner] |= static_cast<std::uint32_t>(1U << index);
  }

  ConditionedArtifact artifact;
  for (const auto& [winner, mask] : winner_masks) {
    const std::vector<Cube> cubes = valid_cubes(training, winners, winner);
    for (const Cube& cube : minimum_cover(cubes, mask)) {
      artifact.entries.push_back(
          {cube.specified, cube.values, realization_by_id(winner)});
    }
  }
  std::sort(artifact.entries.begin(), artifact.entries.end(),
            [](const GuardedVariant& lhs, const GuardedVariant& rhs) {
              if (lhs.realization.canonical_id() !=
                  rhs.realization.canonical_id()) {
                return lhs.realization.canonical_id() <
                       rhs.realization.canonical_id();
              }
              return std::pair(lhs.specified_mask, lhs.value_mask) <
                     std::pair(rhs.specified_mask, rhs.value_mask);
            });
  return artifact;
}

ConditionedSelection select_conditioned_f1(
    const ConditionedArtifact& artifact, const Facts& facts) {
  ConditionedSelection result;
  for (const GuardedVariant& entry : artifact.entries) {
    result.steps += guard_steps(entry, facts);
    if (matches(entry, facts)) {
      result.realization = entry.realization;
      return result;
    }
  }
  ++result.steps;
  result.realization = f1_realization_universe().front();
  result.fallback = true;
  return result;
}

void write_conditioned_csv(std::ostream& output) {
  const std::vector<Facts> facts = exhaustive_facts();
  const ConditionedArtifact artifact = synthesize_exact_conditioned_f1(facts);
  output << "entries,bytes,declared_steps,facts,steps,fallback,cost,"
            "realization\n";
  for (const Facts& point : facts) {
    const ConditionedSelection selection = select_conditioned_f1(artifact, point);
    const std::optional<Realization> evaluated =
        evaluate_f1(selection.realization, point);
    output << artifact.entries.size() << ',' << artifact.serialize().size()
           << ',' << artifact.declared_step_bound() << ','
           << point.canonical_id() << ',' << selection.steps << ','
           << (selection.fallback ? 1 : 0) << ','
           << (evaluated ? evaluated->cost : 0U) << ','
           << selection.realization.canonical_id() << '\n';
  }
}

bool conditioned_self_test(std::ostream& errors) {
  const std::vector<Facts> facts = exhaustive_facts();
  const ConditionedArtifact artifact = synthesize_exact_conditioned_f1(facts);
  if (artifact.entries.empty() || artifact.serialize().empty()) {
    errors << "conditioned artifact is empty\n";
    return false;
  }
  for (const Facts& point : facts) {
    const ConditionedSelection selection = select_conditioned_f1(artifact, point);
    const OracleResult oracle = solve_f1(point);
    if (selection.fallback ||
        selection.realization.canonical_id() !=
            oracle.optimum.canonical_id()) {
      errors << "conditioned baseline differs from oracle for "
             << point.canonical_id() << '\n';
      return false;
    }
    if (selection.steps > artifact.declared_step_bound()) {
      errors << "conditioned baseline exceeds step bound for "
             << point.canonical_id() << '\n';
      return false;
    }
    if (!evaluate_f1(selection.realization, point)) {
      errors << "conditioned baseline selected an illegal realization for "
             << point.canonical_id() << '\n';
      return false;
    }
  }
  return true;
}

}  // namespace residual
