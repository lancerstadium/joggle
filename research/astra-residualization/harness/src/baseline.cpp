#include "residual/baseline.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <ostream>
#include <string>

namespace residual {
namespace {

[[nodiscard]] const Realization& safe_fallback() {
  static const Realization fallback =
      f1_realization_universe().front();
  return fallback;
}

[[nodiscard]] std::string portfolio_id(const Portfolio& portfolio) {
  std::vector<std::string> ids;
  ids.reserve(portfolio.realizations.size());
  for (const Realization& realization : portfolio.realizations) {
    ids.push_back(realization.canonical_id());
  }
  std::sort(ids.begin(), ids.end());
  return std::accumulate(ids.begin(), ids.end(), std::string{},
                         [](std::string result, const std::string& id) {
                           if (!result.empty()) {
                             result += ';';
                           }
                           result += id;
                           return result;
                         });
}

[[nodiscard]] std::optional<Realization> best_stored(
    const Portfolio& portfolio, const Facts& facts) {
  std::optional<Realization> best;
  for (const Realization& realization : portfolio.realizations) {
    const std::optional<Realization> evaluated =
        evaluate_f1(realization, facts);
    if (!evaluated) {
      continue;
    }
    if (!best || evaluated->cost < best->cost ||
        (evaluated->cost == best->cost &&
         evaluated->canonical_id() < best->canonical_id())) {
      best = evaluated;
    }
  }
  return best;
}

[[nodiscard]] std::optional<Realization> evaluated_fallback(
    const Facts& facts) {
  return evaluate_f1(safe_fallback(), facts);
}

[[nodiscard]] bool better_portfolio(const Portfolio& candidate,
                                    const Portfolio& current,
                                    const std::vector<Facts>& training) {
  if (current.realizations.empty()) {
    return true;
  }
  const PortfolioEvaluation lhs = evaluate_portfolio(candidate, training);
  const PortfolioEvaluation rhs = evaluate_portfolio(current, training);
  if (lhs.optimized != rhs.optimized) {
    return lhs.optimized > rhs.optimized;
  }
  const std::uint64_t lhs_excess =
      lhs.total_selected_cost - lhs.total_oracle_cost;
  const std::uint64_t rhs_excess =
      rhs.total_selected_cost - rhs.total_oracle_cost;
  if (lhs_excess != rhs_excess) {
    return lhs_excess < rhs_excess;
  }
  if (candidate.serialized_bytes() != current.serialized_bytes()) {
    return candidate.serialized_bytes() < current.serialized_bytes();
  }
  return portfolio_id(candidate) < portfolio_id(current);
}

void choose_subsets(const std::vector<Realization>& universe,
                    const std::vector<Facts>& training, const std::size_t k,
                    const std::size_t begin, Portfolio* building,
                    Portfolio* best) {
  if (building->realizations.size() == k) {
    if (better_portfolio(*building, *best, training)) {
      *best = *building;
    }
    return;
  }
  const std::size_t missing = k - building->realizations.size();
  for (std::size_t index = begin; index + missing <= universe.size(); ++index) {
    building->realizations.push_back(universe[index]);
    choose_subsets(universe, training, k, index + 1, building, best);
    building->realizations.pop_back();
  }
}

[[nodiscard]] double percentile(std::vector<double> values,
                                const double quantile) {
  if (values.empty()) {
    return 0.0;
  }
  std::sort(values.begin(), values.end());
  const double position =
      quantile * static_cast<double>(values.size() - 1U);
  const std::size_t index = static_cast<std::size_t>(std::ceil(position));
  return values[index];
}

}  // namespace

std::size_t Portfolio::serialized_bytes() const {
  std::size_t bytes = 0;
  for (const Realization& realization : realizations) {
    bytes += realization.canonical_id().size() + 1U;
  }
  return bytes;
}

Portfolio static_one_best(const std::vector<Facts>& training) {
  Portfolio best;
  std::uint64_t best_total = std::numeric_limits<std::uint64_t>::max();
  for (const Realization& candidate : f1_realization_universe()) {
    std::uint64_t total = 0;
    for (const Facts& facts : training) {
      std::optional<Realization> selected = evaluate_f1(candidate, facts);
      if (!selected) {
        selected = evaluated_fallback(facts);
      }
      if (!selected) {
        return {};
      }
      total += selected->cost;
    }
    if (total < best_total ||
        (total == best_total &&
         (best.realizations.empty() ||
          candidate.canonical_id() < best.realizations.front().canonical_id()))) {
      best_total = total;
      best.realizations = {candidate};
    }
  }
  return best;
}

Portfolio exact_top_k(const std::vector<Facts>& training, std::size_t k) {
  const std::vector<Realization> universe = f1_realization_universe();
  k = std::min(k, universe.size());
  Portfolio building;
  Portfolio best;
  choose_subsets(universe, training, k, 0, &building, &best);
  return best;
}

PortfolioEvaluation evaluate_portfolio(const Portfolio& portfolio,
                                       const std::vector<Facts>& facts_set) {
  PortfolioEvaluation result;
  result.fact_points = facts_set.size();
  result.regret.reserve(facts_set.size());
  for (const Facts& facts : facts_set) {
    const OracleResult oracle = solve_f1(facts);
    std::optional<Realization> selected = best_stored(portfolio, facts);
    if (selected) {
      ++result.optimized;
    } else {
      selected = evaluated_fallback(facts);
    }
    if (!selected || oracle.legal.empty()) {
      continue;
    }
    result.total_selected_cost += selected->cost;
    result.total_oracle_cost += oracle.optimum.cost;
    const double excess = static_cast<double>(selected->cost - oracle.optimum.cost);
    result.regret.push_back(excess / static_cast<double>(oracle.optimum.cost));
  }
  return result;
}

void write_baseline_csv(std::ostream& output) {
  const std::vector<Facts> facts = exhaustive_facts();
  output << "baseline,k,bytes,optimized,fact_points,mean_regret,p95_regret,"
            "max_regret,portfolio\n";
  for (std::size_t k = 1; k <= f1_realization_universe().size(); ++k) {
    const Portfolio portfolio =
        k == 1 ? static_one_best(facts) : exact_top_k(facts, k);
    const PortfolioEvaluation evaluated = evaluate_portfolio(portfolio, facts);
    const double mean = evaluated.regret.empty()
                            ? 0.0
                            : std::accumulate(evaluated.regret.begin(),
                                              evaluated.regret.end(), 0.0) /
                                  static_cast<double>(evaluated.regret.size());
    const double maximum =
        evaluated.regret.empty()
            ? 0.0
            : *std::max_element(evaluated.regret.begin(),
                                evaluated.regret.end());
    output << (k == 1 ? "static-one-best" : "exact-top-k") << ',' << k
           << ',' << portfolio.serialized_bytes() << ',' << evaluated.optimized
           << ',' << evaluated.fact_points << ',' << mean << ','
           << percentile(evaluated.regret, 0.95) << ',' << maximum << ','
           << portfolio_id(portfolio) << '\n';
  }
}

bool baseline_self_test(std::ostream& errors) {
  const std::vector<Facts> facts = exhaustive_facts();
  std::size_t prior_optimized = 0;
  std::uint64_t prior_excess = std::numeric_limits<std::uint64_t>::max();
  for (std::size_t k = 1; k <= f1_realization_universe().size(); ++k) {
    const Portfolio portfolio = exact_top_k(facts, k);
    if (portfolio.realizations.size() != k) {
      errors << "top-k portfolio has wrong size for k=" << k << '\n';
      return false;
    }
    const PortfolioEvaluation evaluated = evaluate_portfolio(portfolio, facts);
    const std::uint64_t excess =
        evaluated.total_selected_cost - evaluated.total_oracle_cost;
    if (evaluated.optimized < prior_optimized || excess > prior_excess) {
      errors << "top-k quality is not monotone at k=" << k << '\n';
      return false;
    }
    prior_optimized = evaluated.optimized;
    prior_excess = excess;
  }
  const Portfolio all = exact_top_k(facts, f1_realization_universe().size());
  const PortfolioEvaluation complete = evaluate_portfolio(all, facts);
  if (complete.total_selected_cost != complete.total_oracle_cost) {
    errors << "complete portfolio does not match exhaustive oracle\n";
    return false;
  }
  return true;
}

}  // namespace residual
