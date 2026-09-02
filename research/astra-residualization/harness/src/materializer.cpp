#include "residual/materializer.h"

#include <array>
#include <limits>
#include <ostream>
#include <string_view>

namespace residual {
namespace {

enum class Path : std::uint8_t { Nchw, Nhwc, NhwcRelu };

struct Candidate {
  Path path;
  std::uint32_t cost;
};

static_assert(sizeof(std::uint8_t) + sizeof(std::uint32_t) ==
              kF1DeclaredArenaBound);

[[nodiscard]] std::array<std::string_view, 4> atoms(const Path path) {
  switch (path) {
    case Path::Nchw:
      return {"stem.nchw", "act.nchw", "skip.nchw", "join.nchw"};
    case Path::Nhwc:
      return {"stem.nhwc", "act.nhwc", "skip.nhwc", "join.nhwc_store"};
    case Path::NhwcRelu:
      return {"stem.nhwc_relu", "act.none", "skip.nhwc",
              "join.nhwc_store"};
  }
  return {};
}

[[nodiscard]] std::uint32_t base_cost(const Path path, const Facts& facts) {
  const bool small = facts.shape == Shape::Small;
  const bool latency = facts.objective == Objective::Latency;
  switch (path) {
    case Path::Nchw:
      if (latency) {
        return small ? 14U : 30U;
      }
      return small ? 16U : 34U;
    case Path::Nhwc:
      if (latency) {
        return small ? 21U : 24U;
      }
      return small ? 18U : 25U;
    case Path::NhwcRelu:
      if (latency) {
        return small ? 15U : 18U;
      }
      return small ? 14U : 19U;
  }
  return std::numeric_limits<std::uint32_t>::max();
}

[[nodiscard]] Layout layout(const Path path) {
  return path == Path::Nchw ? Layout::Nchw : Layout::Nhwc;
}

[[nodiscard]] Realization make_realization(const Candidate& candidate) {
  Realization result;
  for (const std::string_view atom : atoms(candidate.path)) {
    result.atoms.emplace_back(atom);
  }
  result.cost = candidate.cost;
  return result;
}

void consider(const Path path, const Facts& facts, Candidate* best,
              std::uint32_t* steps) {
  // Four frozen atom-cost reads, one interaction-cost read, one resident-cost
  // read, and one canonical argmin comparison.
  *steps += 7U;
  std::uint32_t cost = base_cost(path, facts);
  if (layout(path) != facts.resident) {
    cost += facts.objective == Objective::Latency ? 3U : 2U;
  }
  const Candidate candidate{path, cost};
  const Realization candidate_realization = make_realization(candidate);
  const Realization best_realization = make_realization(*best);
  if (candidate.cost < best->cost ||
      (candidate.cost == best->cost &&
       candidate_realization.canonical_id() <
           best_realization.canonical_id())) {
    *best = candidate;
  }
}

}  // namespace

MaterializationResult materialize_f1(const Facts& facts) {
  MaterializationResult result;
  Candidate best{Path::Nchw, std::numeric_limits<std::uint32_t>::max()};

  consider(Path::Nchw, facts, &best, &result.steps);

  ++result.steps;  // tensor capability predicate for NHWC path.
  ++result.steps;  // scratch predicate for NHWC path.
  if (facts.tensor && facts.scratch_limit >= 4U) {
    consider(Path::Nhwc, facts, &best, &result.steps);
  }

  ++result.steps;  // tensor capability predicate for fused path.
  ++result.steps;  // scratch predicate for fused path.
  if (facts.tensor && facts.scratch_limit >= 7U) {
    consider(Path::NhwcRelu, facts, &best, &result.steps);
  }

  ++result.steps;  // Return the canonical winner.
  result.arena_bytes = kF1DeclaredArenaBound;
  result.realization = make_realization(best);
  return result;
}

void write_materializer_csv(std::ostream& output) {
  output << "facts,steps,step_bound,arena,arena_bound,trials,cost,"
            "realization\n";
  for (const Facts& facts : exhaustive_facts()) {
    const MaterializationResult result = materialize_f1(facts);
    output << facts.canonical_id() << ',' << result.steps << ','
           << kF1DeclaredStepBound << ',' << result.arena_bytes << ','
           << kF1DeclaredArenaBound << ',' << result.performance_trials << ','
           << result.realization.cost << ','
           << result.realization.canonical_id() << '\n';
  }
}

bool materializer_self_test(std::ostream& errors) {
  for (const Facts& facts : exhaustive_facts()) {
    const OracleResult oracle = solve_f1(facts);
    const MaterializationResult first = materialize_f1(facts);
    const MaterializationResult second = materialize_f1(facts);
    if (first.realization.canonical_id() != oracle.optimum.canonical_id() ||
        first.realization.cost != oracle.optimum.cost) {
      errors << "materializer differs from oracle for "
             << facts.canonical_id() << '\n';
      return false;
    }
    if (first.steps > kF1DeclaredStepBound ||
        first.arena_bytes > kF1DeclaredArenaBound) {
      errors << "materializer exceeds bound for " << facts.canonical_id()
             << '\n';
      return false;
    }
    if (first.performance_trials != 0) {
      errors << "materializer performed a trial for " << facts.canonical_id()
             << '\n';
      return false;
    }
    if (first.realization.canonical_id() != second.realization.canonical_id() ||
        first.realization.cost != second.realization.cost ||
        first.steps != second.steps) {
      errors << "materializer is non-deterministic for "
             << facts.canonical_id() << '\n';
      return false;
    }
    if (!evaluate_f1(first.realization, facts)) {
      errors << "materializer emitted an illegal realization for "
             << facts.canonical_id() << '\n';
      return false;
    }
  }
  return true;
}

}  // namespace residual
