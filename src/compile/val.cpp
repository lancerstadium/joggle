#include "compile/eval.h"

#include "ir/fn.h"
#include "lang/prelude.h"
#include "ir/type.h"

#include <algorithm>
#include <stdexcept>
#include <typeinfo>
#include <unordered_set>
#include <utility>

namespace joggle::detail {
namespace {

template <typename T>
std::optional<ExecVal> list_exec_val(const ParamVal& value) {
  auto decoded = decode_parameter<std::vector<T>>(value);
  return decoded ? std::optional<ExecVal>{std::move(*decoded)} : std::nullopt;
}

template <typename T>
ParamVal list_parameter_value(const std::vector<T>& values) {
  std::vector<ParamVal> elements;
  elements.reserve(values.size());
  for (const T& value : values) {
    elements.emplace_back(value);
  }
  return ParamVal::list(std::move(elements));
}

ParamVal list_parameter_value(const std::vector<bool>& values) {
  std::vector<ParamVal> elements;
  elements.reserve(values.size());
  for (const bool value : values) {
    elements.emplace_back(value);
  }
  return ParamVal::list(std::move(elements));
}

}  // namespace

StagedVal::StagedVal(Type type, ExecVal value)
    : type_(std::move(type)), value_(std::move(value)) {}

StagedVal::StagedVal(Val value)
    : type_(value.type()), value_(std::move(value)) {
  if (std::get<Val>(value_).known()) {
    throw std::invalid_argument(
        "a Known IR value must enter staging through stage(Val)");
  }
}

bool StagedVal::known() const {
  return std::holds_alternative<ExecVal>(value_);
}

const Type& StagedVal::type() const { return type_; }

const ExecVal* StagedVal::known_value() const {
  return std::get_if<ExecVal>(&value_);
}

ExecVal* StagedVal::known_value() { return std::get_if<ExecVal>(&value_); }

const Val* StagedVal::residual_value() const {
  return std::get_if<Val>(&value_);
}

void Locals::push() { scopes_.emplace_back(); }

void Locals::pop() {
  if (!scopes_.empty()) {
    scopes_.pop_back();
  }
}

void Locals::resize(std::size_t depth) { scopes_.resize(depth); }

std::size_t Locals::depth() const { return scopes_.size(); }

bool Locals::define(std::string name, std::optional<StagedVal> value) {
  return !scopes_.empty() &&
         scopes_.back().emplace(std::move(name), std::move(value)).second;
}

bool Locals::assign(std::string_view name, std::optional<StagedVal> value) {
  for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
    const auto found = scope->find(std::string(name));
    if (found != scope->end()) {
      found->second = std::move(value);
      return true;
    }
  }
  return false;
}

bool Locals::contains(std::string_view name) const {
  return std::any_of(scopes_.rbegin(), scopes_.rend(), [&](const Scope& scope) {
    return scope.contains(std::string(name));
  });
}

StagedVal* Locals::find(std::string_view name) {
  for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
    const auto found = scope->find(std::string(name));
    if (found != scope->end()) {
      return found->second ? &*found->second : nullptr;
    }
  }
  return nullptr;
}

const StagedVal* Locals::find(std::string_view name) const {
  for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
    const auto found = scope->find(std::string(name));
    if (found != scope->end()) {
      return found->second ? &*found->second : nullptr;
    }
  }
  return nullptr;
}

std::vector<std::string> Locals::names() const {
  std::vector<std::string> names;
  std::unordered_set<std::string> seen;
  for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
    for (const auto& [name, value] : *scope) {
      if (!seen.insert(name).second || !value) {
        continue;
      }
      names.push_back(name);
    }
  }
  std::ranges::sort(names);
  return names;
}

KnownBindings Locals::known_bindings() const {
  KnownBindings bindings;
  for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
    for (const auto& [name, value] : *scope) {
      if (bindings.contains(name) || !value || !value->known()) {
        continue;
      }
      const ExecVal* known = value->known_value();
      if (known != nullptr) {
        if (auto payload = parameter_value(*known)) {
          bindings.emplace(name, KnownBinding{std::move(*payload),
                                              type_domain(value->type())});
        }
      }
    }
  }
  return bindings;
}

std::string_view exec_val_type(const ExecVal& value) {
  if (std::holds_alternative<std::int64_t>(value)) {
    return typeid(std::int64_t).name();
  }
  if (std::holds_alternative<double>(value)) {
    return typeid(double).name();
  }
  if (std::holds_alternative<bool>(value)) {
    return typeid(bool).name();
  }
  if (std::holds_alternative<std::string>(value)) {
    return typeid(std::string).name();
  }
  if (std::holds_alternative<Type>(value)) {
    return typeid(Type).name();
  }
  if (std::holds_alternative<Bytes>(value)) {
    return typeid(Bytes).name();
  }
  if (std::holds_alternative<std::shared_ptr<Fn>>(value)) {
    return typeid(Fn).name();
  }
  if (std::holds_alternative<IntegerList>(value)) {
    return typeid(IntegerList).name();
  }
  if (std::holds_alternative<RealList>(value)) {
    return typeid(RealList).name();
  }
  if (std::holds_alternative<BooleanList>(value)) {
    return typeid(BooleanList).name();
  }
  if (std::holds_alternative<StringList>(value)) {
    return typeid(StringList).name();
  }
  if (std::holds_alternative<TypeList>(value)) {
    return typeid(TypeList).name();
  }
  if (const auto* host = std::get_if<HostVal>(&value)) {
    return host->cpp_type;
  }
  return typeid(void).name();
}

std::optional<std::vector<ExecVal>> list_elements(const ExecVal& value) {
  return std::visit(
      []<typename T>(const T& stored) -> std::optional<std::vector<ExecVal>> {
        using Stored = std::remove_cvref_t<T>;
        if constexpr (std::is_same_v<Stored, IntegerList> ||
                      std::is_same_v<Stored, RealList> ||
                      std::is_same_v<Stored, BooleanList> ||
                      std::is_same_v<Stored, StringList> ||
                      std::is_same_v<Stored, TypeList>) {
          std::vector<ExecVal> result;
          result.reserve(stored.size());
          for (const auto& element : stored) {
            result.emplace_back(element);
          }
          return result;
        } else {
          return std::nullopt;
        }
      },
      value);
}

std::optional<Domain> cpp_value_domain(std::string_view type) {
  if (type == typeid(std::int64_t).name()) {
    return Domain{ValKind::Integer, false};
  }
  if (type == typeid(double).name()) {
    return Domain{ValKind::Real, false};
  }
  if (type == typeid(bool).name()) {
    return Domain{ValKind::Boolean, false};
  }
  if (type == typeid(std::string).name()) {
    return Domain{ValKind::String, false};
  }
  if (type == typeid(Type).name()) {
    return Domain{ValKind::Type, false};
  }
  if (type == typeid(Bytes).name()) {
    return Domain{ValKind::Bytes, false};
  }
  if (type == typeid(Fn).name()) {
    return Domain{ValKind::Fn, false};
  }
  if (type == typeid(IntegerList).name()) {
    return Domain{ValKind::Integer, true};
  }
  if (type == typeid(RealList).name()) {
    return Domain{ValKind::Real, true};
  }
  if (type == typeid(BooleanList).name()) {
    return Domain{ValKind::Boolean, true};
  }
  if (type == typeid(StringList).name()) {
    return Domain{ValKind::String, true};
  }
  if (type == typeid(TypeList).name()) {
    return Domain{ValKind::Type, true};
  }
  return std::nullopt;
}

std::optional<Type> domain_type(Compiler& compiler,
                                const Mod::Expr& expression) {
  const auto domain = kernel_domain(expression);
  if (!domain) {
    return std::nullopt;
  }
  if (!domain->list) {
    return compiler.make(domain_name(domain->element));
  }
  if (expression.arguments.size() != 1U) {
    return std::nullopt;
  }
  auto element = domain_type(compiler, expression.arguments.front());
  const auto prelude = compiler.mod(prelude_mod_name);
  const auto list =
      prelude ? prelude->type("list") : std::optional<Mod::TypeDecl>{};
  return element && list ? compiler.make(*list, *element)
                         : std::optional<Type>{};
}

std::optional<Mod::Expr> type_domain(const Type& type) {
  const Mod::Symbol symbol = type.schema().symbol();
  if (symbol.mod_name() != prelude_mod_name) {
    return std::nullopt;
  }
  if (symbol.local_name() != "list") {
    const auto expression =
        Mod::Expr::reference(std::string(symbol.local_name()));
    return kernel_domain(expression) ? std::optional<Mod::Expr>{expression}
                                     : std::nullopt;
  }
  const auto parameters = TypeAccess::parameters(type);
  const Type* element =
      parameters.size() == 1U ? parameters.front().as_type() : nullptr;
  auto domain = element ? type_domain(*element) : std::optional<Mod::Expr>{};
  return domain ? std::optional<Mod::Expr>{Mod::Expr::list_domain(
                      std::move(*domain))}
                : std::nullopt;
}

std::optional<Type> execution_type(Compiler& compiler, const ExecVal& value) {
  if (const auto* host = std::get_if<HostVal>(&value)) {
    return host->concrete_type;
  }
  const auto domain = cpp_value_domain(exec_val_type(value));
  return domain ? domain_type(compiler,
                              domain_expression(domain->element, domain->list))
                : std::optional<Type>{};
}

std::optional<StagedVal> stage(Compiler& compiler, ExecVal value) {
  auto type = execution_type(compiler, value);
  return type ? std::optional<StagedVal>{std::in_place, std::move(*type),
                                         std::move(value)}
              : std::nullopt;
}

std::optional<StagedVal> stage(Val value) {
  if (!value.known()) {
    return StagedVal(std::move(value));
  }
  const auto domain = type_domain(value.type());
  const auto payload = FnAccess::known_value(value);
  const Mod::ParamDecl parameter{"value", domain ? *domain : Mod::Expr{}, false,
                                 std::nullopt};
  auto known = domain && payload ? exec_val(*payload, parameter)
                                 : std::optional<ExecVal>{};
  return known ? std::optional<StagedVal>{std::in_place, value.type(),
                                          std::move(*known)}
               : std::nullopt;
}

std::optional<Val> ir_value(Compiler& compiler, const StagedVal& value) {
  if (const Val* residual = value.residual_value()) {
    return *residual;
  }
  const ExecVal* known = value.known_value();
  auto payload = known ? parameter_value(*known) : std::optional<ParamVal>{};
  return payload ? compiler.known(value.type(), std::move(*payload))
                 : std::optional<Val>{};
}

std::optional<bool> known_boolean(const StagedVal& value) {
  const ExecVal* known = value.known_value();
  const bool* boolean = known ? std::get_if<bool>(known) : nullptr;
  return boolean ? std::optional<bool>{*boolean} : std::nullopt;
}

bool same_staged_value(const StagedVal& lhs, const StagedVal& rhs) {
  if (lhs.type() != rhs.type() || lhs.known() != rhs.known()) {
    return false;
  }
  if (!lhs.known()) {
    const Val* left = lhs.residual_value();
    const Val* right = rhs.residual_value();
    return left != nullptr && right != nullptr && *left == *right;
  }
  const ExecVal* left = lhs.known_value();
  const ExecVal* right = rhs.known_value();
  if (left == nullptr || right == nullptr || left->index() != right->index()) {
    return false;
  }
  const auto left_parameter = parameter_value(*left);
  const auto right_parameter = parameter_value(*right);
  if (left_parameter || right_parameter) {
    return left_parameter && right_parameter &&
           *left_parameter == *right_parameter;
  }
  if (const auto* bytes = std::get_if<Bytes>(left)) {
    return *bytes == std::get<Bytes>(*right);
  }
  if (const auto* fn = std::get_if<std::shared_ptr<Fn>>(left)) {
    return *fn == std::get<std::shared_ptr<Fn>>(*right);
  }
  if (const auto* host = std::get_if<HostVal>(left)) {
    const auto& other = std::get<HostVal>(*right);
    return host->cpp_type == other.cpp_type && host->storage == other.storage &&
           host->concrete_type == other.concrete_type;
  }
  return false;
}

std::optional<ExecVal> exec_val(const ParamVal& value,
                                const Mod::ParamDecl& parameter) {
  const auto domain = kernel_domain(parameter.domain);
  if (domain && domain->list) {
    switch (domain->element) {
    case ValKind::Integer:
      return list_exec_val<std::int64_t>(value);
    case ValKind::Real:
      return list_exec_val<double>(value);
    case ValKind::Boolean:
      return list_exec_val<bool>(value);
    case ValKind::String:
      return list_exec_val<std::string>(value);
    case ValKind::Type:
      return list_exec_val<Type>(value);
    case ValKind::Fn:
    case ValKind::Bytes:
      return std::nullopt;
    }
  }
  switch (value.kind()) {
  case ParamVal::Kind::I64:
    return ExecVal{*value.as_i64()};
  case ParamVal::Kind::F64:
    return ExecVal{*value.as_f64()};
  case ParamVal::Kind::Boolean:
    return ExecVal{*value.as_bool()};
  case ParamVal::Kind::String:
    return ExecVal{*value.as_string()};
  case ParamVal::Kind::Type:
    return ExecVal{*value.as_type()};
  case ParamVal::Kind::List:
    return std::nullopt;
  }
  return std::nullopt;
}

std::optional<ParamVal> parameter_value(const ExecVal& value) {
  if (const auto* stored = std::get_if<std::int64_t>(&value)) {
    return ParamVal(*stored);
  }
  if (const auto* stored = std::get_if<double>(&value)) {
    return ParamVal(*stored);
  }
  if (const auto* stored = std::get_if<bool>(&value)) {
    return ParamVal(*stored);
  }
  if (const auto* stored = std::get_if<std::string>(&value)) {
    return ParamVal(*stored);
  }
  if (const auto* stored = std::get_if<Type>(&value)) {
    return ParamVal(*stored);
  }
  if (const auto* stored = std::get_if<IntegerList>(&value)) {
    return list_parameter_value(*stored);
  }
  if (const auto* stored = std::get_if<RealList>(&value)) {
    return list_parameter_value(*stored);
  }
  if (const auto* stored = std::get_if<BooleanList>(&value)) {
    return list_parameter_value(*stored);
  }
  if (const auto* stored = std::get_if<StringList>(&value)) {
    return list_parameter_value(*stored);
  }
  if (const auto* stored = std::get_if<TypeList>(&value)) {
    return list_parameter_value(*stored);
  }
  return std::nullopt;
}

}  // namespace joggle::detail
