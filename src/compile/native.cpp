#include "compile/compiler.h"

#include "joggle/detail/native.h"
#include "pkg/repo.h"

#include <cstring>
#include <exception>
#include <utility>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace joggle::detail {

Library::~Library() { close(); }

Library::Library(Library&& other) noexcept
    : handle_(std::exchange(other.handle_, nullptr)) {}

Library& Library::operator=(Library&& other) noexcept {
  if (this != &other) {
    close();
    handle_ = std::exchange(other.handle_, nullptr);
  }
  return *this;
}

std::optional<Library> Library::open(const std::filesystem::path& path,
                                     Diag& diagnostics) {
  Library result;
#if defined(_WIN32)
  result.handle_ = reinterpret_cast<void*>(LoadLibraryW(path.c_str()));
  if (result.handle_ == nullptr) {
    diagnostics.report("cannot load native library '" + path.string() + "'");
    return std::nullopt;
  }
#else
  result.handle_ = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (result.handle_ == nullptr) {
    const char* message = dlerror();
    diagnostics.report(
        "cannot load native library '" + path.string() +
        "': " + (message == nullptr ? "unknown error" : message));
    return std::nullopt;
  }
#endif
  return result;
}

void* Library::symbol(const char* name) const {
#if defined(_WIN32)
  return reinterpret_cast<void*>(
      GetProcAddress(reinterpret_cast<HMODULE>(handle_), name));
#else
  return dlsym(handle_, name);
#endif
}

void Library::close() {
  if (handle_ == nullptr) {
    return;
  }
#if defined(_WIN32)
  FreeLibrary(reinterpret_cast<HMODULE>(handle_));
#else
  dlclose(handle_);
#endif
  handle_ = nullptr;
}

std::string mod_identity(const Mod& mod) {
  return std::string(mod.name()) + "@" + to_string(mod.version()) + "#" +
         std::string(mod.digest());
}

std::string native_key(std::string_view mod, std::string_view target) {
  return std::string(mod) + "\n" + std::string(target);
}

}  // namespace joggle::detail

namespace joggle {

using detail::mod_identity;
using detail::native_key;

bool Compiler::load_native(std::string_view mod,
                           const std::filesystem::path& library) {
  if (!state_->linked) {
    state_->diagnostics.report(
        "cannot load native before the compiler is linked");
    return false;
  }
  const auto found = state_->mods.find(mod);
  if (found == state_->mods.end()) {
    state_->diagnostics.report("native target mod '" + std::string(mod) +
                               "' is not linked");
    return false;
  }
  return load_native(found->second, library);
}

bool Compiler::load_native(std::string_view mod) {
  if (!state_->linked) {
    state_->diagnostics.report(
        "cannot load native before the compiler is linked");
    return false;
  }
  const auto found = state_->mods.find(mod);
  if (found == state_->mods.end()) {
    state_->diagnostics.report("native target mod '" + std::string(mod) +
                               "' is not linked");
    return false;
  }
  return load_native(found->second);
}

bool Compiler::load_native(const Mod& mod,
                           const std::filesystem::path& library) {
  if (!state_->linked) {
    state_->diagnostics.report(
        "cannot load native before the compiler is linked");
    return false;
  }
  const auto loaded = state_->mods.find(mod.name());
  if (loaded == state_->mods.end() ||
      loaded->second.version() != mod.version() ||
      loaded->second.digest() != mod.digest()) {
    state_->diagnostics.report("native target '" + mod_identity(mod) +
                               "' is not part of this compiler");
    return false;
  }
  const std::string identity = mod_identity(mod);
  if (state_->loaded_natives.contains(identity)) {
    return true;
  }
  if (state_->has_lock) {
    const auto locked = state_->locked_natives.find(
        native_key(mod.name(), detail::native_target));
    if (locked == state_->locked_natives.end()) {
      state_->diagnostics.report("native for '" + identity +
                                 "' is absent from the lock file");
      return false;
    }
    const auto actual = detail::native_digest(library, state_->diagnostics);
    if (!actual || *actual != locked->second.digest) {
      state_->diagnostics.report("native library '" + library.string() +
                                 "' does not match locked digest " +
                                 locked->second.digest);
      return false;
    }
  }

  auto opened = detail::Library::open(library, state_->diagnostics);
  if (!opened) {
    return false;
  }
  void* address = opened->symbol(detail::native_entry);
  if (address == nullptr) {
    state_->diagnostics.report("native library '" + library.string() +
                               "' does not export " + detail::native_entry);
    return false;
  }
  static_assert(sizeof(detail::NativeEntry) == sizeof(address));
  detail::NativeEntry entry = nullptr;
  std::memcpy(&entry, &address, sizeof(entry));
  const detail::NativeLib* native = nullptr;
  try {
    native = entry();
  } catch (const std::exception& exception) {
    state_->diagnostics.report("native entry in '" + library.string() +
                               "' threw: " + exception.what());
    return false;
  } catch (...) {
    state_->diagnostics.report("native entry in '" + library.string() +
                               "' threw an unknown exception");
    return false;
  }
  if (native == nullptr || native->abi != detail::native_abi ||
      native->size < sizeof(detail::NativeLib) ||
      native->mod_identity == nullptr || native->target == nullptr ||
      native->load == nullptr) {
    state_->diagnostics.report("native library '" + library.string() +
                               "' has an incompatible descriptor");
    return false;
  }
  if (native->mod_identity != identity) {
    state_->diagnostics.report("native library '" + library.string() +
                               "' targets '" + native->mod_identity +
                               "', not '" + identity + "'");
    return false;
  }
  if (native->target != std::string_view(detail::native_target)) {
    state_->diagnostics.report(
        "native library '" + library.string() + "' targets '" + native->target +
        "', not host target '" + detail::native_target + "'");
    return false;
  }

  auto type_verifiers = state_->type_verifiers;
  auto op_verifiers = state_->op_verifiers;
  auto bindings = state_->bindings;
  auto hermetic_evaluations = state_->hermetic_evaluations;
  auto host_types = state_->host_types;
  auto host_representations = state_->host_representations;
  const std::size_t before = state_->diagnostics.size();
  try {
    native->load(*this, loaded->second, state_->diagnostics);
  } catch (const std::exception& exception) {
    state_->diagnostics.report("native binding for '" + identity +
                               "' threw: " + exception.what());
  } catch (...) {
    state_->diagnostics.report("native binding for '" + identity +
                               "' threw an unknown exception");
  }
  if (state_->diagnostics.size() != before) {
    state_->type_verifiers = std::move(type_verifiers);
    state_->op_verifiers = std::move(op_verifiers);
    state_->bindings = std::move(bindings);
    state_->hermetic_evaluations = std::move(hermetic_evaluations);
    state_->host_types = std::move(host_types);
    state_->host_representations = std::move(host_representations);
    return false;
  }

  state_->native_libraries.push_back(std::move(*opened));
  state_->loaded_natives.insert(identity);
  return true;
}

bool Compiler::load_native(const Mod& mod) {
  const auto source = state_->mod_sources.find(mod.name());
  if (source == state_->mod_sources.end()) {
    state_->diagnostics.report("mod '" + mod_identity(mod) +
                               "' has no installed Mod location");
    return false;
  }
  auto candidates =
      detail::native_candidates(source->second, state_->diagnostics);
  if (candidates.empty()) {
    state_->diagnostics.report("mod '" + mod_identity(mod) +
                               "' has no native for target '" +
                               detail::native_target + "'");
    return false;
  }
  if (candidates.size() != 1U) {
    state_->diagnostics.report("mod '" + mod_identity(mod) +
                               "' has ambiguous native for target '" +
                               detail::native_target + "'");
    return false;
  }
  return load_native(mod, candidates.front());
}

}  // namespace joggle

