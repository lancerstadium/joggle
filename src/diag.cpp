#include "joggle/diag.h"

#include <ostream>
#include <utility>

namespace joggle {

void Diag::report(Issue issue) { issues_.push_back(std::move(issue)); }

void Diag::report(std::string message, std::optional<SourceRange> source) {
  report(Issue{std::move(message), std::move(source), {}});
}

void Diag::print(std::ostream& output) const {
  for (const Issue& issue : issues_) {
    if (issue.source) {
      output << issue.source->source << ':' << issue.source->begin.line << ':'
             << issue.source->begin.column << ": ";
    }
    output << "error: " << issue.message << '\n';
    for (const std::string& note : issue.notes) {
      output << "  note: " << note << '\n';
    }
  }
}

}  // namespace joggle
