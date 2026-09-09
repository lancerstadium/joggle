#ifndef JOGGLE_JOGGLE_H
#define JOGGLE_JOGGLE_H

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace joggle {

inline constexpr unsigned version_major = 0;
inline constexpr unsigned version_minor = 1;
inline constexpr unsigned version_patch = 0;

enum class Severity : std::uint8_t { note, warning, error };

struct Loc {
  std::string file;
  std::size_t line = 0;
  std::size_t column = 0;
};

struct Diag {
  Severity severity = Severity::error;
  std::string message;
  Loc loc;
};

class Env {
public:
  Env();
  ~Env();
  Env(Env&&) noexcept;
  Env& operator=(Env&&) noexcept;
  Env(const Env&) = delete;
  Env& operator=(const Env&) = delete;

  void path(std::string path);
  bool load(std::string_view name);
  const std::vector<Diag>& diags() const noexcept;
  void clear_diags() noexcept;
  int print_diags(std::FILE* file) const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

class Mod {
public:
  Mod();
  ~Mod();
  Mod(Mod&&) noexcept;
  Mod& operator=(Mod&&) noexcept;
  Mod(const Mod&) = delete;
  Mod& operator=(const Mod&) = delete;

  std::string_view name() const noexcept;
  void name(std::string name);
  bool ok() const noexcept;
  const std::vector<Diag>& diags() const noexcept;
  void clear_diags() noexcept;
  int print_diags(std::FILE* file) const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;

  friend bool parse(Env&, std::string_view, Mod&, std::string_view);
  friend std::string print(const Mod&);
};

bool parse(Env& env, std::string_view source, Mod& out,
           std::string_view file = {});
std::string print(const Mod& mod);
bool print(std::FILE* file, const Mod& mod);

} // namespace joggle

#endif
