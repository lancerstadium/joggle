#include "residual/oracle.h"

#include <algorithm>
#include <array>
#include <limits>
#include <ostream>
#include <sstream>
#include <string_view>

namespace residual {
namespace {

struct Cost {
  std::uint32_t latency_small;
  std::uint32_t latency_large;
  std::uint32_t energy_small;
  std::uint32_t energy_large;
};

struct Atom {
  std::string_view id;
  std::uint32_t scratch;
  bool requires_tensor;
  Cost cost;
};

constexpr std::array<Atom, 3> kStem{{
    {"stem.nchw", 1, false, {8, 20, 10, 24}},
    {"stem.nhwc", 1, true, {10, 12, 8, 13}},
    {"stem.nhwc_relu", 4, true, {8, 9, 7, 10}},
}};

constexpr std::array<Atom, 3> kActivation{{
    {"act.nchw", 1, false, {3, 5, 3, 5}},
    {"act.nhwc", 0, false, {2, 3, 2, 3}},
    {"act.none", 0, false, {0, 0, 0, 0}},
}};

constexpr std::array<Atom, 2> kSkip{{
    {"skip.nchw", 0, false, {1, 2, 1, 2}},
    {"skip.nhwc", 1, false, {3, 4, 4, 5}},
}};

constexpr std::array<Atom, 2> kJoin{{
    {"join.nchw", 1, false, {2, 3, 2, 3}},
    {"join.nhwc_store", 2, false, {4, 5, 3, 4}},
}};

[[nodiscard]] bool is(const Atom& atom, const std::string_view id) {
  return atom.id == id;
}

[[nodiscard]] std::uint32_t atom_cost(const Atom& atom,
                                      const Facts& facts) {
  if (facts.objective == Objective::Latency) {
    return facts.shape == Shape::Small ? atom.cost.latency_small
                                       : atom.cost.latency_large;
  }
  return facts.shape == Shape::Small ? atom.cost.energy_small
                                     : atom.cost.energy_large;
}

[[nodiscard]] Layout stem_layout(const Atom& stem) {
  return is(stem, "stem.nchw") ? Layout::Nchw : Layout::Nhwc;
}

[[nodiscard]] bool structurally_legal(const Atom& stem,
                                      const Atom& activation,
                                      const Atom& skip,
                                      const Atom& join) {
  const bool stem_activation =
      (is(stem, "stem.nchw") && is(activation, "act.nchw")) ||
      (is(stem, "stem.nhwc") && is(activation, "act.nhwc")) ||
      (is(stem, "stem.nhwc_relu") && is(activation, "act.none"));
  if (!stem_activation) {
    return false;
  }

  const bool nchw_join = is(join, "join.nchw") &&
                         is(stem, "stem.nchw") && is(skip, "skip.nchw");
  const bool nhwc_join = is(join, "join.nhwc_store") &&
                         !is(stem, "stem.nchw") && is(skip, "skip.nhwc");
  return nchw_join || nhwc_join;
}

[[nodiscard]] bool fact_legal(const std::array<const Atom*, 4>& atoms,
                              const Facts& facts) {
  std::uint32_t scratch = 0;
  for (const Atom* atom : atoms) {
    if (atom->requires_tensor && !facts.tensor) {
      return false;
    }
    scratch += atom->scratch;
  }
  return scratch <= facts.scratch_limit;
}

[[nodiscard]] std::uint32_t realization_cost(
    const Atom& stem, const Atom& activation, const Atom& skip,
    const Atom& join, const Facts& facts) {
  std::uint32_t result = atom_cost(stem, facts) + atom_cost(activation, facts) +
                         atom_cost(skip, facts) + atom_cost(join, facts);

  const bool unfused_nhwc = is(stem, "stem.nhwc") &&
                            is(activation, "act.nhwc") &&
                            is(join, "join.nhwc_store");
  if (unfused_nhwc && facts.shape == Shape::Small) {
    result += facts.objective == Objective::Latency ? 2U : 1U;
  }
  if (stem_layout(stem) != facts.resident) {
    result += facts.objective == Objective::Latency ? 3U : 2U;
  }
  return result;
}

[[nodiscard]] std::string fact_name(const Facts& facts) {
  std::ostringstream name;
  name << (facts.shape == Shape::Small ? "small" : "large") << ','
       << (facts.tensor ? "on" : "off") << ',' << facts.scratch_limit
       << ','
       << (facts.objective == Objective::Latency ? "latency" : "energy")
       << ',' << (facts.resident == Layout::Nchw ? "nchw" : "nhwc");
  return name.str();
}

[[nodiscard]] const Atom* find_atom(const std::string& id) {
  const auto find_in = [&id](const auto& atoms) -> const Atom* {
    const auto found = std::find_if(
        atoms.begin(), atoms.end(),
        [&id](const Atom& atom) { return atom.id == id; });
    return found == atoms.end() ? nullptr : &*found;
  };
  if (const Atom* atom = find_in(kStem)) {
    return atom;
  }
  if (const Atom* atom = find_in(kActivation)) {
    return atom;
  }
  if (const Atom* atom = find_in(kSkip)) {
    return atom;
  }
  return find_in(kJoin);
}

}  // namespace

std::string Facts::canonical_id() const { return fact_name(*this); }

std::string Realization::canonical_id() const {
  std::ostringstream id;
  for (std::size_t index = 0; index < atoms.size(); ++index) {
    if (index != 0) {
      id << '|';
    }
    id << atoms[index];
  }
  return id.str();
}

std::vector<Facts> exhaustive_facts() {
  std::vector<Facts> result;
  result.reserve(32);
  for (const Shape shape : {Shape::Small, Shape::Large}) {
    for (const bool tensor : {false, true}) {
      for (const std::uint32_t scratch : {4U, 8U}) {
        for (const Objective objective : {Objective::Latency,
                                          Objective::Energy}) {
          for (const Layout resident : {Layout::Nchw, Layout::Nhwc}) {
            result.push_back({shape, tensor, scratch, objective, resident});
          }
        }
      }
    }
  }
  return result;
}

std::vector<Realization> f1_realization_universe() {
  return {
      {{"stem.nchw", "act.nchw", "skip.nchw", "join.nchw"}, 0},
      {{"stem.nhwc", "act.nhwc", "skip.nhwc", "join.nhwc_store"}, 0},
      {{"stem.nhwc_relu", "act.none", "skip.nhwc", "join.nhwc_store"},
       0},
  };
}

std::optional<Realization> evaluate_f1(const Realization& realization,
                                       const Facts& facts) {
  if (realization.atoms.size() != 4) {
    return std::nullopt;
  }
  std::array<const Atom*, 4> atoms{};
  for (std::size_t index = 0; index < realization.atoms.size(); ++index) {
    atoms[index] = find_atom(realization.atoms[index]);
    if (atoms[index] == nullptr) {
      return std::nullopt;
    }
  }
  const Atom& stem = *atoms[0];
  const Atom& activation = *atoms[1];
  const Atom& skip = *atoms[2];
  const Atom& join = *atoms[3];
  if (!structurally_legal(stem, activation, skip, join) ||
      !fact_legal(atoms, facts)) {
    return std::nullopt;
  }
  Realization evaluated = realization;
  evaluated.cost = realization_cost(stem, activation, skip, join, facts);
  return evaluated;
}

OracleResult solve_f1(const Facts& facts) {
  OracleResult result;
  for (const Atom& stem : kStem) {
    for (const Atom& activation : kActivation) {
      for (const Atom& skip : kSkip) {
        for (const Atom& join : kJoin) {
          ++result.assignments_visited;
          if (!structurally_legal(stem, activation, skip, join)) {
            continue;
          }
          const std::array<const Atom*, 4> atoms{
              &stem, &activation, &skip, &join};
          if (!fact_legal(atoms, facts)) {
            continue;
          }

          Realization realization;
          realization.atoms = {std::string(stem.id), std::string(activation.id),
                               std::string(skip.id), std::string(join.id)};
          realization.cost =
              realization_cost(stem, activation, skip, join, facts);
          result.legal.push_back(std::move(realization));
        }
      }
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

void write_csv(std::ostream& output) {
  output << "shape,tensor,scratch,objective,resident,assignments,legal,"
            "optimum_cost,optimum\n";
  for (const Facts& facts : exhaustive_facts()) {
    const OracleResult result = solve_f1(facts);
    output << fact_name(facts) << ',' << result.assignments_visited << ','
           << result.legal.size() << ',' << result.optimum.cost << ','
           << result.optimum.canonical_id() << '\n';
  }
}

bool self_test(std::ostream& errors) {
  bool ok = true;
  std::size_t total_assignments = 0;
  for (const Facts& facts : exhaustive_facts()) {
    const OracleResult first = solve_f1(facts);
    const OracleResult second = solve_f1(facts);
    total_assignments += first.assignments_visited;

    if (first.assignments_visited != 36) {
      errors << "expected 36 assignments for " << fact_name(facts) << '\n';
      ok = false;
    }
    const std::size_t expected_legal =
        !facts.tensor ? 1U : (facts.scratch_limit == 4U ? 2U : 3U);
    if (first.legal.size() != expected_legal) {
      errors << "unexpected legal count for " << fact_name(facts) << ": "
             << first.legal.size() << " != " << expected_legal << '\n';
      ok = false;
    }
    if (first.optimum.canonical_id() != second.optimum.canonical_id() ||
        first.optimum.cost != second.optimum.cost) {
      errors << "non-deterministic optimum for " << fact_name(facts) << '\n';
      ok = false;
    }
    const auto optimum = std::find_if(
        first.legal.begin(), first.legal.end(),
        [&first](const Realization& realization) {
          return realization.canonical_id() == first.optimum.canonical_id();
        });
    if (optimum == first.legal.end()) {
      errors << "optimum is absent from legal set for " << fact_name(facts)
             << '\n';
      ok = false;
    }
  }
  if (total_assignments != 1152) {
    errors << "expected 1152 total assignments, got " << total_assignments
           << '\n';
    ok = false;
  }
  return ok;
}

}  // namespace residual
