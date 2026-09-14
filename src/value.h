#pragma once

#include <cstdint>
#include <unordered_set>
#include <vector>

namespace joggle::detail {

struct Store;

class ValFamilies {
public:
  explicit ValFamilies(const Store& store);

  std::uint32_t root(std::uint32_t id);
  std::unordered_set<std::uint32_t> members(std::uint32_t seed);

private:
  void join(std::uint32_t left, std::uint32_t right);

  const Store& store_;
  std::vector<std::uint32_t> parents_;
  std::vector<std::uint8_t> ranks_;
};

std::unordered_set<std::uint32_t> family(const Store& store,
                                         std::uint32_t seed);
bool printable_value(const Store& store,
                     const std::unordered_set<std::uint32_t>& values);

}  // namespace joggle::detail
