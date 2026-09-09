#include "joggle/joggle.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <utility>

namespace joggle {

namespace {

void print_diag(std::FILE* file, const Diag& diag) {
  const char* level = "error";
  if (diag.severity == Severity::note)
    level = "note";
  else if (diag.severity == Severity::warning)
    level = "warning";

  if (!diag.loc.file.empty()) {
    std::fprintf(file, "%s:%zu:%zu: %s: %s\n", diag.loc.file.c_str(),
                 diag.loc.line, diag.loc.column, level, diag.message.c_str());
  } else {
    std::fprintf(file, "%s: %s\n", level, diag.message.c_str());
  }
}

std::string_view trim(std::string_view text) {
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
    text.remove_prefix(1);
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
    text.remove_suffix(1);
  return text;
}

bool is_name(std::string_view text) {
  if (text.empty() ||
      !(std::isalpha(static_cast<unsigned char>(text.front())) ||
        text.front() == '_'))
    return false;
  return std::all_of(text.begin() + 1, text.end(), [](char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' ||
           ch == '.';
  });
}

} // namespace

struct Env::Impl {
  std::vector<std::filesystem::path> paths;
  std::vector<Diag> diags;
};

struct Mod::Impl {
  std::string name;
  std::vector<Diag> diags;
};

Env::Env() : impl_(std::make_unique<Impl>()) {}
Env::~Env() = default;
Env::Env(Env&&) noexcept = default;
Env& Env::operator=(Env&&) noexcept = default;

void Env::path(std::string path) { impl_->paths.emplace_back(std::move(path)); }

bool Env::load(std::string_view name) {
  for (const auto& root : impl_->paths) {
    if (std::filesystem::exists(root / name / "module.jog"))
      return true;
  }
  impl_->diags.push_back(
      {Severity::error, "module not found: " + std::string(name), {}});
  return false;
}

const std::vector<Diag>& Env::diags() const noexcept { return impl_->diags; }
void Env::clear_diags() noexcept { impl_->diags.clear(); }

int Env::print_diags(std::FILE* file) const {
  for (const Diag& diag : impl_->diags)
    print_diag(file, diag);
  return impl_->diags.empty() ? 0 : 1;
}

Mod::Mod() : impl_(std::make_unique<Impl>()) {}
Mod::~Mod() = default;
Mod::Mod(Mod&&) noexcept = default;
Mod& Mod::operator=(Mod&&) noexcept = default;

std::string_view Mod::name() const noexcept { return impl_->name; }
void Mod::name(std::string name) { impl_->name = std::move(name); }
bool Mod::ok() const noexcept { return impl_->diags.empty(); }
const std::vector<Diag>& Mod::diags() const noexcept { return impl_->diags; }
void Mod::clear_diags() noexcept { impl_->diags.clear(); }

int Mod::print_diags(std::FILE* file) const {
  for (const Diag& diag : impl_->diags)
    print_diag(file, diag);
  return impl_->diags.empty() ? 0 : 1;
}

bool parse(Env&, std::string_view source, Mod& out, std::string_view file) {
  out.impl_->diags.clear();
  source = trim(source);
  constexpr std::string_view prefix = "module ";
  if (!source.starts_with(prefix)) {
    out.impl_->diags.push_back({Severity::error, "expected 'module'",
                                {std::string(file), 1, 1}});
    return false;
  }
  source.remove_prefix(prefix.size());
  source = trim(source);
  const std::size_t end = source.find_first_of(" \t\r\n{");
  const std::string_view module_name = source.substr(0, end);
  if (!is_name(module_name)) {
    out.impl_->diags.push_back({Severity::error, "expected module name",
                                {std::string(file), 1, prefix.size() + 1}});
    return false;
  }
  out.impl_->name = std::string(module_name);
  return true;
}

std::string print(const Mod& mod) {
  return "module " + std::string(mod.name()) + "\n";
}

bool print(std::FILE* file, const Mod& mod) {
  const std::string text = print(mod);
  return std::fwrite(text.data(), 1, text.size(), file) == text.size();
}

} // namespace joggle
