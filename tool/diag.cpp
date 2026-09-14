#include "diag.h"

#include <cstdint>
#include <utility>

namespace joggle::tool {
namespace {

std::string_view severity(Severity value) noexcept {
  switch (value) {
    case Severity::note:
      return "note";
    case Severity::warning:
      return "warning";
    case Severity::error:
      return "error";
  }
  return "error";
}

Attr encode(const Diag& diag) {
  Attr::Dict value{
      {"message", diag.message},
      {"severity", std::string(severity(diag.severity))},
  };
  if (!diag.loc.file.empty())
    value.emplace("file", diag.loc.file);
  if (diag.loc.line != 0)
    value.emplace("line", static_cast<std::int64_t>(diag.loc.line));
  if (diag.loc.column != 0)
    value.emplace("column", static_cast<std::int64_t>(diag.loc.column));
  return Attr(std::move(value));
}

}  // namespace

bool parse_diag_format(std::string_view text, DiagFormat& out) noexcept {
  if (text == "text") {
    out = DiagFormat::text;
    return true;
  }
  if (text == "jog") {
    out = DiagFormat::jog;
    return true;
  }
  return false;
}

int print_diags(std::FILE* file, std::span<const Diag> diags,
                DiagFormat format) {
  if (format == DiagFormat::text) {
    for (const Diag& diag : diags) {
      const std::string_view level = severity(diag.severity);
      if (diag.loc.file.empty()) {
        std::fprintf(file, "%.*s: %s\n", static_cast<int>(level.size()),
                     level.data(), diag.message.c_str());
      } else {
        std::fprintf(file, "%s:%zu:%zu: %.*s: %s\n", diag.loc.file.c_str(),
                     diag.loc.line, diag.loc.column,
                     static_cast<int>(level.size()), level.data(),
                     diag.message.c_str());
      }
    }
  } else {
    Attr::List encoded;
    encoded.reserve(diags.size());
    for (const Diag& diag : diags)
      encoded.push_back(encode(diag));
    if (!joggle::print(file, Attr(std::move(encoded))) ||
        std::fputc('\n', file) == EOF)
      return 1;
  }
  return diags.empty() ? 0 : 1;
}

int print_error(std::FILE* file, std::string message, DiagFormat format,
                Loc loc) {
  if (format == DiagFormat::text && loc.file.empty()) {
    std::fprintf(file, "joggle: %s\n", message.c_str());
    return 1;
  }
  const Diag diag{Severity::error, std::move(message), std::move(loc)};
  return print_diags(file, std::span<const Diag>(&diag, 1), format);
}

}  // namespace joggle::tool
