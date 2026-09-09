#include "detail.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

struct jog_module_v1 {
  void* state;
};

namespace joggle {

namespace {

struct Host {
  jog_host_fn_v1 function = nullptr;
  void* data = nullptr;
  Fn declaration;
};

struct CallState {
  std::span<const Attr> args;
  std::vector<Attr>* returns = nullptr;
  std::vector<bool> written;
  std::vector<Diag>* diags = nullptr;
  std::string symbol;
  bool failed = false;
};

struct LoadState {
  std::map<std::string, Host, std::less<>>* hosts = nullptr;
  std::vector<Diag>* diags = nullptr;
  Mod* module = nullptr;
  std::string name;
};

void close_library(void* handle) {
  if (!handle)
    return;
#if defined(_WIN32)
  FreeLibrary(static_cast<HMODULE>(handle));
#else
  dlclose(handle);
#endif
}

void* open_library(const std::filesystem::path& path, std::string& error) {
#if defined(_WIN32)
  void* handle = LoadLibraryW(path.wstring().c_str());
  if (!handle)
    error = "LoadLibrary failed";
#else
  void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!handle) {
    const char* message = dlerror();
    error = message ? message : "dlopen failed";
  }
#endif
  return handle;
}

jog_module_entry_v1 entry(void* handle) {
#if defined(_WIN32)
  return reinterpret_cast<jog_module_entry_v1>(
      GetProcAddress(static_cast<HMODULE>(handle), "joggle_module_v1"));
#else
  return reinterpret_cast<jog_module_entry_v1>(
      dlsym(handle, "joggle_module_v1"));
#endif
}

bool bind_host(jog_module_v1* opaque, const char* symbol,
               jog_host_fn_v1 function, void* data) {
  if (!opaque || !opaque->state || !symbol || !function)
    return false;
  auto& state = *static_cast<LoadState*>(opaque->state);
  const std::string full(symbol);
  const std::string prefix = state.name + ".";
  if (!full.starts_with(prefix)) {
    detail::add_diag(*state.diags, "native binding is outside module '" +
                                       state.name + "': " + full);
    return false;
  }
  const std::string local = full.substr(prefix.size());
  const Fn fn = state.module->find_fn(local);
  if (!fn || !fn.host()) {
    detail::add_diag(*state.diags,
                     "native binding has no matching [host] function: " + full);
    return false;
  }
  if (state.hosts->contains(full)) {
    detail::add_diag(*state.diags, "duplicate native binding: " + full);
    return false;
  }
  state.hosts->emplace(full, Host{function, data, fn});
  return true;
}

std::size_t call_arg_count(const jog_call_v1* call) {
  if (!call || !call->state)
    return 0;
  return static_cast<const CallState*>(call->state)->args.size();
}

bool encode(const Attr& input, jog_value_v1& output) {
  if (input.empty()) {
    output.kind = JOG_NIL_V1;
    output.data.handle = nullptr;
  } else if (const auto value = input.boolean()) {
    output.kind = JOG_BOOL_V1;
    output.data.boolean = *value;
  } else if (const auto value = input.integer()) {
    output.kind = JOG_I64_V1;
    output.data.integer = *value;
  } else if (const auto value = input.real()) {
    output.kind = JOG_F64_V1;
    output.data.real = *value;
  } else if (const auto value = input.string()) {
    output.kind = JOG_STR_V1;
    output.data.string = {value->data(), value->size()};
  } else
    return false;
  return true;
}

bool decode(const jog_value_v1& input, Attr& output) {
  switch (input.kind) {
  case JOG_NIL_V1:
    output = Attr{};
    return true;
  case JOG_BOOL_V1:
    output = Attr(input.data.boolean);
    return true;
  case JOG_I64_V1:
    output = Attr(input.data.integer);
    return true;
  case JOG_F64_V1:
    output = Attr(input.data.real);
    return true;
  case JOG_STR_V1:
    if (!input.data.string.data && input.data.string.size)
      return false;
    output = Attr(std::string(input.data.string.data, input.data.string.size));
    return true;
  case JOG_HANDLE_V1:
    return false;
  }
  return false;
}

bool call_arg(const jog_call_v1* call, std::size_t index, jog_value_v1* value) {
  if (!call || !call->state || !value)
    return false;
  const auto& state = *static_cast<const CallState*>(call->state);
  return index < state.args.size() && encode(state.args[index], *value);
}

bool call_ret(jog_call_v1* call, std::size_t index, const jog_value_v1* value) {
  if (!call || !call->state || !value)
    return false;
  auto& state = *static_cast<CallState*>(call->state);
  if (index >= state.returns->size() ||
      !decode(*value, (*state.returns)[index]))
    return false;
  state.written[index] = true;
  return true;
}

bool call_fail(jog_call_v1* call, const char* message) {
  if (!call || !call->state || !message)
    return false;
  auto& state = *static_cast<CallState*>(call->state);
  detail::add_diag(*state.diags,
                   "host function '" + state.symbol + "': " + message);
  state.failed = true;
  return false;
}

const jog_api_v1 host_api{module_abi_version, bind_host, call_arg_count,
                          call_arg,           call_ret,  call_fail};

bool scalar_matches(const Ty& type, const Attr& value) {
  const std::string_view name = type.text();
  if (name == "bool")
    return value.boolean().has_value();
  if (name == "f16" || name == "f32" || name == "f64")
    return value.real().has_value();
  if (name == "str")
    return value.string().has_value();
  if (name == "int" || name == "index" ||
      (name.size() > 1 && (name.front() == 'i' || name.front() == 'u')))
    return value.integer().has_value();
  return true;
}

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

}  // namespace

struct Env::Impl {
  std::vector<std::filesystem::path> paths;
  std::vector<Diag> diags;
  std::map<std::string, std::unique_ptr<Mod>, std::less<>> modules;
  std::set<std::string, std::less<>> loading;
  std::map<std::string, Host, std::less<>> hosts;
  std::vector<void*> libraries;

  ~Impl() {
    for (auto it = libraries.rbegin(); it != libraries.rend(); ++it)
      close_library(*it);
  }
};

Env::Env() : impl_(std::make_unique<Impl>()) {}
Env::~Env() = default;
Env::Env(Env&&) noexcept = default;
Env& Env::operator=(Env&&) noexcept = default;

void Env::path(std::string path) { impl_->paths.emplace_back(std::move(path)); }

bool Env::load(std::string_view name) {
  const std::string key(name);
  if (impl_->modules.contains(key))
    return true;
  if (impl_->loading.contains(key)) {
    impl_->diags.push_back(
        {Severity::error, "module dependency cycle at: " + key, {}});
    return false;
  }

  std::filesystem::path directory;
  for (const auto& root : impl_->paths) {
    const auto candidate = root / key;
    if (std::filesystem::is_regular_file(candidate / "module.jog")) {
      directory = candidate;
      break;
    }
  }
  if (directory.empty()) {
    impl_->diags.push_back({Severity::error, "module not found: " + key, {}});
    return false;
  }

  std::vector<std::filesystem::path> files{directory / "module.jog"};
  const auto library = directory / "lib";
  if (std::filesystem::is_directory(library)) {
    for (const auto& item : std::filesystem::directory_iterator(library)) {
      if (item.is_regular_file() && item.path().extension() == ".jog")
        files.push_back(item.path());
    }
    std::sort(files.begin() + 1, files.end());
  }

  std::ostringstream source;
  for (const auto& file : files) {
    std::ifstream input(file);
    if (!input) {
      impl_->diags.push_back(
          {Severity::error, "cannot read module source: " + file.string(), {}});
      return false;
    }
    source << input.rdbuf() << '\n';
  }

  impl_->loading.insert(key);
  auto module = std::make_unique<Mod>();
  if (!parse(*this, source.str(), *module, files.front().string())) {
    impl_->diags.insert(impl_->diags.end(), module->diags().begin(),
                        module->diags().end());
    impl_->loading.erase(key);
    return false;
  }
  if (module->name() != name) {
    impl_->diags.push_back({Severity::error,
                            "module declares '" + std::string(module->name()) +
                                "' but was loaded as '" + key + "'",
                            {files.front().string(), 1, 1}});
    impl_->loading.erase(key);
    return false;
  }
  for (const std::string& dependency : module->uses()) {
    if (!load(dependency)) {
      impl_->loading.erase(key);
      return false;
    }
  }
  if (!module->verify(*this)) {
    impl_->diags.insert(impl_->diags.end(), module->diags().begin(),
                        module->diags().end());
    impl_->loading.erase(key);
    return false;
  }
  Mod* module_ptr = module.get();
  impl_->modules.emplace(key, std::move(module));

  std::filesystem::path native;
  const auto native_dir = directory / "native";
  if (std::filesystem::is_directory(native_dir)) {
    const std::string leaf = key.substr(key.rfind('.') + 1);
    for (const std::string_view extension : {".so", ".dylib", ".dll"}) {
      const auto candidate =
          native_dir / ("joggle_" + leaf + std::string(extension));
      if (std::filesystem::is_regular_file(candidate)) {
        native = candidate;
        break;
      }
    }
  }
  if (!native.empty()) {
    std::string error;
    void* handle = open_library(native, error);
    if (!handle) {
      impl_->diags.push_back(
          {Severity::error,
           "cannot load native module '" + native.string() + "': " + error,
           {}});
      impl_->modules.erase(key);
      impl_->loading.erase(key);
      return false;
    }
    const jog_module_entry_v1 init = entry(handle);
    if (!init) {
      impl_->diags.push_back(
          {Severity::error,
           "native module has no joggle_module_v1 entry: " + native.string(),
           {}});
      close_library(handle);
      impl_->modules.erase(key);
      impl_->loading.erase(key);
      return false;
    }
    LoadState state{&impl_->hosts, &impl_->diags, module_ptr, key};
    jog_module_v1 opaque{&state};
    const std::size_t diag_count = impl_->diags.size();
    if (!init(&host_api, &opaque) || impl_->diags.size() != diag_count) {
      const std::string prefix = key + ".";
      std::erase_if(impl_->hosts, [&](const auto& item) {
        return item.first.starts_with(prefix);
      });
      detail::add_diag(impl_->diags,
                       "native module initialization failed: " + key);
      close_library(handle);
      impl_->modules.erase(key);
      impl_->loading.erase(key);
      return false;
    }
    impl_->libraries.push_back(handle);
  }
  impl_->loading.erase(key);
  return true;
}

bool Env::loaded(std::string_view name) const noexcept {
  return impl_->modules.contains(name);
}

Fn Env::find_fn(std::string_view symbol) const noexcept {
  std::size_t best = 0;
  Fn result;
  for (const auto& [name, module] : impl_->modules) {
    if (name.size() <= best || symbol.size() <= name.size() ||
        !symbol.starts_with(name) || symbol[name.size()] != '.')
      continue;
    Fn candidate = module->find_fn(symbol.substr(name.size() + 1));
    if (candidate) {
      best = name.size();
      result = candidate;
    }
  }
  return result;
}

bool Env::bound(std::string_view symbol) const noexcept {
  return impl_->hosts.contains(symbol);
}

bool Env::call(std::string_view symbol, std::span<const Attr> args,
               std::vector<Attr>& returns) {
  const auto found = impl_->hosts.find(symbol);
  if (found == impl_->hosts.end()) {
    detail::add_diag(impl_->diags,
                     "host function is not bound: " + std::string(symbol));
    return false;
  }
  const Host& host = found->second;
  const std::vector<Val> params = host.declaration.params();
  if (params.size() != args.size()) {
    detail::add_diag(impl_->diags,
                     "argument count does not match host declaration: " +
                         std::string(symbol));
    return false;
  }
  for (std::size_t index = 0; index < args.size(); ++index) {
    if (!scalar_matches(params[index].type(), args[index])) {
      detail::add_diag(impl_->diags,
                       "argument type does not match host declaration: " +
                           std::string(symbol));
      return false;
    }
  }
  returns.assign(host.declaration.returns().size(), Attr{});
  CallState state{args,
                  &returns,
                  std::vector<bool>(returns.size(), false),
                  &impl_->diags,
                  std::string(symbol),
                  false};
  jog_call_v1 call{&host_api, &state};
  if (!host.function(&call, host.data) || state.failed)
    return false;
  if (std::find(state.written.begin(), state.written.end(), false) !=
      state.written.end()) {
    detail::add_diag(impl_->diags,
                     "host function did not write every return: " +
                         std::string(symbol));
    return false;
  }
  const std::vector<Ty> types = host.declaration.returns();
  for (std::size_t index = 0; index < returns.size(); ++index) {
    if (!scalar_matches(types[index], returns[index])) {
      detail::add_diag(impl_->diags,
                       "return type does not match host declaration: " +
                           std::string(symbol));
      return false;
    }
  }
  return true;
}

std::vector<std::string> Env::modules() const {
  std::vector<std::string> out;
  out.reserve(impl_->modules.size());
  for (const auto& [name, ignored] : impl_->modules) {
    (void)ignored;
    out.push_back(name);
  }
  return out;
}

const std::vector<Diag>& Env::diags() const noexcept { return impl_->diags; }
void Env::clear_diags() noexcept { impl_->diags.clear(); }

void Env::error(std::string message, Loc loc) {
  detail::add_diag(impl_->diags, std::move(message), std::move(loc));
}

int Env::print_diags(std::FILE* file) const {
  return detail::print_diags(file, impl_->diags);
}

}  // namespace joggle

namespace joggle::detail {

void add_diag(std::vector<Diag>& diags, std::string message, Loc loc) {
  diags.push_back({Severity::error, std::move(message), std::move(loc)});
}

int print_diags(std::FILE* file, const std::vector<Diag>& diags) {
  for (const Diag& diag : diags)
    ::joggle::print_diag(file, diag);
  return diags.empty() ? 0 : 1;
}

}  // namespace joggle::detail
