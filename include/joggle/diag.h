#pragma once

#include <cstddef>
#include <iosfwd>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace joggle {

namespace detail {
struct DiagAccess;
}

struct Loc {
  struct Pos {
    std::size_t line = 1;
    std::size_t column = 1;

    auto operator<=>(const Pos&) const = default;
  };

  std::string source;
  Pos begin;
  Pos end;

  auto operator<=>(const Loc&) const = default;
};

struct Issue {
  std::string message;
  std::optional<Loc> source;
  std::vector<std::string> notes;
};

class Diag {
public:
  void report(Issue issue);
  void report(std::string message, std::optional<Loc> source = std::nullopt);

  bool ok() const { return issues_.empty(); }
  std::size_t size() const { return issues_.size(); }
  std::span<const Issue> issues() const { return issues_; }
  void print(std::ostream& output) const;
  void clear() { issues_.clear(); }

private:
  std::vector<Issue> issues_;
  friend struct detail::DiagAccess;
};

}  // namespace joggle
