#ifndef JOGGLE_TOOL_DIAG_H
#define JOGGLE_TOOL_DIAG_H

#include "joggle/joggle.h"

#include <cstdio>
#include <span>
#include <string>
#include <string_view>

namespace joggle::tool {

enum class DiagFormat { text, jog };

bool parse_diag_format(std::string_view text, DiagFormat& out) noexcept;
int print_diags(std::FILE* file, std::span<const Diag> diags,
                DiagFormat format);
int print_error(std::FILE* file, std::string message, DiagFormat format,
                Loc loc = {});

}  // namespace joggle::tool

#endif
