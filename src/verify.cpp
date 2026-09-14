#include "detail.h"
#include "language.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <exception>
#include <limits>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace joggle {

using detail::GenericInfo;
using detail::generic_info;
using detail::generic_names;
using detail::intrinsic_cast;
using detail::intrinsic_type;

namespace {

std::uint32_t arg_blk(const detail::Store& store, std::uint32_t value) {
  for (std::uint32_t id = 0; id < store.blks.size(); ++id) {
    const auto& args = store.blks[id].data.args;
    if (std::find(args.begin(), args.end(), value) != args.end())
      return id;
  }
  return detail::none;
}

std::uint32_t arg_fn(const detail::Store& store, std::uint32_t value) {
  for (std::uint32_t id = 0; id < store.fns.size(); ++id) {
    const auto& fn = store.fns[id].data;
    if (std::find(fn.generic_vals.begin(), fn.generic_vals.end(), value) !=
            fn.generic_vals.end() ||
        std::find(fn.params.begin(), fn.params.end(), value) != fn.params.end())
      return id;
  }
  return detail::none;
}

std::size_t op_index(const detail::Store& store, std::uint32_t blk,
                     std::uint32_t op) {
  const auto& ops = store.blks[blk].data.ops;
  const auto found = std::find(ops.begin(), ops.end(), op);
  return found == ops.end() ? ops.size()
                            : static_cast<std::size_t>(found - ops.begin());
}

bool blk_within(const detail::Store& store, std::uint32_t child,
                std::uint32_t ancestor) {
  while (child != detail::none) {
    if (child == ancestor)
      return true;
    const auto parent = store.blks[child].data.parent_op;
    child =
        parent == detail::none ? detail::none : store.ops[parent].data.blk;
  }
  return false;
}

}  // namespace

bool detail::dominates(const detail::Store& store, std::uint32_t value,
                       std::uint32_t use) {
  const detail::ValData& val = store.vals[value].data;
  const std::uint32_t use_blk = store.ops[use].data.blk;
  if (val.kind == detail::ValKind::generic ||
      val.kind == detail::ValKind::param)
    return arg_fn(store, value) == store.blks[use_blk].data.fn;
  if (val.kind == detail::ValKind::blk_arg) {
    const std::uint32_t def_blk = arg_blk(store, value);
    return def_blk != detail::none &&
           blk_within(store, use_blk, def_blk);
  }
  if (val.def == detail::none || val.def >= store.ops.size())
    return false;
  const std::uint32_t def_blk = store.ops[val.def].data.blk;
  if (def_blk == use_blk)
    return op_index(store, def_blk, val.def) <
           op_index(store, use_blk, use);

  std::uint32_t child = use_blk;
  while (child != detail::none) {
    const std::uint32_t parent = store.blks[child].data.parent_op;
    if (parent == detail::none)
      return false;
    const std::uint32_t parent_blk = store.ops[parent].data.blk;
    if (parent_blk == def_blk)
      return op_index(store, def_blk, val.def) <
             op_index(store, parent_blk, parent);
    child = parent_blk;
  }
  return false;
}

detail::Dom::Dom(const detail::Store& store)
    : store_(&store), val_blks_(store.vals.size(), detail::none),
      val_fns_(store.vals.size(), detail::none),
      op_pos_(store.ops.size(), std::numeric_limits<std::size_t>::max()) {
  for (std::uint32_t id = 0; id < store.fns.size(); ++id) {
    if (!store.fns[id].live)
      continue;
    for (const std::uint32_t value : store.fns[id].data.generic_vals)
      if (value < val_fns_.size())
        val_fns_[value] = id;
    for (const std::uint32_t value : store.fns[id].data.params)
      if (value < val_fns_.size())
        val_fns_[value] = id;
  }
  for (std::uint32_t id = 0; id < store.blks.size(); ++id) {
    if (!store.blks[id].live)
      continue;
    for (const std::uint32_t value : store.blks[id].data.args)
      if (value < val_blks_.size())
        val_blks_[value] = id;
    const auto& ops = store.blks[id].data.ops;
    for (std::size_t index = 0; index < ops.size(); ++index)
      if (ops[index] < op_pos_.size())
        op_pos_[ops[index]] = index;
  }
}

bool detail::Dom::has(std::uint32_t value, std::uint32_t use) const {
  const detail::Store& store = *store_;
  if (value >= store.vals.size() || !store.vals[value].live ||
      use >= store.ops.size() || !store.ops[use].live)
    return false;
  const detail::ValData& val = store.vals[value].data;
  const std::uint32_t use_blk = store.ops[use].data.blk;
  if (use_blk >= store.blks.size() || !store.blks[use_blk].live)
    return false;
  if (val.kind == detail::ValKind::generic ||
      val.kind == detail::ValKind::param)
    return val_fns_[value] == store.blks[use_blk].data.fn;
  if (val.kind == detail::ValKind::blk_arg) {
    const std::uint32_t def_blk = val_blks_[value];
    return def_blk != detail::none && blk_within(store, use_blk, def_blk);
  }
  if (val.def == detail::none || val.def >= store.ops.size() ||
      !store.ops[val.def].live)
    return false;
  const std::uint32_t def_blk = store.ops[val.def].data.blk;
  if (def_blk == use_blk)
    return op_pos_[val.def] < op_pos_[use];

  std::uint32_t child = use_blk;
  while (child != detail::none) {
    const std::uint32_t parent = store.blks[child].data.parent_op;
    if (parent == detail::none || parent >= store.ops.size() ||
        !store.ops[parent].live)
      return false;
    const std::uint32_t parent_blk = store.ops[parent].data.blk;
    if (parent_blk == def_blk)
      return op_pos_[val.def] < op_pos_[parent];
    if (parent_blk >= store.blks.size() || !store.blks[parent_blk].live)
      return false;
    child = parent_blk;
  }
  return false;
}

namespace {

using Bindings = std::map<std::string, Ty, std::less<>>;

bool generic(const std::vector<std::string>& names, std::string_view name) {
  return std::find(names.begin(), names.end(), name) != names.end();
}

bool index_integer(std::string_view left, std::string_view right) {
  return (left == "index" &&
          (right == "int" || detail::sized_integer_type(right))) ||
         (right == "index" &&
          (left == "int" || detail::sized_integer_type(left)));
}

bool merge_binding(Ty& bound, const Ty& actual) {
  if (bound == actual || actual.name() == "_")
    return true;
  if (bound.name() == "_") {
    bound = actual;
    return true;
  }
  if (bound.name() == "int" && detail::sized_integer_type(actual.name())) {
    bound = actual;
    return true;
  }
  return actual.name() == "int" && detail::sized_integer_type(bound.name());
}

Ty reduce_derived(const Ty& type, const std::vector<std::string>& generics,
                  const Bindings& bindings) {
  if (type.args().empty())
    return type;
  std::vector<Ty> args;
  args.reserve(type.args().size());
  for (const Ty& arg : type.args())
    args.push_back(reduce_derived(arg, generics, bindings));
  if (type.name() == "len" && args.size() == 1) {
    Ty operand = args.front();
    if (operand.args().empty() && generic(generics, operand.name())) {
      const auto found = bindings.find(std::string(operand.name()));
      if (found != bindings.end())
        operand = found->second;
    }
    if (operand.name() == "[]")
      return Ty(std::to_string(operand.args().size()));
  }
  return Ty(std::string(type.name()), args);
}

bool unify(const Ty& formal, const Ty& actual,
           const std::vector<std::string>& generics, Bindings& bindings) {
  const Ty reduced_formal = reduce_derived(formal, generics, bindings);
  if (reduced_formal != formal)
    return unify(reduced_formal, actual, generics, bindings);
  if (formal.empty() || actual.empty() || formal.name() == "_" ||
      formal.name() == "Attr" || actual.name() == "_" ||
      actual.name() == "Attr")
    return true;
  if (formal.args().empty() && generic(generics, formal.name())) {
    const auto found = bindings.find(std::string(formal.name()));
    if (found == bindings.end()) {
      bindings.emplace(std::string(formal.name()), actual);
      return true;
    }
    return merge_binding(found->second, actual);
  }
  if ((formal.name() == "int" &&
       detail::sized_integer_type(actual.name())) ||
      (actual.name() == "int" &&
       detail::sized_integer_type(formal.name())) ||
      index_integer(formal.name(), actual.name()))
    return true;
  if (formal.name() != actual.name())
    return false;
  if (formal.args().size() != actual.args().size())
    return false;
  for (std::size_t index = 0; index < formal.args().size(); ++index)
    if (!unify(formal.args()[index], actual.args()[index], generics, bindings))
      return false;
  return true;
}

std::size_t specificity(const Ty& type,
                        const std::vector<std::string>& generics) {
  if (type.name() == "_" || type.name() == "Attr" ||
      type.name() == "meta" ||
      (type.args().empty() && generic(generics, type.name())))
    return 0;
  std::size_t score = 1;
  for (const Ty& arg : type.args())
    score += specificity(arg, generics);
  return score;
}

Ty substitute(const Ty& type, const std::vector<std::string>& generics,
              const Bindings& bindings) {
  if (type.args().empty() && generic(generics, type.name())) {
    const auto found = bindings.find(std::string(type.name()));
    return found == bindings.end() ? Ty("_") : found->second;
  }
  if (type.args().empty())
    return type;
  std::vector<Ty> args;
  args.reserve(type.args().size());
  for (const Ty& arg : type.args())
    args.push_back(substitute(arg, generics, bindings));
  if (type.name() == "len" && args.size() == 1 &&
      args.front().name() == "[]")
    return Ty(std::to_string(args.front().args().size()));
  return Ty(std::string(type.name()), args);
}

bool integer_term(std::string_view text) {
  if (text.empty())
    return false;
  std::size_t index = text.front() == '-' || text.front() == '+' ? 1 : 0;
  if (index == text.size())
    return false;
  return std::all_of(
      text.begin() + static_cast<std::ptrdiff_t>(index), text.end(),
      [](char ch) { return std::isdigit(static_cast<unsigned char>(ch)); });
}

Ty term_kind(const Ty& term, std::span<const GenericInfo> context) {
  if (term.name() == "[]") {
    Ty element("_");
    if (!term.args().empty()) {
      element = term_kind(term.args().front(), context);
      for (std::size_t index = 1; index < term.args().size(); ++index)
        if (term_kind(term.args()[index], context) != element)
          element = Ty("_");
    }
    const std::array<Ty, 1> args{element};
    return Ty("list", args);
  }
  if (term.args().empty()) {
    if (term.name() == "_")
      return Ty("_");
    for (const GenericInfo& generic : context)
      if (generic.name == term.name())
        return generic.type;
    if (integer_term(term.name()))
      return Ty("int");
    if (term.name() == "true" || term.name() == "false")
      return Ty("bool");
    return Ty("Ty");
  }
  if (term.name() == "len" && term.args().size() == 1) {
    const Ty operand = term_kind(term.args().front(), context);
    return operand.name() == "list" ? Ty("int") : Ty("_");
  }
  return Ty("Ty");
}

bool accepts_kind(const Ty& expected, const Ty& actual) {
  if (expected.text() == "_" || expected.text() == "Attr" ||
      expected.text() == "meta" || actual.text() == "_")
    return true;
  if (expected.name() == "list" && expected.args().size() == 1 &&
      actual.name() == "list" && actual.args().size() == 1)
    return accepts_kind(expected.args().front(), actual.args().front());
  return expected == actual;
}

bool accepts_term(const Ty& expected, const Ty& term,
                  std::span<const GenericInfo> context = {}) {
  return accepts_kind(expected, term_kind(term, context));
}

bool unknown(std::span<const Ty> types) {
  return std::any_of(types.begin(), types.end(),
                     [](const Ty& type) { return type.name() == "_"; });
}

Fn select_overload(std::span<const Fn> candidates,
                   std::span<const Ty> arguments,
                   std::span<const Ty> explicit_arguments,
                   std::vector<Ty>* returns, bool* ambiguous,
                   std::span<const GenericInfo> context,
                   std::vector<Ty>* resolved_generics = nullptr,
                   std::span<const Ty> expected_returns = {}) {
  Fn best;
  std::vector<Ty> best_returns;
  std::vector<Ty> best_generics;
  std::vector<Ty> common_returns;
  std::array<std::size_t, 4> best_rank{};
  std::size_t matches_count = 0;
  bool returns_agree = true;
  bool tied = false;
  for (const Fn candidate : candidates) {
    const std::vector<Val> params = candidate.params();
    const std::vector<Ty> candidate_returns = candidate.returns();
    const std::vector<GenericInfo> info = generic_info(candidate);
    const std::vector<std::string> generics = generic_names(info);
    if (params.size() != arguments.size() ||
        (!expected_returns.empty() &&
         candidate_returns.size() != expected_returns.size()) ||
        (!explicit_arguments.empty() &&
         explicit_arguments.size() > generics.size()))
      continue;
    Bindings bindings;
    for (std::size_t index = 0; index < explicit_arguments.size(); ++index)
      bindings.emplace(generics[index], explicit_arguments[index]);
    bool matches = true;
    std::size_t general = 0;
    std::size_t score = 0;
    std::size_t exact = 0;
    for (std::size_t index = 0; index < params.size(); ++index) {
      const Ty formal = params[index].type();
      if ((arguments[index].name() == "_" ||
           arguments[index].name() == "Attr") &&
          (formal.name() == "_" || formal.name() == "Attr" ||
           formal.name() == "meta" ||
           (formal.args().empty() && generic(generics, formal.name()))))
        ++general;
      score += specificity(formal, generics);
      score += formal.name() == "Attr" && arguments[index].name() == "Attr";
      exact += formal == arguments[index];
      if (!unify(formal, arguments[index], generics, bindings)) {
        matches = false;
        break;
      }
    }
    for (std::size_t index = 0;
         matches && index < expected_returns.size(); ++index) {
      if (expected_returns[index].text() == "_")
        continue;
      const Ty& formal = candidate_returns[index];
      Bindings inferred = bindings;
      if (!unify(formal, expected_returns[index], generics, inferred)) {
        matches = false;
        break;
      }
      bindings = std::move(inferred);
    }
    for (std::size_t index = 0; matches && index < info.size(); ++index) {
      const auto bound = bindings.find(std::string(info[index].name));
      if (bound != bindings.end() &&
          !accepts_term(info[index].type, bound->second, context))
        matches = false;
    }
    if (!matches)
      continue;
    ++matches_count;
    const std::array rank{
        general, score, exact,
        1023 - std::min<std::size_t>(generics.size(), 1023)};
    std::vector<Ty> substituted;
    for (const Ty& type : candidate_returns)
      substituted.push_back(substitute(type, generics, bindings));
    if (matches_count == 1)
      common_returns = substituted;
    else if (common_returns != substituted)
      returns_agree = false;
    std::vector<Ty> bound_generics;
    bound_generics.reserve(generics.size());
    for (const std::string& generic : generics) {
      const auto bound = bindings.find(generic);
      bound_generics.push_back(bound == bindings.end() ? Ty("_")
                                                       : bound->second);
    }
    if (!best || rank > best_rank) {
      best = candidate;
      best_returns = std::move(substituted);
      best_generics = std::move(bound_generics);
      best_rank = rank;
      tied = false;
    } else if (rank == best_rank)
      tied = true;
  }
  if (ambiguous)
    *ambiguous = tied;
  if (!best || tied) {
    if (returns && matches_count != 0 && returns_agree)
      *returns = std::move(common_returns);
    return {};
  }
  if (returns)
    *returns = std::move(best_returns);
  if (resolved_generics)
    *resolved_generics = std::move(best_generics);
  return best;
}

bool depends_on(const Ty& type, std::span<const GenericInfo> context) {
  if (type.args().empty()) {
    return std::any_of(context.begin(), context.end(), [&](const auto& item) {
      return item.name == type.name();
    });
  }
  return std::any_of(type.args().begin(), type.args().end(),
                     [&](const Ty& arg) { return depends_on(arg, context); });
}

Ty mask_dependent(const Ty& type, std::span<const GenericInfo> context) {
  if (type.args().empty())
    return depends_on(type, context) ? Ty("_") : type;
  std::vector<Ty> args;
  args.reserve(type.args().size());
  for (const Ty& arg : type.args())
    args.push_back(mask_dependent(arg, context));
  return Ty(std::string(type.name()), args);
}

bool can_defer(std::span<const Fn> candidates,
               std::span<const Ty> arguments,
               std::span<const Ty> explicit_arguments,
               std::span<const Ty> expected_returns,
               std::span<const GenericInfo> context) {
  const auto dependent = [&](std::span<const Ty> types) {
    return std::any_of(types.begin(), types.end(),
                       [&](const Ty& type) { return depends_on(type, context); });
  };
  if (context.empty() ||
      (!dependent(arguments) && !dependent(explicit_arguments) &&
       !dependent(expected_returns)))
    return false;

  std::vector<Ty> masked_arguments;
  std::vector<Ty> masked_explicit;
  std::vector<Ty> masked_returns;
  for (const Ty& type : arguments)
    masked_arguments.push_back(mask_dependent(type, context));
  for (const Ty& type : explicit_arguments)
    masked_explicit.push_back(mask_dependent(type, context));
  for (const Ty& type : expected_returns)
    masked_returns.push_back(mask_dependent(type, context));

  for (const Fn candidate : candidates) {
    const std::array one{candidate};
    if (select_overload(one, masked_arguments, masked_explicit, nullptr,
                        nullptr, context, nullptr, masked_returns))
      return true;
  }
  return false;
}

std::vector<Fn> declarations(const Mod& mod, const Env& env,
                             std::string_view callee,
                             std::vector<Ty>& explicit_args,
                             std::string& symbol) {
  if (callee == "base.list")
    return {};
  const Ty applied{std::string(callee)};
  symbol = callee;
  if (!applied.args().empty()) {
    symbol = applied.name();
    explicit_args = applied.args();
  }
  return env.resolve_fns(mod, symbol);
}

void infer_list(detail::Store& store, detail::OpData& op) {
  if (op.args.empty() && !op.outs.empty()) {
    detail::ValData& out = store.vals[op.outs.front()].data;
    if (out.type_annotation && out.type.name() == "list" &&
        out.type.args().size() == 1)
      return;
  }
  Ty element("_");
  if (!op.args.empty()) {
    element = store.vals[op.args.front()].data.type;
    for (std::size_t index = 1; index < op.args.size(); ++index)
      if (store.vals[op.args[index]].data.type != element)
        element = Ty("_");
  }
  if (!op.outs.empty()) {
    const std::array<Ty, 1> args{element};
    store.vals[op.outs.front()].data.type = Ty("list", args);
  }
}

Ty return_context(const detail::Store& store, std::uint32_t value) {
  for (const std::uint32_t user : store.vals[value].data.users) {
    if (user >= store.ops.size() || !store.ops[user].live)
      continue;
    const detail::OpData& op = store.ops[user].data;
    if (op.kind != Op::Kind::ret || op.blk >= store.blks.size())
      continue;
    const std::uint32_t owner = store.blks[op.blk].data.fn;
    if (owner >= store.fns.size() || !store.fns[owner].live)
      continue;
    const std::vector<Ty>& returns = store.fns[owner].data.returns;
    for (std::size_t index = 0;
         index < op.args.size() && index < returns.size(); ++index)
      if (op.args[index] == value)
        return returns[index];
  }
  return Ty("_");
}

bool statement_placeholder(const detail::Store& store,
                           const detail::OpData& op) {
  if (op.form != Op::Form::expr || op.outs.size() != 1)
    return false;
  const std::uint32_t output = op.outs.front();
  if (output >= store.vals.size() || !store.vals[output].live)
    return false;
  const detail::ValData& value = store.vals[output].data;
  return value.name.empty() && !value.type_annotation && value.users.empty();
}

void infer_call(detail::Store& store, const Mod& mod, const Env& env,
                detail::OpData& op, bool diagnose) {
  if (intrinsic_cast(op.callee) && op.args.size() == 1 && op.outs.size() == 1) {
    store.vals[op.outs.front()].data.type = Ty(op.callee);
    return;
  }
  if (op.callee == "base.copy" && op.args.size() == 1 && op.outs.size() == 1) {
    store.vals[op.outs.front()].data.type =
        store.vals[op.args.front()].data.type;
    return;
  }
  if (op.callee == "base.list") {
    infer_list(store, op);
    return;
  }
  if (op.callee == "operator []" && op.args.size() == 2 && !op.outs.empty()) {
    const Ty& container = store.vals[op.args.front()].data.type;
    if (container.name() == "list" && container.args().size() == 1) {
      store.vals[op.outs.front()].data.type = container.args().front();
      return;
    }
  }

  std::vector<Ty> explicit_args;
  std::string symbol;
  const std::vector<Fn> candidates =
      declarations(mod, env, op.callee, explicit_args, symbol);
  if (candidates.empty()) {
    if (diagnose) {
      const std::vector<Fn> hidden = env.find_fns(symbol);
      if (!hidden.empty())
        detail::add_diag(store.diags,
                         "call to '" + std::string(op.callee) +
                             "' requires 'use " +
                             std::string(hidden.front().module()) + "'",
                         op.loc);
      else if (env.declared(symbol))
        detail::add_diag(store.diags,
                         "call to '" + std::string(op.callee) +
                             "' names a local function in another module",
                         op.loc);
    }
    return;
  }
  std::vector<Ty> arguments;
  arguments.reserve(op.args.size());
  for (const std::uint32_t argument : op.args)
    arguments.push_back(store.vals[argument].data.type);
  std::vector<Ty> returns;
  std::vector<Ty> expected_returns;
  const bool statement = statement_placeholder(store, op);
  if (!statement) {
    expected_returns.reserve(op.outs.size());
    for (const std::uint32_t output : op.outs) {
      const detail::ValData& value = store.vals[output].data;
      expected_returns.push_back(value.type_annotation
                                     ? value.type
                                     : return_context(store, output));
    }
  }
  bool ambiguous = false;
  const std::uint32_t owner = store.blks[op.blk].data.fn;
  const std::vector<GenericInfo> context =
      owner == detail::none ? std::vector<GenericInfo>{}
                            : generic_info(store, store.fns[owner].data);
  const Fn fn = select_overload(candidates, arguments, explicit_args, &returns,
                                &ambiguous, context, nullptr,
                                expected_returns);
  if (!fn) {
    const bool open_inference =
        !diagnose && (unknown(arguments) || unknown(explicit_args));
    if (open_inference ||
        can_defer(candidates, arguments, explicit_args, expected_returns,
                  context)) {
      for (std::size_t index = 0;
           index < op.outs.size() && index < returns.size(); ++index) {
        detail::ValData& result = store.vals[op.outs[index]].data;
        if (!result.type_annotation || result.type.text() == "_")
          result.type = returns[index];
      }
      for (std::size_t index = 0;
           index < op.outs.size() && index < expected_returns.size(); ++index) {
        detail::ValData& result = store.vals[op.outs[index]].data;
        if ((!result.type_annotation || result.type.text() == "_") &&
            expected_returns[index].text() != "_")
          result.type = expected_returns[index];
      }
      return;
    }
    if (diagnose) {
      std::string message;
      if (ambiguous)
        message = "call to '" + std::string(op.callee) + "' is ambiguous";
      else if (candidates.size() == 1) {
        const Fn candidate = candidates.front();
        const std::vector<Val> params = candidate.params();
        const std::vector<Ty> candidate_returns = candidate.returns();
        const std::vector<std::string> generics =
            generic_names(generic_info(candidate));
        if (params.size() != arguments.size())
          message = "call to '" + std::string(op.callee) + "' expects " +
                    std::to_string(params.size()) + " arguments, got " +
                    std::to_string(arguments.size());
        else if (!expected_returns.empty() &&
                 candidate_returns.size() != expected_returns.size())
          message = "call to '" + std::string(op.callee) + "' expects " +
                    std::to_string(candidate_returns.size()) +
                    " results, got " +
                    std::to_string(expected_returns.size());
        else if (explicit_args.size() > generics.size())
          message =
              "call to '" + std::string(op.callee) + "' accepts at most " +
              std::to_string(generics.size()) +
              " explicit generic arguments, got " +
              std::to_string(explicit_args.size());
        else {
          Bindings bindings;
          for (std::size_t index = 0; index < explicit_args.size(); ++index)
            bindings.emplace(generics[index], explicit_args[index]);
          for (std::size_t index = 0; index < params.size(); ++index) {
            const Ty formal = params[index].type();
            if (unify(formal, arguments[index], generics, bindings))
              continue;
            message =
                "argument " + std::to_string(index + 1) + " of '" +
                std::string(op.callee) + "' has type '" +
                std::string(arguments[index].text()) + "', expected '" +
                std::string(substitute(formal, generics, bindings).text()) +
                "'";
            break;
          }
          for (std::size_t index = 0;
               message.empty() && index < expected_returns.size(); ++index) {
            if (expected_returns[index].text() == "_")
              continue;
            Bindings inferred = bindings;
            if (unify(candidate_returns[index], expected_returns[index],
                      generics, inferred)) {
              bindings = std::move(inferred);
              continue;
            }
            const Ty actual = substitute(candidate_returns[index], generics,
                                         bindings);
            message = "result " + std::to_string(index + 1) + " of '" +
                      std::string(op.callee) + "' requires type '" +
                      std::string(expected_returns[index].text()) +
                      "', but the overload returns '" +
                      std::string(actual.text()) + "'";
          }
          const std::vector<GenericInfo> info = generic_info(candidate);
          for (std::size_t index = 0; message.empty() && index < info.size();
               ++index) {
            const auto bound = bindings.find(std::string(info[index].name));
            if (bound == bindings.end() ||
                accepts_term(info[index].type, bound->second, context))
              continue;
            message = "generic argument '" + std::string(info[index].name) +
                      "' of '" + std::string(op.callee) + "' has type '" +
                      std::string(term_kind(bound->second, context).text()) +
                      "', expected '" + std::string(info[index].type.text()) +
                      "'";
          }
        }
      }
      if (message.empty()) {
        message = "no overload of '" + std::string(op.callee) + "' accepts (";
        for (std::size_t index = 0; index < arguments.size(); ++index) {
          if (index)
            message += ", ";
          message += std::string(arguments[index].text());
        }
        message += ')';
      }
      detail::add_diag(store.diags, std::move(message), op.loc);
    }
    return;
  }
  if (returns.size() != op.outs.size()) {
    if (statement && returns.empty())
      return;
    if (diagnose)
      detail::add_diag(store.diags,
                       "result count of '" + std::string(op.callee) +
                           "' does not match its declaration",
                       op.loc);
    return;
  }
  for (std::size_t index = 0; index < returns.size(); ++index) {
    detail::ValData& result = store.vals[op.outs[index]].data;
    Ty& type = result.type;
    if (!result.type_annotation || type.empty() || type.text() == "_")
      type = returns[index];
    else if (!returns[index].empty() && returns[index].text() != "_" &&
             type != returns[index] && diagnose)
      detail::add_diag(store.diags,
                       "result " + std::to_string(index + 1) + " of '" +
                           std::string(op.callee) + "' has declared type '" +
                           std::string(type.text()) + "', expected '" +
                           std::string(returns[index].text()) + "'",
                       op.loc);
  }
}

bool normalize_void_statements(detail::Store& store, const Mod& mod,
                               const Env& env) {
  bool changed = false;
  for (auto& slot : store.ops) {
    detail::OpData& op = slot.data;
    if (!slot.live || op.kind != Op::Kind::call ||
        !statement_placeholder(store, op))
      continue;
    std::vector<Ty> explicit_args;
    std::string symbol;
    const std::vector<Fn> candidates =
        declarations(mod, env, op.callee, explicit_args, symbol);
    if (candidates.empty())
      continue;
    std::vector<Ty> arguments;
    arguments.reserve(op.args.size());
    for (const std::uint32_t argument : op.args)
      arguments.push_back(store.vals[argument].data.type);
    const std::uint32_t owner = store.blks[op.blk].data.fn;
    const std::vector<GenericInfo> context =
        owner == detail::none ? std::vector<GenericInfo>{}
                              : generic_info(store, store.fns[owner].data);
    std::vector<Ty> returns;
    if (!select_overload(candidates, arguments, explicit_args, &returns,
                         nullptr, context) ||
        !returns.empty())
      continue;
    const std::uint32_t output = op.outs.front();
    op.outs.clear();
    store.vals[output].live = false;
    ++store.vals[output].generation;
    changed = true;
  }
  if (changed)
    detail::rebuild_uses(store);
  return changed;
}

void infer_regions(detail::Store& store, const detail::OpData& op) {
  if (op.kind == Op::Kind::loop && op.blks.size() == 1) {
    auto& args = store.blks[op.blks.front()].data.args;
    for (std::size_t index = 0; index < op.iter_names.size(); ++index) {
      const Ty& source = store.vals[op.args[index]].data.type;
      store.vals[args[index]].data.type =
          source.name() == "list" && source.args().size() == 1
              ? source.args().front()
          : source.name() == "range" ? Ty("index")
                                     : Ty("_");
    }
    for (std::size_t index = 0; index < op.carried_count; ++index) {
      const Ty type =
          store.vals[op.args[op.iter_names.size() + index]].data.type;
      store.vals[args[op.iter_names.size() + index]].data.type = type;
      store.vals[op.outs[index]].data.type = type;
    }
  } else if (op.kind == Op::Kind::branch) {
    for (const std::uint32_t blk : op.blks)
      for (std::size_t index = 0; index < op.carried_count; ++index)
        store.vals[store.blks[blk].data.args[index]].data.type =
            store.vals[op.args[index + 1]].data.type;
    for (std::size_t index = 0; index < op.carried_count; ++index)
      store.vals[op.outs[index]].data.type =
          store.vals[op.args[index + 1]].data.type;
  }
}

struct TypeLookup {
  Fn constructor;
  bool seen = false;
  bool ambiguous = false;
  std::vector<std::size_t> arities;
  std::optional<std::size_t> mismatch;
  Ty expected;
  Ty actual;
};

TypeLookup type_declaration(const Mod& mod, const Env& env, const Ty& type,
                            std::span<const GenericInfo> context) {
  const std::vector<Fn> candidates = env.resolve_fns(mod, type.name());
  TypeLookup result;
  result.seen = !candidates.empty();
  for (const Fn candidate : candidates) {
    if (!detail::type_constructor(candidate))
      continue;
    result.arities.push_back(candidate.generics().size());
    if (candidate.generics().size() != type.args().size())
      continue;
    const std::vector<Val> generics = candidate.generics();
    bool compatible = true;
    for (std::size_t index = 0; index < generics.size(); ++index) {
      const Ty expected = generics[index].type();
      if (accepts_term(expected, type.args()[index], context))
        continue;
      if (!result.mismatch) {
        result.mismatch = index;
        result.expected = expected;
        result.actual = term_kind(type.args()[index], context);
      }
      compatible = false;
      break;
    }
    if (!compatible)
      continue;
    if (result.constructor) {
      result.constructor = {};
      result.ambiguous = true;
      return result;
    }
    result.constructor = candidate;
  }
  return result;
}

bool verify_type(detail::Store& store, const Mod& mod, const Env& env,
                 const Ty& type, const std::vector<std::string>& generics,
                 std::span<const GenericInfo> context, Loc loc) {
  if (!type.valid()) {
    detail::add_diag(store.diags,
                     "malformed type '" + std::string(type.text()) + "'",
                     std::move(loc));
    return false;
  }
  if (type.name() == "list") {
    if (type.args().size() != 1) {
      detail::add_diag(store.diags, "type 'list' expects 1 argument",
                       std::move(loc));
      return false;
    }
    return verify_type(store, mod, env, type.args().front(), generics, context,
                       std::move(loc));
  }
  if (type.args().empty() && intrinsic_type(type.name()))
    return true;
  if (type.args().empty() && generic(generics, type.name())) {
    const Ty kind = term_kind(type, context);
    if (accepts_kind(Ty("Ty"), kind))
      return true;
    detail::add_diag(store.diags,
                     "type parameter '" + std::string(type.name()) +
                         "' has type '" + std::string(kind.text()) +
                         "', expected 'Ty'",
                     std::move(loc));
    return false;
  }
  if (intrinsic_type(type.name())) {
    detail::add_diag(store.diags,
                     "type '" + std::string(type.name()) +
                         "' does not accept type arguments",
                     std::move(loc));
    return false;
  }
  if (generic(generics, type.name())) {
    detail::add_diag(store.diags,
                     "type parameter '" + std::string(type.name()) +
                         "' cannot be used as a type constructor",
                     std::move(loc));
    return false;
  }
  const Ty kind = term_kind(type, context);
  if (!accepts_kind(Ty("Ty"), kind)) {
    detail::add_diag(store.diags,
                     "type expression '" + std::string(type.text()) +
                         "' has type '" + std::string(kind.text()) +
                         "', expected 'Ty'",
                     std::move(loc));
    return false;
  }

  const TypeLookup lookup = type_declaration(mod, env, type, context);
  if (!lookup.constructor) {
    if (lookup.ambiguous) {
      detail::add_diag(store.diags,
                       "type constructor '" + std::string(type.name()) +
                           "' is ambiguous",
                       std::move(loc));
      return false;
    }
    if (lookup.seen) {
      if (lookup.mismatch)
        detail::add_diag(
            store.diags,
            "type argument " + std::to_string(*lookup.mismatch + 1) + " of '" +
                std::string(type.name()) + "' has type '" +
                std::string(lookup.actual.text()) + "', expected '" +
                std::string(lookup.expected.text()) + "'",
            std::move(loc));
      else if (lookup.arities.size() == 1)
        detail::add_diag(store.diags,
                         "type '" + std::string(type.name()) + "' expects " +
                             std::to_string(lookup.arities.front()) +
                             (lookup.arities.front() == 1
                                  ? " type argument, got "
                                  : " type arguments, got ") +
                             std::to_string(type.args().size()),
                         std::move(loc));
      else
        detail::add_diag(store.diags,
                         lookup.arities.empty()
                             ? "function family '" + std::string(type.name()) +
                                   "' has no type-constructor overload"
                             : "no type-constructor overload of '" +
                                   std::string(type.name()) + "' accepts " +
                                   std::to_string(type.args().size()) +
                                   " arguments",
                         std::move(loc));
      return false;
    }
    std::vector<Fn> hidden;
    if (type.name().find('.') != std::string_view::npos) {
      hidden = env.find_fns(type.name());
    } else {
      for (const std::string& module : env.modules()) {
        for (const Fn candidate : env.fns(module)) {
          if (candidate.name() == type.name() &&
              detail::type_constructor(candidate)) {
            hidden.push_back(candidate);
            break;
          }
        }
        if (!hidden.empty())
          break;
      }
    }
    if (!hidden.empty()) {
      detail::add_diag(
          store.diags,
          "type '" + std::string(type.name()) + "' requires 'use " +
              std::string(hidden.front().module()) + "'",
          std::move(loc));
      return false;
    }
    if (type.name().find('.') != std::string_view::npos &&
        env.declared(type.name())) {
      detail::add_diag(store.diags,
                       "type '" + std::string(type.name()) +
                           "' names a local function in another module",
                       std::move(loc));
      return false;
    }
    return true;
  }
  const std::vector<Val> constructor_generics = lookup.constructor.generics();
  const auto verify_argument = [&](auto&& self, const Ty& expected,
                                   const Ty& argument) -> bool {
    if (expected.name() == "Ty")
      return verify_type(store, mod, env, argument, generics, context, loc);
    if (expected.name() != "list" || expected.args().size() != 1 ||
        argument.name() != "[]")
      return true;
    for (const Ty& item : argument.args())
      if (!self(self, expected.args().front(), item))
        return false;
    return true;
  };
  for (std::size_t index = 0; index < constructor_generics.size(); ++index)
    if (!verify_argument(verify_argument, constructor_generics[index].type(),
                         type.args()[index]))
      return false;
  return true;
}

void verify_bindings(detail::Store& store, std::uint32_t fn_id) {
  if (fn_id >= store.fns.size() || !store.fns[fn_id].live)
    return;
  const detail::FnData& fn = store.fns[fn_id].data;
  std::set<std::string, std::less<>> visible;
  std::set<std::string, std::less<>> declared;
  const auto declare = [&](std::uint32_t value, const Loc& loc) {
    if (value >= store.vals.size() || !store.vals[value].live)
      return;
    const std::string& name = store.vals[value].data.name;
    if (name.empty())
      return;
    if (!detail::valid_binding(name)) {
      detail::add_diag(store.diags, "invalid binding name '" + name + "'",
                       loc);
      return;
    }
    if (!declared.insert(name).second)
      detail::add_diag(store.diags,
                       "binding '" + name +
                           "' is already declared in this scope",
                       loc);
    visible.insert(name);
  };
  for (const std::uint32_t generic : fn.generic_vals)
    declare(generic, fn.loc);
  for (const std::uint32_t param : fn.params)
    declare(param, fn.loc);

  std::unordered_set<std::uint32_t> visited;
  const auto block = [&](const auto& self, std::uint32_t blk,
                         std::set<std::string, std::less<>> scope,
                         std::set<std::string, std::less<>> local) -> void {
    if (blk >= store.blks.size() || !store.blks[blk].live ||
        !visited.insert(blk).second)
      return;
    for (const std::uint32_t op_id : store.blks[blk].data.ops) {
      if (op_id >= store.ops.size() || !store.ops[op_id].live)
        continue;
      const detail::OpData& op = store.ops[op_id].data;
      for (std::size_t child_index = 0; child_index < op.blks.size();
           ++child_index) {
        const std::uint32_t child = op.blks[child_index];
        std::set<std::string, std::less<>> child_scope = scope;
        std::set<std::string, std::less<>> child_local;
        if (op.kind == Op::Kind::loop) {
          for (std::size_t index = 0; index < op.iter_names.size(); ++index) {
            const std::string& name = op.iter_names[index];
            if (!detail::valid_binding(name))
              detail::add_diag(store.diags,
                               "invalid loop variable '" + name + "'",
                               op.loc);
            if (!child_local.insert(name).second)
              detail::add_diag(store.diags,
                               "duplicate loop variable '" + name + "'",
                               op.loc);
            if (scope.contains(name))
              detail::add_diag(store.diags,
                               "loop variable '" + name +
                                   "' is already visible",
                               op.loc);
            child_scope.insert(name);
            if (child >= store.blks.size() || !store.blks[child].live ||
                index >= store.blks[child].data.args.size())
              continue;
            const std::uint32_t argument =
                store.blks[child].data.args[index];
            if (argument < store.vals.size() && store.vals[argument].live &&
                store.vals[argument].data.name != name)
              detail::add_diag(store.diags,
                               "loop variable and block argument names differ",
                               op.loc);
          }
        }
        self(self, child, std::move(child_scope), std::move(child_local));
      }
      if (op.form != Op::Form::let && op.form != Op::Form::var)
        continue;
      for (const std::uint32_t output : op.outs) {
        if (output >= store.vals.size() || !store.vals[output].live)
          continue;
        const std::string& name = store.vals[output].data.name;
        if (name.empty())
          continue;
        if (!detail::valid_binding(name))
          detail::add_diag(store.diags,
                           "invalid binding name '" + name + "'", op.loc);
        if (!local.insert(name).second)
          detail::add_diag(store.diags,
                           "binding '" + name +
                               "' is already declared in this scope",
                           op.loc);
        scope.insert(name);
      }
    }
  };
  if (!fn.blks.empty())
    block(block, fn.blks.front(), std::move(visible), std::move(declared));
}

}  // namespace

Fn detail::resolve_overload(std::span<const Fn> candidates,
                            std::span<const Ty> arguments,
                            std::span<const Ty> explicit_arguments,
                            std::vector<Ty>* returns, bool* ambiguous,
                            std::span<const Val> context,
                            std::vector<Ty>* generics,
                            std::span<const Ty> expected_returns) {
  std::vector<GenericInfo> info;
  info.reserve(context.size());
  for (const Val generic : context)
    info.push_back({generic.name(), generic.type()});
  return select_overload(candidates, arguments, explicit_arguments, returns,
                         ambiguous, info, generics, expected_returns);
}

bool Mod::verify(const Env& env) {
  detail::Store& store = impl_->store;
  store.diags.clear();
  detail::rebuild_uses(store);
  std::vector<Ty> original_types;
  original_types.reserve(store.vals.size());
  for (const auto& value : store.vals)
    original_types.push_back(value.data.type);
  if (store.name.empty())
    detail::add_diag(store.diags, "module has no name");
  std::set<std::string, std::less<>> dependencies;
  for (const std::string& dependency : store.uses) {
    if (dependency == store.name)
      detail::add_diag(store.diags,
                       "module cannot depend on itself: " + dependency);
    if (!dependencies.insert(dependency).second)
      detail::add_diag(store.diags,
                       "duplicate module dependency: " + dependency);
    if (!env.loaded(dependency))
      detail::add_diag(store.diags,
                       "dependency module is not loaded: " + dependency);
    else if (dependency != store.name &&
             env.reaches(dependency, store.name))
      detail::add_diag(store.diags,
                       "module dependency cycle through: " + dependency);
  }
  for (std::uint32_t fn = 0; fn < store.fns.size(); ++fn)
    verify_bindings(store, fn);
  for (const auto& fn_slot : store.fns) {
    if (!fn_slot.live)
      continue;
    const detail::FnData& fn = fn_slot.data;
    const std::vector<GenericInfo> context = generic_info(store, fn);
    const std::vector<std::string> generics = generic_names(context);
    for (const std::uint32_t generic : fn.generic_vals)
      verify_type(store, *this, env, store.vals[generic].data.type, generics,
                  context, fn.loc);
    for (const std::uint32_t param : fn.params)
      verify_type(store, *this, env, store.vals[param].data.type, generics,
                  context, fn.loc);
    for (const Ty& type : fn.returns)
      verify_type(store, *this, env, type, generics, context, fn.loc);
  }
  for (std::size_t iteration = 0; iteration <= store.ops.size(); ++iteration) {
    std::vector<Ty> before;
    before.reserve(store.vals.size());
    for (const auto& value : store.vals)
      before.push_back(value.data.type);
    for (auto& op : store.ops) {
      if (!op.live)
        continue;
      if (op.data.kind == Op::Kind::call)
        infer_call(store, *this, env, op.data, false);
      infer_regions(store, op.data);
    }
    bool stable = before.size() == store.vals.size();
    for (std::size_t index = 0; stable && index < before.size(); ++index)
      stable = before[index] == store.vals[index].data.type;
    if (stable)
      break;
  }
  for (auto& op : store.ops)
    if (op.live && op.data.kind == Op::Kind::call)
      infer_call(store, *this, env, op.data, true);
  for (const auto& fn_slot : store.fns) {
    if (!fn_slot.live || fn_slot.data.external)
      continue;
    const detail::FnData& fn = fn_slot.data;
    if (fn.blks.empty()) {
      detail::add_diag(store.diags, "function '" + fn.name + "' has no body",
                       fn.loc);
      continue;
    }
    const auto& body_ops = store.blks[fn.blks.front()].data.ops;
    if (body_ops.empty() ||
        store.ops[body_ops.back()].data.kind != Op::Kind::ret) {
      detail::add_diag(store.diags,
                       "function '" + fn.name + "' must end with return",
                       fn.loc);
      continue;
    }
    const std::vector<std::string> generics =
        generic_names(generic_info(store, fn));
    for (const std::uint32_t blk : fn.blks) {
      if (blk >= store.blks.size() || !store.blks[blk].live)
        continue;
      for (const std::uint32_t op : store.blks[blk].data.ops) {
        if (op >= store.ops.size() || !store.ops[op].live ||
            store.ops[op].data.kind != Op::Kind::ret)
          continue;
        const detail::OpData& ret = store.ops[op].data;
        if (ret.args.size() != fn.returns.size())
          detail::add_diag(store.diags,
                           "return arity does not match function signature",
                           ret.loc);
        Bindings bindings;
        for (std::size_t index = 0;
             index < std::min(ret.args.size(), fn.returns.size()); ++index) {
          const Ty& actual = store.vals[ret.args[index]].data.type;
          const Ty& expected = fn.returns[index];
          if (!actual.empty() && actual.text() != "_" && !expected.empty() &&
              expected.text() != "_" && actual != expected &&
              !unify(expected, actual, generics, bindings))
            detail::add_diag(store.diags,
                             "return type '" + std::string(actual.text()) +
                                 "' does not match '" +
                                 std::string(expected.text()) + "'",
                             ret.loc);
        }
      }
    }
  }
  for (const auto& blk_slot : store.blks) {
    if (!blk_slot.live || blk_slot.data.parent_op == detail::none)
      continue;
    const auto& ops = blk_slot.data.ops;
    if (ops.empty() || store.ops[ops.back()].data.kind != Op::Kind::yield) {
      detail::add_diag(store.diags, "nested Blk must end with yield");
      continue;
    }
    const auto& parent = store.ops[blk_slot.data.parent_op].data;
    const auto& yield = store.ops[ops.back()].data;
    if (yield.args.size() != parent.carried_count) {
      detail::add_diag(store.diags,
                       "yield arity does not match carried values");
      continue;
    }
    for (std::size_t index = 0; index < yield.args.size(); ++index) {
      if (index >= parent.outs.size() || yield.args[index] >= store.vals.size() ||
          parent.outs[index] >= store.vals.size() ||
          !store.vals[yield.args[index]].live ||
          !store.vals[parent.outs[index]].live)
        continue;
      const Ty& actual = store.vals[yield.args[index]].data.type;
      const Ty& expected = store.vals[parent.outs[index]].data.type;
      Bindings bindings;
      if (!actual.empty() && actual.text() != "_" && !expected.empty() &&
          expected.text() != "_" &&
          !unify(expected, actual, {}, bindings))
        detail::add_diag(store.diags,
                         "yield type '" + std::string(actual.text()) +
                             "' does not match carried type '" +
                             std::string(expected.text()) + "'",
                         yield.loc);
    }
  }
  const detail::Dom dom(store);
  for (std::uint32_t op_id = 0; op_id < store.ops.size(); ++op_id) {
    const auto& op_slot = store.ops[op_id];
    if (!op_slot.live)
      continue;
    const detail::OpData& op = op_slot.data;
    if (op.blk >= store.blks.size() || !store.blks[op.blk].live)
      detail::add_diag(store.diags, "operation has an invalid parent Blk",
                       op.loc);
    if (op.kind == Op::Kind::call && op.callee.empty())
      detail::add_diag(store.diags, "call has no callee", op.loc);
    const bool binds = op.form == Op::Form::let ||
                       op.form == Op::Form::var ||
                       op.form == Op::Form::assign ||
                       op.form == Op::Form::compound ||
                       op.form == Op::Form::index_assign;
    if (binds &&
        (op.outs.empty() ||
         std::any_of(op.outs.begin(), op.outs.end(), [&](std::uint32_t id) {
           return id >= store.vals.size() ||
                  store.vals[id].data.name.empty();
         })))
      detail::add_diag(store.diags,
                       "visible operation requires named results", op.loc);
    if ((op.form == Op::Form::assign ||
         op.form == Op::Form::compound ||
         op.form == Op::Form::index_assign) &&
        op.outs.size() != 1)
      detail::add_diag(store.diags,
                       "assignment must produce exactly one result", op.loc);
    if (op.kind == Op::Kind::call && op.form == Op::Form::assign &&
        op.args.empty())
      detail::add_diag(store.diags, "assignment call has no value", op.loc);
    if (op.kind == Op::Kind::call && op.form == Op::Form::compound &&
        (op.args.size() < 2 ||
         !std::string_view(op.callee).starts_with("operator ")))
      detail::add_diag(store.diags,
                       "compound assignment call is inconsistent", op.loc);
    if (op.form == Op::Form::index_assign &&
        (op.kind != Op::Kind::call || op.args.size() < 3))
      detail::add_diag(store.diags,
                       "indexed assignment is inconsistent", op.loc);
    if ((op.kind == Op::Kind::call || op.kind == Op::Kind::constant) &&
        op.outs.size() > 1) {
      if (op.form == Op::Form::hidden)
        detail::add_diag(store.diags,
                         "multi-result operation must have named bindings",
                         op.loc);
      else if (std::any_of(op.outs.begin(), op.outs.end(),
                           [&](std::uint32_t id) {
                             return id >= store.vals.size() ||
                                    store.vals[id].data.name.empty();
                           }))
        detail::add_diag(store.diags,
                         "multi-result operation has an unnamed result",
                         op.loc);
    }
    const auto blk_args = [&](std::size_t index, std::size_t count) {
      if (index >= op.blks.size() || op.blks[index] >= store.blks.size())
        return false;
      const auto& child = store.blks[op.blks[index]];
      return child.live && child.data.parent_op == op_id &&
             child.data.args.size() == count;
    };
    if (op.kind == Op::Kind::loop &&
        (op.blks.size() != 1 ||
         op.args.size() != op.iter_names.size() + op.carried_count ||
         op.outs.size() != op.carried_count ||
         !blk_args(0, op.iter_names.size() + op.carried_count)))
      detail::add_diag(store.diags, "loop structure is inconsistent", op.loc);
    if (op.kind == Op::Kind::branch &&
        (op.blks.size() != 2 || op.args.size() != 1 + op.carried_count ||
         op.outs.size() != op.carried_count ||
         !blk_args(0, op.carried_count) || !blk_args(1, op.carried_count)))
      detail::add_diag(store.diags, "if structure is inconsistent", op.loc);
    for (const auto arg : op.args) {
      if (arg >= store.vals.size() || !store.vals[arg].live) {
        detail::add_diag(store.diags, "operation uses an invalid value",
                         op.loc);
        continue;
      }
      if (!dom.has(arg, op_id))
        detail::add_diag(store.diags, "value does not dominate its use",
                         op.loc);
    }
    for (std::size_t index = 0; index < op.outs.size(); ++index) {
      const auto value = op.outs[index];
      if (value >= store.vals.size() || !store.vals[value].live ||
          store.vals[value].data.def != op_id ||
          store.vals[value].data.index != index)
        detail::add_diag(store.diags, "operation result is inconsistent",
                         op.loc);
    }
  }
  if (store.diags.empty()) {
    bool changed = normalize_void_statements(store, *this, env);
    for (std::size_t index = 0; index < original_types.size(); ++index)
      changed = store.vals[index].data.type != original_types[index] || changed;
    if (changed)
      detail::touch(store);
    return true;
  }

  std::vector<Diag> diagnostics = std::move(store.diags);
  for (std::size_t index = 0; index < original_types.size(); ++index)
    store.vals[index].data.type = std::move(original_types[index]);
  store.diags = std::move(diagnostics);
  return false;
}


}  // namespace joggle
