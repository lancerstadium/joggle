#include "language.h"

namespace joggle::detail {

std::vector<GenericInfo> generic_info(Fn fn) {
  std::vector<GenericInfo> out;
  for (const Val generic : fn.generics())
    out.push_back({generic.name(), generic.type()});
  return out;
}

std::vector<GenericInfo> generic_info(const Store& store, const FnData& fn) {
  std::vector<GenericInfo> out;
  out.reserve(fn.generic_vals.size());
  for (const std::uint32_t id : fn.generic_vals)
    out.push_back({store.vals[id].data.name, store.vals[id].data.type});
  return out;
}

std::vector<std::string> generic_names(
    std::span<const GenericInfo> generics) {
  std::vector<std::string> out;
  out.reserve(generics.size());
  for (const GenericInfo& generic : generics)
    out.emplace_back(generic.name);
  return out;
}

int precedence(std::string_view op) {
  if (op == "||")
    return 1;
  if (op == "&&")
    return 2;
  if (op == "==" || op == "!=")
    return 3;
  if (op == "<" || op == "<=" || op == ">" || op == ">=")
    return 4;
  if (op == "|" || op == "^" || op == "&")
    return 5;
  if (op == "<<" || op == ">>")
    return 6;
  if (op == "+" || op == "-")
    return 7;
  if (op == "*" || op == "/" || op == "%")
    return 8;
  return -1;
}

bool intrinsic_cast(std::string_view name) {
  static constexpr std::string_view names[] = {"f16", "f32", "f64"};
  for (const std::string_view scalar : names)
    if (scalar == name)
      return true;
  return sized_integer_type(name);
}

bool intrinsic_type(std::string_view name) {
  static constexpr std::string_view names[] = {
      "_",     "nil", "bool", "int", "index", "str", "bytes", "dict",
      "list",  "range", "Ty", "Attr", "Mod", "Fn", "Blk", "Op", "Val",
      "meta"};
  if (intrinsic_cast(name))
    return true;
  for (const std::string_view intrinsic : names)
    if (intrinsic == name)
      return true;
  return false;
}

}  // namespace joggle::detail
