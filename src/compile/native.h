#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "joggle/diag.h"
#include "joggle/mod.h"

namespace joggle::detail {

class Library {
public:
  Library() = default;
  ~Library();
  Library(const Library&) = delete;
  Library& operator=(const Library&) = delete;
  Library(Library&& other) noexcept;
  Library& operator=(Library&& other) noexcept;

  static std::optional<Library> open(const std::filesystem::path& path,
                                     Diag& diagnostics);
  void* symbol(const char* name) const;

private:
  void close();
  void* handle_ = nullptr;
};

std::string mod_identity(const Mod& mod);
std::string native_key(std::string_view mod, std::string_view target);

}  // namespace joggle::detail
