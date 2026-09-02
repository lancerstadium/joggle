#ifndef JOGGLE_RESEARCH_RESIDUAL_BASELINE_H
#define JOGGLE_RESEARCH_RESIDUAL_BASELINE_H

#include "residual/oracle.h"

#include <cstddef>
#include <iosfwd>
#include <vector>

namespace residual {

struct Portfolio {
  std::vector<Realization> realizations;

  [[nodiscard]] std::size_t serialized_bytes() const;
};

struct PortfolioEvaluation {
  std::size_t fact_points = 0;
  std::size_t optimized = 0;
  std::uint64_t total_selected_cost = 0;
  std::uint64_t total_oracle_cost = 0;
  std::vector<double> regret;
};

[[nodiscard]] Portfolio static_one_best(const std::vector<Facts>& training);
[[nodiscard]] Portfolio exact_top_k(const std::vector<Facts>& training,
                                    std::size_t k);
[[nodiscard]] PortfolioEvaluation evaluate_portfolio(
    const Portfolio& portfolio, const std::vector<Facts>& facts);
void write_baseline_csv(std::ostream& output);
[[nodiscard]] bool baseline_self_test(std::ostream& errors);

}  // namespace residual

#endif
