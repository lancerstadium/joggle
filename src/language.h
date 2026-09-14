#ifndef JOGGLE_LANGUAGE_H
#define JOGGLE_LANGUAGE_H

#include "detail.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace joggle::detail {

struct GenericInfo {
  std::string_view name;
  Ty type;
};

std::vector<GenericInfo> generic_info(Fn fn);
std::vector<GenericInfo> generic_info(const Store& store, const FnData& fn);
std::vector<std::string> generic_names(
    std::span<const GenericInfo> generics);
int precedence(std::string_view op);
bool intrinsic_cast(std::string_view name);
bool intrinsic_type(std::string_view name);

}  // namespace joggle::detail

#endif
