#pragma once

#include <iosfwd>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace joggle {

namespace detail {
struct DiagnosticAccess;
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

struct Diagnostic {
  std::string message;
  std::optional<SourceRange> source;
  std::vector<std::string> notes;
};

class Diagnostics {
public:
  void report(Diagnostic diagnostic);
  void report(std::string message,
              std::optional<SourceRange> source = std::nullopt);

  bool ok() const { return entries_.empty(); }
  std::size_t size() const { return entries_.size(); }
  std::span<const Diagnostic> entries() const { return entries_; }
  void print(std::ostream& output) const;
  void clear() { entries_.clear(); }

private:
  std::vector<Diagnostic> entries_;
  friend struct detail::DiagnosticAccess;
};

}  // namespace joggle
