#pragma once

#include <cstddef>

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
};

}  // namespace joggle::detail
