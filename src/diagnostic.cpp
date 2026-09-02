#include "joggle/diagnostic.h"

#include <ostream>
#include <utility>

namespace joggle {

void Diagnostics::report(Diagnostic diagnostic) {
  entries_.push_back(std::move(diagnostic));
}

void Diagnostics::report(std::string message,
                         std::optional<SourceRange> source) {
  report(Diagnostic{std::move(message), std::move(source), {}});
}

void Diagnostics::print(std::ostream& output) const {
  for (const Diagnostic& diagnostic : entries_) {
    if (diagnostic.source) {
      output << diagnostic.source->source << ':'
             << diagnostic.source->begin.line << ':'
             << diagnostic.source->begin.column << ": ";
    }
    output << "error: " << diagnostic.message << '\n';
    for (const std::string& note : diagnostic.notes) {
      output << "  note: " << note << '\n';
    }
  }
}

}  // namespace joggle
