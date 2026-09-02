#include "residual/transition.h"

#include "residual/materializer.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <ostream>
#include <set>
#include <vector>

namespace residual {
namespace {

[[nodiscard]] std::vector<std::string> sorted_atoms(
    const Realization& realization) {
  std::vector<std::string> atoms = realization.atoms;
  std::sort(atoms.begin(), atoms.end());
  return atoms;
}

[[nodiscard]] std::vector<std::string> difference(
    const std::vector<std::string>& lhs,
    const std::vector<std::string>& rhs) {
  std::vector<std::string> result;
  std::set_difference(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(),
                      std::back_inserter(result));
  return result;
}

[[nodiscard]] double percentile(std::vector<double> values,
                                const double quantile) {
  std::sort(values.begin(), values.end());
  const double position =
      quantile * static_cast<double>(values.size() - 1U);
  return values[static_cast<std::size_t>(std::ceil(position))];
}

}  // namespace

TransitionPlan plan_f1_transition(const ResidentState& before,
                                  const Facts& after_facts) {
  const MaterializationResult materialized = materialize_f1(after_facts);
  const std::vector<std::string> old_atoms = sorted_atoms(before.realization);
  const std::vector<std::string> new_atoms =
      sorted_atoms(materialized.realization);

  TransitionPlan plan;
  plan.before = before;
  plan.after = materialized.realization;
  plan.add = difference(new_atoms, old_atoms);
  plan.remove = difference(old_atoms, new_atoms);
  plan.steps = materialized.steps;
  plan.steps += static_cast<std::uint32_t>(new_atoms.size());
  plan.steps += static_cast<std::uint32_t>(old_atoms.size());
  plan.steps += 1U;  // Validate the complete staged realization.
  plan.steps += static_cast<std::uint32_t>(plan.add.size());
  plan.steps += 1U;  // Publish or retain the old generation.
  plan.arena_bytes = kF1TransitionArenaBound;
  plan.write_bound = static_cast<std::uint32_t>(plan.add.size());
  return plan;
}

TransitionOutcome apply_f1_transition(
    const TransitionPlan& plan, const bool validation_passes,
    const std::optional<std::size_t> interrupt_after_writes) {
  TransitionOutcome outcome;
  outcome.resident = plan.before;
  for (std::size_t index = 0; index < plan.add.size(); ++index) {
    if (interrupt_after_writes && index >= *interrupt_after_writes) {
      return outcome;
    }
    ++outcome.staged_writes;
  }
  if (!validation_passes) {
    return outcome;
  }
  if (interrupt_after_writes &&
      *interrupt_after_writes < plan.add.size()) {
    return outcome;
  }
  outcome.resident.realization = plan.after;
  outcome.resident.generation = plan.before.generation + 1U;
  outcome.published = true;
  return outcome;
}

void write_transition_summary(std::ostream& output) {
  const std::vector<Facts> facts = exhaustive_facts();
  std::vector<double> closure_ratios;
  std::size_t avoid_full = 0;
  std::size_t one_fact_avoid_full = 0;
  std::size_t one_fact_transitions = 0;
  std::vector<double> one_fact_ratios;
  std::size_t transitions = 0;
  for (const Facts& old_facts : facts) {
    const ResidentState before{materialize_f1(old_facts).realization, 7U};
    for (const Facts& new_facts : facts) {
      if (old_facts.canonical_id() == new_facts.canonical_id()) {
        continue;
      }
      const TransitionPlan plan = plan_f1_transition(before, new_facts);
      const double ratio = static_cast<double>(plan.add.size()) /
                           static_cast<double>(plan.after.atoms.size());
      closure_ratios.push_back(ratio);
      if (plan.add.size() < plan.after.atoms.size()) {
        ++avoid_full;
      }
      if (std::popcount(static_cast<std::uint8_t>(
              f1_fact_bits(old_facts) ^ f1_fact_bits(new_facts))) == 1) {
        ++one_fact_transitions;
        one_fact_ratios.push_back(ratio);
        if (plan.add.size() < plan.after.atoms.size()) {
          ++one_fact_avoid_full;
        }
      }
      ++transitions;
    }
  }
  output << "transitions,median_closure,p95_closure,max_closure,"
            "avoid_full_fraction,one_fact_transitions,"
            "one_fact_median,one_fact_p95,one_fact_avoid_full_fraction,"
            "step_bound,arena_bound\n";
  output << transitions << ',' << percentile(closure_ratios, 0.5) << ','
         << percentile(closure_ratios, 0.95) << ','
         << percentile(closure_ratios, 1.0) << ','
         << static_cast<double>(avoid_full) /
                static_cast<double>(transitions)
         << ',' << one_fact_transitions << ','
         << percentile(one_fact_ratios, 0.5) << ','
         << percentile(one_fact_ratios, 0.95) << ','
         << static_cast<double>(one_fact_avoid_full) /
                static_cast<double>(one_fact_transitions)
         << ',' << kF1TransitionStepBound << ','
         << kF1TransitionArenaBound << '\n';
}

bool transition_self_test(std::ostream& errors) {
  const std::vector<Facts> facts = exhaustive_facts();
  for (const Facts& old_facts : facts) {
    const ResidentState before{materialize_f1(old_facts).realization, 7U};
    for (const Facts& new_facts : facts) {
      const TransitionPlan plan = plan_f1_transition(before, new_facts);
      const OracleResult oracle = solve_f1(new_facts);
      if (plan.after.canonical_id() != oracle.optimum.canonical_id()) {
        errors << "transition target differs from oracle\n";
        return false;
      }
      const std::vector<std::string> expected_add = difference(
          sorted_atoms(plan.after), sorted_atoms(plan.before.realization));
      if (plan.add != expected_add || plan.write_bound != plan.add.size()) {
        errors << "transition delta is not the minimum atom addition set\n";
        return false;
      }
      if (plan.steps > kF1TransitionStepBound ||
          plan.arena_bytes > kF1TransitionArenaBound) {
        errors << "transition plan exceeds declared bounds\n";
        return false;
      }

      const TransitionOutcome rejected =
          apply_f1_transition(plan, false, std::nullopt);
      if (rejected.published || rejected.resident.generation != 7U ||
          rejected.resident.realization.canonical_id() !=
              before.realization.canonical_id()) {
        errors << "validation failure changed resident generation\n";
        return false;
      }
      for (std::size_t writes = 0; writes < plan.add.size(); ++writes) {
        const TransitionOutcome interrupted =
            apply_f1_transition(plan, true, writes);
        if (interrupted.published || interrupted.resident.generation != 7U ||
            interrupted.resident.realization.canonical_id() !=
                before.realization.canonical_id()) {
          errors << "interrupted staging changed resident generation\n";
          return false;
        }
      }
      const TransitionOutcome committed =
          apply_f1_transition(plan, true, std::nullopt);
      if (!committed.published || committed.resident.generation != 8U ||
          committed.resident.realization.canonical_id() !=
              plan.after.canonical_id()) {
        errors << "valid transition did not publish atomically\n";
        return false;
      }
    }
  }
  return true;
}

}  // namespace residual
