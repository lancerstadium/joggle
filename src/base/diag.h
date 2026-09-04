#pragma once

#include <cstddef>
#include <string>

#include "joggle/diag.h"

namespace joggle::detail {

struct DiagAccess {
  static void attach_since(Diag& diagnostics, std::size_t first,
                           const Loc& source) {
    for (std::size_t index = first; index < diagnostics.issues_.size();
         ++index) {
      if (!diagnostics.issues_[index].source) {
        diagnostics.issues_[index].source = source;
      }
    }
  }

  static void note_since(Diag& diagnostics, std::size_t first,
                         std::string note) {
    for (std::size_t index = first; index < diagnostics.issues_.size();
         ++index) {
      auto& notes = diagnostics.issues_[index].notes;
      if (notes.empty() || notes.back() != note) {
        notes.push_back(note);
      }
    }
  }
};

}  // namespace joggle::detail
