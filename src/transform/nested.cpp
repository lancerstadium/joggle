#include "transform/nested.h"

#include <algorithm>
#include <vector>

namespace joggle::detail {
namespace {

void collect(Val value, std::vector<Val>& found) {
  if (value.known() ||
      std::find(found.begin(), found.end(), value) != found.end()) {
    return;
  }
  if (value.inline_fn()) {
    found.push_back(value);
  }
  for (const Val& capture : value.captures()) {
    collect(capture, found);
  }
}

}  // namespace

std::vector<Val> nested_values(const Fn& fn) {
  std::vector<Val> found;
  for (const Blk& block : fn.blks()) {
    for (const Op& op : block.ops()) {
      collect(op.callee(), found);
      for (const Val& argument : op.arguments()) {
        collect(argument, found);
      }
    }
    const Term term = block.terminator();
    if (const auto condition = term.condition()) {
      collect(*condition, found);
    }
    for (const Val& value : term.returned()) {
      collect(value, found);
    }
    for (std::size_t successor = 0; successor < term.successor_count();
         ++successor) {
      for (const Val& value : term.arguments(successor)) {
        collect(value, found);
      }
    }
  }
  return found;
}

}  // namespace joggle::detail
