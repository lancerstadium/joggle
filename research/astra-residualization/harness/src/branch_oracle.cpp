#include "residual/branch_oracle.h"

#include <algorithm>
#include <array>
#include <limits>
#include <ostream>
#include <sstream>
#include <string_view>

namespace residual {
namespace {

struct BranchCost {
  std::uint32_t latency_small;
  std::uint32_t latency_large;
  std::uint32_t energy_small;
  std::uint32_t energy_large;
};

struct BranchAtom {
  std::string_view id;
  std::uint8_t vec;
  std::uint8_t tensor;
  std::uint8_t copy;
  BranchCost cost;
};

constexpr std::array<BranchAtom, 2> kLeft{{
    {"left.vec", 1, 0, 0, {8, 16, 6, 12}},
    {"left.tensor", 0, 1, 0, {7, 8, 9, 10}},
}};

constexpr std::array<BranchAtom, 3> kRight{{
    {"right.vec", 1, 0, 0, {7, 13, 5, 9}},
    {"right.tensor", 0, 1, 0, {5, 7, 8, 9}},
    {"right.copyvec", 0, 0, 1, {9, 10, 3, 4}},
}};

constexpr std::array<std::string_view, 2> kSchedules{
    "schedule.serial_global", "schedule.overlap_pair"};

[[nodiscard]] const BranchAtom* find_branch_atom(const std::string& id) {
  const auto left = std::find_if(kLeft.begin(), kLeft.end(),
                                 [&id](const BranchAtom& atom) {
                                   return atom.id == id;
                                 });
  if (left != kLeft.end()) {
    return &*left;
  }
  const auto right = std::find_if(kRight.begin(), kRight.end(),
                                  [&id](const BranchAtom& atom) {
                                    return atom.id == id;
                                  });
  return right == kRight.end() ? nullptr : &*right;
}

[[nodiscard]] std::uint32_t branch_cost(const BranchAtom& atom,
                                        const BranchFacts& facts) {
  if (facts.objective == Objective::Latency) {
    return facts.shape == Shape::Small ? atom.cost.latency_small
                                       : atom.cost.latency_large;
  }
  return facts.shape == Shape::Small ? atom.cost.energy_small
                                     : atom.cost.energy_large;
}

[[nodiscard]] bool capability_legal(const BranchAtom& atom,
                                    const BranchFacts& facts) {
  return (atom.tensor == 0U || facts.tensor) &&
         (atom.copy == 0U || facts.copy) && atom.vec <= facts.vec_lanes;
}

[[nodiscard]] bool overlap_legal(const BranchAtom& left,
                                 const BranchAtom& right,
                                 const BranchFacts& facts) {
  return facts.fine_events &&
         static_cast<std::uint32_t>(left.vec + right.vec) <= facts.vec_lanes &&
         static_cast<std::uint32_t>(left.tensor + right.tensor) <= 1U &&
         static_cast<std::uint32_t>(left.copy + right.copy) <=
             (facts.copy ? 1U : 0U);
}

[[nodiscard]] Realization evaluate_parts(const BranchAtom& left,
                                         const BranchAtom& right,
                                         const bool overlap,
                                         const BranchFacts& facts) {
  Realization result;
  result.atoms = {std::string(left.id), std::string(right.id),
                  std::string(overlap ? kSchedules[1] : kSchedules[0]),
                  "join.add"};
  const std::uint32_t left_cost = branch_cost(left, facts);
  const std::uint32_t right_cost = branch_cost(right, facts);
  if (facts.objective == Objective::Latency) {
    result.cost = overlap ? std::max(left_cost, right_cost) + 1U
                          : left_cost + right_cost + 2U;
  } else {
    result.cost = left_cost + right_cost + (overlap ? 2U : 1U);
  }
  return result;
}

}  // namespace

std::string BranchFacts::canonical_id() const {
  std::ostringstream id;
  id << (shape == Shape::Small ? "small" : "large") << ','
     << (tensor ? "tensor-on" : "tensor-off") << ','
     << (copy ? "copy-on" : "copy-off") << ','
     << (vec_lanes == 1U ? "vec1" : "vec2") << ','
     << (fine_events ? "fine" : "coarse") << ','
     << (objective == Objective::Latency ? "latency" : "energy");
  return id.str();
}

std::vector<BranchFacts> exhaustive_branch_facts() {
  std::vector<BranchFacts> result;
  result.reserve(64);
  for (const Shape shape : {Shape::Small, Shape::Large}) {
    for (const bool tensor : {false, true}) {
      for (const bool copy : {false, true}) {
        for (const std::uint8_t vec_lanes :
             std::array<std::uint8_t, 2>{1U, 2U}) {
          for (const bool fine_events : {false, true}) {
            for (const Objective objective : {Objective::Latency,
                                              Objective::Energy}) {
              result.push_back({shape, tensor, copy, vec_lanes, fine_events,
                                objective});
            }
          }
        }
      }
    }
  }
  return result;
}

std::uint8_t f2_fact_bits(const BranchFacts& facts) {
  std::uint8_t bits = 0;
  bits |= facts.shape == Shape::Large ? 1U << 0U : 0U;
  bits |= facts.tensor ? 1U << 1U : 0U;
  bits |= facts.copy ? 1U << 2U : 0U;
  bits |= facts.vec_lanes == 2U ? 1U << 3U : 0U;
  bits |= facts.fine_events ? 1U << 4U : 0U;
  bits |= facts.objective == Objective::Energy ? 1U << 5U : 0U;
  return bits;
}

std::vector<Realization> f2_realization_universe() {
  std::vector<Realization> result;
  result.reserve(12);
  const BranchFacts canonical{Shape::Small, true, true, 2U, true,
                              Objective::Latency};
  for (const BranchAtom& left : kLeft) {
    for (const BranchAtom& right : kRight) {
      for (const bool overlap : {false, true}) {
        result.push_back(evaluate_parts(left, right, overlap, canonical));
        result.back().cost = 0;
      }
    }
  }
  return result;
}

std::optional<Realization> evaluate_f2(const Realization& realization,
                                       const BranchFacts& facts) {
  if (realization.atoms.size() != 4U || realization.atoms[3] != "join.add") {
    return std::nullopt;
  }
  const BranchAtom* left = find_branch_atom(realization.atoms[0]);
  const BranchAtom* right = find_branch_atom(realization.atoms[1]);
  if (left == nullptr || right == nullptr ||
      !capability_legal(*left, facts) || !capability_legal(*right, facts)) {
    return std::nullopt;
  }
  const bool serial = realization.atoms[2] == kSchedules[0];
  const bool overlap = realization.atoms[2] == kSchedules[1];
  if ((!serial && !overlap) ||
      (overlap && !overlap_legal(*left, *right, facts))) {
    return std::nullopt;
  }
  return evaluate_parts(*left, *right, overlap, facts);
}

BranchOracleResult solve_f2(const BranchFacts& facts) {
  BranchOracleResult result;
  for (const Realization& assignment : f2_realization_universe()) {
    ++result.assignments_visited;
    const std::optional<Realization> evaluated = evaluate_f2(assignment, facts);
    if (evaluated) {
      result.legal.push_back(*evaluated);
    }
  }
  std::sort(result.legal.begin(), result.legal.end(),
            [](const Realization& lhs, const Realization& rhs) {
              if (lhs.cost != rhs.cost) {
                return lhs.cost < rhs.cost;
              }
              return lhs.canonical_id() < rhs.canonical_id();
            });
  if (!result.legal.empty()) {
    result.optimum = result.legal.front();
  } else {
    result.optimum.cost = std::numeric_limits<std::uint32_t>::max();
  }
  return result;
}

void write_f2_csv(std::ostream& output) {
  output << "facts,assignments,legal,optimum_cost,optimum\n";
  for (const BranchFacts& facts : exhaustive_branch_facts()) {
    const BranchOracleResult result = solve_f2(facts);
    output << facts.canonical_id() << ',' << result.assignments_visited << ','
           << result.legal.size() << ',' << result.optimum.cost << ','
           << result.optimum.canonical_id() << '\n';
  }
}

bool f2_oracle_self_test(std::ostream& errors) {
  std::size_t assignments = 0;
  const Realization fallback = f2_realization_universe().front();
  for (const BranchFacts& facts : exhaustive_branch_facts()) {
    const BranchOracleResult first = solve_f2(facts);
    const BranchOracleResult second = solve_f2(facts);
    assignments += first.assignments_visited;
    if (first.assignments_visited != 12U || first.legal.empty() ||
        !evaluate_f2(fallback, facts)) {
      errors << "F2 enumeration/fallback invariant failed for "
             << facts.canonical_id() << '\n';
      return false;
    }
    if (first.optimum.canonical_id() != second.optimum.canonical_id() ||
        first.optimum.cost != second.optimum.cost) {
      errors << "F2 oracle is non-deterministic\n";
      return false;
    }
    for (const Realization& legal : first.legal) {
      const bool overlap = legal.atoms[2] == kSchedules[1];
      if ((!facts.fine_events && overlap) ||
          (facts.vec_lanes == 1U && overlap &&
           legal.atoms[0] == "left.vec" &&
           legal.atoms[1] == "right.vec") ||
          (overlap && legal.atoms[0] == "left.tensor" &&
           legal.atoms[1] == "right.tensor")) {
        errors << "F2 resource/event invariant failed\n";
        return false;
      }
    }
  }
  if (assignments != 768U) {
    errors << "F2 expected 768 assignments, got " << assignments << '\n';
    return false;
  }
  return true;
}

}  // namespace residual
