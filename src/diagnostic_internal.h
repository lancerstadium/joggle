#pragma once

#include <cstddef>
#include <string>

#include "joggle/diagnostic.h"

namespace joggle::detail {

struct DiagnosticAccess {
  static void attach_since(Diagnostics& diagnostics, std::size_t first,
                           const SourceRange& source) {
    for (std::size_t index = first; index < diagnostics.entries_.size();
         ++index) {
      if (!diagnostics.entries_[index].source) {
        diagnostics.entries_[index].source = source;
      }
    }
  }

  static void note_since(Diagnostics& diagnostics, std::size_t first,
                         std::string note) {
    for (std::size_t index = first; index < diagnostics.entries_.size();
         ++index) {
      auto& notes = diagnostics.entries_[index].notes;
      if (notes.empty() || notes.back() != note) {
        notes.push_back(note);
      }
    }
  }
};

}  // namespace joggle::detail
