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

struct SourcePosition {
  std::size_t line = 1;
  std::size_t column = 1;

  auto operator<=>(const SourcePosition&) const = default;
};

struct SourceRange {
  std::string source;
  SourcePosition begin;
  SourcePosition end;

  auto operator<=>(const SourceRange&) const = default;
};

struct Issue {
  std::string message;
  std::optional<SourceRange> source;
  std::vector<std::string> notes;
};

class Diag {
public:
  void report(Issue issue);
  void report(std::string message,
              std::optional<SourceRange> source = std::nullopt);

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
