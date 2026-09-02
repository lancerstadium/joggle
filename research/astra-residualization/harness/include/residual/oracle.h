#ifndef JOGGLE_RESEARCH_RESIDUAL_ORACLE_H
#define JOGGLE_RESEARCH_RESIDUAL_ORACLE_H

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace residual {

enum class Shape { Small, Large };
enum class Objective { Latency, Energy };
enum class Layout { Nchw, Nhwc };

struct Facts {
  Shape shape = Shape::Small;
  bool tensor = false;
  std::uint32_t scratch_limit = 0;
  Objective objective = Objective::Latency;
  Layout resident = Layout::Nchw;

  [[nodiscard]] std::string canonical_id() const;
};

struct Realization {
  std::vector<std::string> atoms;
  std::uint32_t cost = 0;

  [[nodiscard]] std::string canonical_id() const;
};

struct OracleResult {
  std::size_t assignments_visited = 0;
  std::vector<Realization> legal;
  Realization optimum;
};

[[nodiscard]] std::vector<Facts> exhaustive_facts();
[[nodiscard]] std::uint8_t f1_fact_bits(const Facts& facts);
[[nodiscard]] std::vector<Realization> f1_realization_universe();
[[nodiscard]] std::optional<Realization> evaluate_f1(
    const Realization& realization, const Facts& facts);
[[nodiscard]] OracleResult solve_f1(const Facts& facts);
void write_csv(std::ostream& output);
[[nodiscard]] bool self_test(std::ostream& errors);

}  // namespace residual

#endif
