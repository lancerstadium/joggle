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

enum class ValKind : std::uint8_t { generic, param, block_arg, result };
enum class Form : std::uint8_t {
  hidden,
  expr,
  let,
  var,
  assign,
  add_assign,
  index_assign
};

struct ValData {
  ValKind kind = ValKind::result;
  std::string name;
  Ty type;
  std::uint32_t def = none;
  std::size_t index = 0;
  std::vector<std::uint32_t> users;
};

struct OpData {
  Op::Kind kind = Op::Kind::call;
  std::uint32_t block = none;
  std::string callee;
  std::vector<std::uint32_t> args;
  std::vector<std::uint32_t> outs;
  std::vector<std::uint32_t> blocks;
  std::vector<std::string> iter_names;
  std::size_t carried_count = 0;
  Attr literal;
  Form form = Form::hidden;
  Loc loc;
};

struct BlkData {
  std::uint32_t fn = none;
  std::uint32_t parent_op = none;
  std::vector<std::uint32_t> args;
  std::vector<std::uint32_t> ops;
};

struct FnData {
  std::string name;
  std::vector<std::uint32_t> generic_vals;
  std::vector<std::uint32_t> params;
  std::vector<Ty> returns;
  std::vector<std::uint32_t> blocks;
  Attr::Dict meta;
  bool external = false;
  Loc loc;
};

struct Store {
  std::string name;
  std::vector<std::string> uses;
  std::vector<Slot<FnData>> fns;
  std::vector<Slot<BlkData>> blocks;
  std::vector<Slot<OpData>> ops;
  std::vector<Slot<ValData>> vals;
  std::vector<Diag> diags;
  std::unordered_map<std::string, std::vector<std::uint32_t>> symbols;
};

template <class T>
inline bool live(const std::vector<Slot<T>>& slots, std::uint32_t id,
                 std::uint32_t generation) {
  return id < slots.size() && slots[id].live &&
         slots[id].generation == generation;
}

void add_diag(std::vector<Diag>& diags, std::string message, Loc loc = {});
int print_diags(std::FILE* file, const std::vector<Diag>& diags);
void rebuild_uses(Store& store);
bool dominates(const Store& store, std::uint32_t value, std::uint32_t use);
Fn resolve_overload(std::span<const Fn> candidates,
                    std::span<const Ty> arguments,
                    std::span<const Ty> explicit_arguments,
                    std::vector<Ty>* returns, bool* ambiguous,
                    std::span<const Val> context = {},
                    std::vector<Ty>* generics = nullptr);

}  // namespace joggle::detail

struct joggle::Mod::Impl {
  joggle::detail::Store store;
};

#endif
