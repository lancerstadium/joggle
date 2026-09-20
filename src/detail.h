#ifndef JOGGLE_DETAIL_H
#define JOGGLE_DETAIL_H

#include "joggle/joggle.h"

#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace joggle::detail {

inline constexpr std::uint32_t none = std::numeric_limits<std::uint32_t>::max();

template <class T> struct Slot {
  T data;
  std::uint32_t generation = 1;
  bool live = true;
};

enum class ValKind : std::uint8_t { generic, param, blk_arg, result };
enum class Logic : std::uint8_t { none, and_, or_ };

struct ValData {
  ValKind kind = ValKind::result;
  std::uint32_t fn = none;
  std::string name;
  Ty type;
  Attr::Dict meta;
  std::uint32_t def = none;
  std::size_t index = 0;
  std::vector<std::uint32_t> users;
  bool type_annotation = false;

  friend bool operator==(const ValData&, const ValData&) = default;
};

struct OpData {
  Op::Kind kind = Op::Kind::call;
  std::uint32_t blk = none;
  std::string callee;
  std::vector<std::uint32_t> args;
  std::vector<std::uint32_t> outs;
  std::vector<std::uint32_t> blks;
  std::vector<std::string> iter_names;
  std::size_t carried_count = 0;
  Logic logic = Logic::none;
  Attr literal;
  Attr::Dict meta;
  Op::Form form = Op::Form::hidden;
  Loc loc;

  friend bool operator==(const OpData&, const OpData&) = default;
};

struct BlkData {
  std::uint32_t fn = none;
  std::uint32_t parent_op = none;
  std::vector<std::uint32_t> args;
  std::vector<std::uint32_t> ops;
};

struct FnData {
  std::string name;
  std::uint64_t revision = 0;
  std::vector<std::uint32_t> generic_vals;
  std::vector<std::uint32_t> params;
  std::vector<Ty> returns;
  std::vector<std::uint32_t> blks;
  Attr::Dict meta;
  bool local = false;
  bool external = false;
  Loc loc;
};

struct QueryFnData {
  std::uint32_t id = none;
  std::uint32_t generation = 0;
  std::uint64_t revision = 0;
  bool content = true;
};

struct QueryOpData {
  std::uint32_t id = none;
  std::uint32_t generation = 0;
  OpData data;
};

struct QueryValData {
  std::uint32_t id = none;
  std::uint32_t generation = 0;
  ValData data;
};

enum class QueryCollectionKind : std::uint8_t { operations, values };

struct QueryCollectionData {
  QueryCollectionKind kind = QueryCollectionKind::operations;
  std::uint32_t function = none;
  std::uint32_t generation = 0;
  std::vector<std::pair<std::uint32_t, std::uint32_t>> members;
};

struct QueryData {
  std::uint64_t env = 0;
  std::uint64_t epoch = 0;
  std::uint64_t revision = 0;
  std::uint64_t structure_revision = 0;
  bool whole_revision = false;
  bool structure = false;
  bool packages = false;
  std::vector<std::string> package_dependencies;
  std::vector<std::string> intrinsics;
  std::vector<QueryFnData> dependencies;
  std::vector<QueryCollectionData> collections;
  std::vector<QueryOpData> operations;
  std::vector<QueryValData> values;
  std::string function;
  std::vector<Attr> args;
  Attr result;
};

struct QueryTiming {
  bool verification_cached = false;
  std::chrono::nanoseconds snapshot{};
  std::chrono::nanoseconds verification{};
  std::chrono::nanoseconds evaluation{};
  std::chrono::nanoseconds validation{};
};

struct StageDependencyData {
  QueryData inputs;
  std::vector<std::uint32_t> outputs;
  bool output_structure = false;
  bool output_packages = false;
};

struct Store {
  std::string name;
  std::uint64_t revision = 0;
  std::uint64_t structure_revision = 0;
  std::uint64_t verified_revision = 0;
  std::uint64_t verified_env = 0;
  std::uint64_t verified_epoch = 0;
  std::vector<std::string> uses;
  std::vector<Slot<FnData>> fns;
  std::vector<Slot<BlkData>> blks;
  std::vector<Slot<OpData>> ops;
  std::vector<Slot<ValData>> vals;
  std::vector<Diag> diags;
  std::unordered_map<std::string, std::vector<std::uint32_t>> symbols;
  mutable std::unordered_map<std::size_t, std::vector<QueryData>> queries;
  // Scratch marks for dependency-cone walks. Epochs avoid clearing an
  // operation-sized bitmap on every edit while keeping the public result
  // independent of previous queries.
  mutable std::vector<std::uint32_t> affected_marks;
  mutable std::uint32_t affected_epoch = 0;
};

enum class MetadataTarget : std::uint8_t { fn, op, val };

class Dom {
public:
  explicit Dom(const Store& store);
  bool has(std::uint32_t value, std::uint32_t use) const;

private:
  const Store* store_;
  std::vector<std::uint32_t> val_blks_;
  std::vector<std::uint32_t> val_fns_;
  std::vector<std::size_t> op_pos_;
};

bool same_type_pattern(const Ty& left,
                       const std::vector<std::string>& left_generics,
                       const Ty& right,
                       const std::vector<std::string>& right_generics);

template <class T>
inline bool live(const std::vector<Slot<T>>& slots, std::uint32_t id,
                 std::uint32_t generation) {
  return id < slots.size() && slots[id].live &&
         slots[id].generation == generation;
}

void add_diag(std::vector<Diag>& diags, std::string message, Loc loc = {});
int print_diags(std::FILE* file, const std::vector<Diag>& diags);
bool valid_binding(std::string_view text);
bool valid_qualified_name(std::string_view text);
bool sized_integer_type(std::string_view name) noexcept;
bool integer_type(std::string_view name) noexcept;
bool type_constructor(Fn fn);
bool literal_matches(const Attr& value, const Ty& type);
void touch(Store& store, std::uint32_t fn = none);
void touch_verified(Store& store, std::uint32_t fn = none);
void touch_functions(Store& store, std::span<const std::uint32_t> fns);
void touch_functions_verified(Store& store,
                              std::span<const std::uint32_t> fns);
void journal_metadata(Store& store, MetadataTarget target, std::uint32_t id);
void prepare_structural_mutation(Store& store);
void rebuild_uses(Store& store);
bool dominates(const Store& store, std::uint32_t value, std::uint32_t use);
std::vector<std::uint32_t> affected(const Store& store,
                                    std::span<const std::uint32_t> roots);
Fn resolve_overload(std::span<const Fn> candidates,
                    std::span<const Ty> arguments,
                    std::span<const Ty> explicit_arguments,
                    std::vector<Ty>* returns, bool* ambiguous,
                    std::span<const Val> context = {},
                    std::vector<Ty>* generics = nullptr,
                    std::span<const Ty> expected_returns = {});

}  // namespace joggle::detail

struct joggle::Mod::Impl {
  joggle::detail::Store store;
};

#endif
