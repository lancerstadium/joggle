#include "detail.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

struct joggle_module {
  void* state;
};

namespace joggle {

namespace {

std::uint64_t next_env_id() {
  static std::atomic<std::uint64_t> next{1};
  return next.fetch_add(1, std::memory_order_relaxed);
}

void clear_absent(std::error_code& error) {
  if (error == std::errc::no_such_file_or_directory ||
      error == std::errc::not_a_directory)
    error.clear();
}

struct Native {
  joggle_fn function = nullptr;
  void* data = nullptr;
  std::vector<Fn> declarations;
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
  std::map<std::string, Native, std::less<>>* natives = nullptr;
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

joggle_module_entry entry(void* handle) {
#if defined(_WIN32)
  return reinterpret_cast<joggle_module_entry>(
      GetProcAddress(static_cast<HMODULE>(handle), "joggle_module"));
#else
  return reinterpret_cast<joggle_module_entry>(dlsym(handle, "joggle_module"));
#endif
}

bool bind_native(joggle_module* opaque, const char* symbol, joggle_fn function,
                 void* data) {
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
  const std::vector<Fn> declarations = state.module->find_fns(local);
  if (declarations.empty() ||
      std::any_of(declarations.begin(), declarations.end(),
                  [](Fn fn) { return !fn.external(); })) {
    detail::add_diag(*state.diags,
                     "native binding has no matching external function: " +
                         full);
    return false;
  }
  if (state.natives->contains(full)) {
    detail::add_diag(*state.diags, "duplicate native binding: " + full);
    return false;
  }
  state.natives->emplace(full, Native{function, data, declarations});
  return true;
}

std::size_t call_arg_count(const joggle_call* call) {
  if (!call || !call->state)
    return 0;
  return static_cast<const CallState*>(call->state)->args.size();
}

bool encode(const Attr& input, joggle_value& output) {
  if (input.empty()) {
    output.kind = JOGGLE_NIL;
    output.data.handle = nullptr;
  } else if (const auto value = input.boolean()) {
    output.kind = JOGGLE_BOOL;
    output.data.boolean = *value;
  } else if (const auto value = input.integer()) {
    output.kind = JOGGLE_I64;
    output.data.integer = *value;
  } else if (const auto value = input.real()) {
    output.kind = JOGGLE_F64;
    output.data.real = *value;
  } else if (const auto value = input.string()) {
    output.kind = JOGGLE_STR;
    output.data.string = {value->data(), value->size()};
  } else if (const auto* value = input.bytes()) {
    output.kind = JOGGLE_BYTES;
    output.data.bytes = {reinterpret_cast<const char*>(value->data()),
                         value->size()};
  } else
    return false;
  return true;
}

bool decode(const joggle_value& input, Attr& output) {
  switch (input.kind) {
  case JOGGLE_NIL:
    output = Attr{};
    return true;
  case JOGGLE_BOOL:
    output = Attr(input.data.boolean);
    return true;
  case JOGGLE_I64:
    output = Attr(input.data.integer);
    return true;
  case JOGGLE_F64:
    output = Attr(input.data.real);
    return true;
  case JOGGLE_STR:
    if (!input.data.string.data && input.data.string.size)
      return false;
    if (!input.data.string.size) {
      output = Attr(std::string{});
      return true;
    }
    output = Attr(std::string(input.data.string.data, input.data.string.size));
    return true;
  case JOGGLE_HANDLE:
    return false;
  case JOGGLE_BYTES:
    if (!input.data.bytes.data && input.data.bytes.size)
      return false;
    if (!input.data.bytes.size) {
      output = Attr(Attr::Bytes{});
      return true;
    }
    const auto* first =
        reinterpret_cast<const std::uint8_t*>(input.data.bytes.data);
    output = Attr(Attr::Bytes(first, first + input.data.bytes.size));
    return true;
  }
  return false;
}

bool call_arg(const joggle_call* call, std::size_t index, joggle_value* value) {
  if (!call || !call->state || !value)
    return false;
  const auto& state = *static_cast<const CallState*>(call->state);
  return index < state.args.size() && encode(state.args[index], *value);
}

bool call_ret(joggle_call* call, std::size_t index, const joggle_value* value) {
  if (!call || !call->state || !value)
    return false;
  auto& state = *static_cast<CallState*>(call->state);
  if (index >= state.returns->size() ||
      !decode(*value, (*state.returns)[index]))
    return false;
  state.written[index] = true;
  return true;
}

bool call_fail(joggle_call* call, const char* message) {
  if (!call || !call->state || !message)
    return false;
  auto& state = *static_cast<CallState*>(call->state);
  detail::add_diag(*state.diags,
                   "native function '" + state.symbol + "': " + message);
  state.failed = true;
  return false;
}

const joggle_api native_api{abi_version,    sizeof(joggle_api), bind_native,
                            call_arg_count, call_arg,           call_ret,
                            call_fail};

bool scalar_matches(const Ty& type, const Attr& value) {
  const std::string_view name = type.text();
  if (name == "_" || name == "Attr")
    return value.empty() || value.boolean().has_value() ||
           value.integer().has_value() || value.real().has_value() ||
           value.string().has_value() || value.bytes() != nullptr;
  if (name == "nil")
    return value.empty();
  if (name == "bool")
    return value.boolean().has_value();
  if (name == "f16" || name == "f32" || name == "f64")
    return value.real().has_value();
  if (name == "str")
    return value.string().has_value();
  if (name == "bytes")
    return value.bytes() != nullptr;
  if (detail::integer_type(name))
    return value.integer().has_value();
  return false;
}

Ty scalar_type(const Attr& value) {
  if (value.empty())
    return Ty("nil");
  if (value.boolean())
    return Ty("bool");
  if (value.integer())
    return Ty("int");
  if (value.real())
    return Ty("f64");
  if (value.string())
    return Ty("str");
  if (value.bytes())
    return Ty("bytes");
  if (value.list())
    return Ty("list<_>");
  if (value.dict())
    return Ty("dict");
  return Ty("Attr");
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
  std::uint64_t id = next_env_id();
  std::uint64_t epoch = 0;
  std::vector<std::filesystem::path> paths;
  std::vector<Diag> diags;
  std::map<std::string, std::unique_ptr<Mod>, std::less<>> modules;
  std::set<std::string, std::less<>> loading;
  std::map<std::string, Native, std::less<>> natives;
  std::vector<void*> libraries;
  mutable std::map<std::vector<std::string>, std::vector<const Mod*>>
      visibility;

  ~Impl() {
    for (auto it = libraries.rbegin(); it != libraries.rend(); ++it)
      close_library(*it);
  }
};

std::uint64_t Env::cache_id() const noexcept { return impl_->id; }

std::uint64_t Env::cache_epoch() const noexcept { return impl_->epoch; }

Env::Env() : impl_(std::make_unique<Impl>()) {}
Env::~Env() = default;
Env::Env(Env&&) noexcept = default;
Env& Env::operator=(Env&&) noexcept = default;

void Env::path(std::string path) { impl_->paths.emplace_back(std::move(path)); }

bool Env::load(std::string_view name) {
  if (!detail::valid_qualified_name(name)) {
    detail::add_diag(impl_->diags,
                     "invalid module name '" + std::string(name) + "'");
    return false;
  }
  const std::string key(name);
  if (impl_->modules.contains(key))
    return true;

  struct Rollback {
    Impl& env;
    std::set<std::string, std::less<>> modules;
    std::set<std::string, std::less<>> natives;
    std::set<std::string, std::less<>> loading;
    std::size_t libraries = 0;
    std::uint64_t epoch = 0;
    bool active = true;

    explicit Rollback(Impl& state)
        : env(state), loading(state.loading), libraries(state.libraries.size()),
          epoch(state.epoch) {
      for (const auto& [module, ignored] : state.modules) {
        (void)ignored;
        modules.insert(module);
      }
      for (const auto& [symbol, ignored] : state.natives) {
        (void)ignored;
        natives.insert(symbol);
      }
    }

    ~Rollback() {
      if (!active)
        return;
      std::erase_if(env.natives, [&](const auto& item) {
        return !natives.contains(item.first);
      });
      while (env.libraries.size() > libraries) {
        close_library(env.libraries.back());
        env.libraries.pop_back();
      }
      std::erase_if(env.modules, [&](const auto& item) {
        return !modules.contains(item.first);
      });
      env.visibility.clear();
      env.loading = std::move(loading);
      env.epoch = epoch;
    }
  } rollback(*impl_);

  const bool loaded = load_one(name);
  if (loaded)
    rollback.active = false;
  return loaded;
}

bool Env::load_one(std::string_view name) {
  const std::string key(name);
  if (impl_->modules.contains(key))
    return true;
  if (impl_->loading.contains(key)) {
    impl_->diags.push_back(
        {Severity::error, "module dependency cycle at: " + key, {}});
    return false;
  }

  std::filesystem::path directory;
  std::error_code file_error;
  for (const auto& root : impl_->paths) {
    const auto candidate = root / key;
    const bool found = std::filesystem::is_regular_file(
        candidate / "module.jog", file_error);
    clear_absent(file_error);
    if (file_error) {
      impl_->diags.push_back(
          {Severity::error,
           "cannot inspect module source '" +
               (candidate / "module.jog").string() + "': " +
               file_error.message(),
           {}});
      return false;
    }
    if (found) {
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
  const bool has_library = std::filesystem::is_directory(library, file_error);
  clear_absent(file_error);
  if (file_error) {
    impl_->diags.push_back(
        {Severity::error,
         "cannot inspect module fragments '" + library.string() + "': " +
             file_error.message(),
         {}});
    return false;
  }
  if (has_library) {
    std::filesystem::directory_iterator items(library, file_error), end;
    while (!file_error && items != end) {
      const bool regular = items->is_regular_file(file_error);
      if (!file_error && regular && items->path().extension() == ".jog")
        files.push_back(items->path());
      items.increment(file_error);
    }
    if (file_error) {
      impl_->diags.push_back(
          {Severity::error,
           "cannot enumerate module fragments '" + library.string() +
               "': " + file_error.message(),
           {}});
      return false;
    }
    std::sort(files.begin() + 1, files.end());
  }

  std::vector<Source> sources;
  sources.reserve(files.size());
  for (const auto& file : files) {
    std::ifstream input(file);
    if (!input) {
      impl_->diags.push_back(
          {Severity::error, "cannot read module source: " + file.string(), {}});
      return false;
    }
    std::ostringstream source;
    source << input.rdbuf();
    sources.push_back({source.str(), file.string()});
  }

  impl_->loading.insert(key);
  auto module = std::make_unique<Mod>();
  if (!parse(*this, sources, *module)) {
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
    if (!load_one(dependency)) {
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
  const bool has_native =
      std::filesystem::is_directory(native_dir, file_error);
  clear_absent(file_error);
  if (file_error) {
    impl_->diags.push_back(
        {Severity::error,
         "cannot inspect native module directory '" + native_dir.string() +
             "': " + file_error.message(),
         {}});
    impl_->modules.erase(key);
    impl_->loading.erase(key);
    return false;
  }
  if (has_native) {
    const std::string leaf = key.substr(key.rfind('.') + 1);
    for (const std::string_view extension : {".so", ".dylib", ".dll"}) {
      const auto candidate =
          native_dir / ("joggle_" + leaf + std::string(extension));
      const bool found =
          std::filesystem::is_regular_file(candidate, file_error);
      clear_absent(file_error);
      if (file_error) {
        impl_->diags.push_back(
            {Severity::error,
             "cannot inspect native module '" + candidate.string() +
                 "': " + file_error.message(),
             {}});
        impl_->modules.erase(key);
        impl_->loading.erase(key);
        return false;
      }
      if (found) {
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
    const joggle_module_entry init = entry(handle);
    if (!init) {
      impl_->diags.push_back(
          {Severity::error,
           "native module has no joggle_module entry: " + native.string(),
           {}});
      close_library(handle);
      impl_->modules.erase(key);
      impl_->loading.erase(key);
      return false;
    }
    LoadState state{&impl_->natives, &impl_->diags, module_ptr, key};
    joggle_module opaque{&state};
    const std::size_t diag_count = impl_->diags.size();
    if (!init(&native_api, &opaque) || impl_->diags.size() != diag_count) {
      const std::string prefix = key + ".";
      std::erase_if(impl_->natives, [&](const auto& item) {
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
  impl_->visibility.clear();
  ++impl_->epoch;
  return true;
}

bool Env::loaded(std::string_view name) const noexcept {
  return impl_->modules.contains(name);
}

std::vector<Fn> Env::fns(std::string_view module) const {
  const auto found = impl_->modules.find(module);
  if (found == impl_->modules.end())
    return {};
  std::vector<Fn> result = found->second->fns();
  std::erase_if(result, [](Fn fn) { return fn.local(); });
  return result;
}

std::vector<Fn> Env::find_fns(std::string_view symbol) const {
  std::size_t best = 0;
  std::vector<Fn> result;
  for (const auto& [name, module] : impl_->modules) {
    if (name.size() <= best || symbol.size() <= name.size() ||
        !symbol.starts_with(name) || symbol[name.size()] != '.')
      continue;
    std::vector<Fn> candidates =
        module->find_fns(symbol.substr(name.size() + 1));
    std::erase_if(candidates, [](Fn fn) { return fn.local(); });
    if (!candidates.empty()) {
      best = name.size();
      result = std::move(candidates);
    }
  }
  return result;
}

Fn Env::find_fn(std::string_view symbol) const {
  const std::vector<Fn> matches = find_fns(symbol);
  return matches.size() == 1 ? matches.front() : Fn{};
}

bool Env::declared(std::string_view symbol) const {
  std::size_t best = 0;
  bool result = false;
  for (const auto& [name, module] : impl_->modules) {
    if (name.size() <= best || symbol.size() <= name.size() ||
        !symbol.starts_with(name) || symbol[name.size()] != '.')
      continue;
    if (!module->find_fns(symbol.substr(name.size() + 1)).empty()) {
      best = name.size();
      result = true;
    }
  }
  return result;
}

std::vector<Fn> Env::resolve_fns(const Mod& from,
                                 std::string_view symbol) const {
  return resolve_fns(from.impl_->store, symbol);
}

std::vector<Fn> Env::resolve_fns(const detail::Store& from,
                                 std::string_view symbol) const {
  const auto local = [&](std::string_view name) {
    std::vector<Fn> result;
    const auto found = from.symbols.find(std::string(name));
    if (found == from.symbols.end())
      return result;
    result.reserve(found->second.size());
    for (const std::uint32_t id : found->second)
      if (id < from.fns.size() && from.fns[id].live)
        result.push_back(Fn(const_cast<detail::Store*>(&from), id,
                            from.fns[id].generation));
    return result;
  };

  auto visible = impl_->visibility.find(from.uses);
  if (visible == impl_->visibility.end()) {
    std::vector<std::string> pending = from.uses;
    std::set<std::string, std::less<>> visited;
    while (!pending.empty()) {
      std::string name = std::move(pending.back());
      pending.pop_back();
      if (!visited.insert(name).second)
        continue;
      const auto dependency = impl_->modules.find(name);
      if (dependency == impl_->modules.end())
        continue;
      const std::vector<std::string> next = dependency->second->uses();
      pending.insert(pending.end(), next.begin(), next.end());
    }
    std::vector<const Mod*> modules;
    modules.reserve(visited.size());
    for (const std::string& name : visited) {
      const auto module = impl_->modules.find(name);
      if (module != impl_->modules.end())
        modules.push_back(module->second.get());
    }
    visible = impl_->visibility.emplace(from.uses, std::move(modules)).first;
  }
  const std::vector<const Mod*>& modules = visible->second;

  if (symbol.find('.') != std::string_view::npos) {
    std::size_t best = 0;
    std::vector<Fn> matches;
    const auto consider = [&](std::string_view name,
                              std::vector<Fn> candidates) {
      if (name.size() <= best || symbol.size() <= name.size() ||
          !symbol.starts_with(name) || symbol[name.size()] != '.' ||
          candidates.empty())
        return;
      best = name.size();
      matches = std::move(candidates);
    };
    if (symbol.size() > from.name.size() && symbol.starts_with(from.name) &&
        symbol[from.name.size()] == '.')
      consider(from.name, local(symbol.substr(from.name.size() + 1)));
    for (const Mod* module : modules) {
      const std::string_view name = module->name();
      if (name.size() <= best || symbol.size() <= name.size() ||
          !symbol.starts_with(name) || symbol[name.size()] != '.')
        continue;
      std::vector<Fn> candidates =
          module->find_fns(symbol.substr(name.size() + 1));
      std::erase_if(candidates, [](Fn fn) { return fn.local(); });
      consider(name, std::move(candidates));
    }
    return matches;
  }

  std::vector<Fn> matches = local(symbol);
  for (const Mod* module : modules) {
    std::vector<Fn> candidates = module->find_fns(symbol);
    std::erase_if(candidates, [](Fn fn) { return fn.local(); });
    matches.insert(matches.end(), candidates.begin(), candidates.end());
  }
  return matches;
}

bool Env::reaches(std::string_view from, std::string_view target) const {
  std::vector<std::string> pending{std::string(from)};
  std::set<std::string, std::less<>> visited;
  while (!pending.empty()) {
    std::string name = std::move(pending.back());
    pending.pop_back();
    if (name == target)
      return true;
    if (!visited.insert(name).second)
      continue;
    const auto module = impl_->modules.find(name);
    if (module == impl_->modules.end())
      continue;
    const std::vector<std::string> dependencies = module->second->uses();
    pending.insert(pending.end(), dependencies.begin(), dependencies.end());
  }
  return false;
}

std::vector<Fn> Env::resolve_fns(Fn from, std::string_view symbol) const {
  if (!from)
    return {};
  return resolve_fns(*from.store_, symbol);
}

Fn Env::resolve(const Mod& from, std::string_view symbol) const {
  const std::vector<Fn> matches = resolve_fns(from, symbol);
  return matches.size() == 1 ? matches.front() : Fn{};
}

Fn Env::resolve(Fn from, std::string_view symbol) const {
  const std::vector<Fn> matches = resolve_fns(from, symbol);
  return matches.size() == 1 ? matches.front() : Fn{};
}

Fn Env::resolve(const Mod& from, Op call) const {
  if (!call)
    return {};
  const std::vector<Val> values = call.args();
  return resolve(from, call, call.callee(), values);
}

bool Env::accepts(Op call, Fn candidate) const {
  if (!candidate)
    return false;
  const std::array candidates{candidate};
  return static_cast<bool>(match(call, candidates));
}

Fn Env::match(Op call, std::span<const Fn> candidates,
              bool* ambiguous) const {
  return match(call, candidates, ambiguous, nullptr);
}

Fn Env::match(Op call, std::span<const Fn> candidates, bool* ambiguous,
              std::vector<Ty>* generics) const {
  if (generics)
    generics->clear();
  if (ambiguous)
    *ambiguous = false;
  if (!call || call.kind() != Op::Kind::call)
    return {};
  std::vector<Ty> arguments;
  for (const Val value : call.args())
    arguments.push_back(value.type());
  std::vector<Ty> returns;
  for (const Val value : call.outs())
    returns.push_back(value.type());
  const Ty applied{std::string(call.callee())};
  const std::vector<Ty> explicit_arguments =
      applied.args().empty() ? std::vector<Ty>{} : applied.args();
  const std::vector<Val> context = call.blk().fn().generics();
  std::vector<Fn> live;
  for (Fn candidate : candidates)
    if (candidate)
      live.push_back(candidate);
  std::vector<Ty> resolved_returns;
  const Fn result = detail::resolve_overload(
      live, arguments, explicit_arguments, &resolved_returns, ambiguous,
      context, generics, returns);
  bool valid = result && resolved_returns.size() == returns.size();
  for (std::size_t index = 0; valid && index < returns.size(); ++index)
    valid = returns[index].text() == "_" ||
            resolved_returns[index].text() == "_" ||
            returns[index] == resolved_returns[index];
  if (valid)
    return result;
  if (generics)
    generics->clear();
  return {};
}

std::vector<Ty> Env::match(Op call, Fn candidate) const {
  std::vector<Ty> generics;
  const std::array candidates{candidate};
  if (!match(call, candidates, nullptr, &generics))
    generics.clear();
  return generics;
}

bool Env::expand(Mod& mod, Op call, Fn implementation) const {
  const std::array calls{call};
  const std::array implementations{implementation};
  return expand(mod, calls, implementations);
}

bool Env::expand(Mod& mod, std::span<const Op> calls,
                 std::span<const Fn> implementations) const {
  if (calls.empty())
    return false;
  detail::Store backup = mod.impl_->store;
  const auto rollback = [&]() {
    std::vector<Diag> diagnostics = std::move(mod.impl_->store.diags);
    mod.impl_->store = std::move(backup);
    mod.impl_->store.diags = std::move(diagnostics);
    return false;
  };
  if (calls.size() != implementations.size()) {
    detail::add_diag(mod.impl_->store.diags,
                     "expand requires one implementation per call");
    return rollback();
  }

  std::vector<std::string> semantics;
  semantics.reserve(calls.size());
  for (std::size_t index = 0; index < calls.size(); ++index) {
    const Op call = calls[index];
    const Fn implementation = implementations[index];
    if (!call || !implementation)
      return rollback();
    const Fn source = resolve(mod, call);
    semantics.push_back(source ? std::string(source.module()) + "." +
                                     std::string(source.name())
                               : std::string{});
    const std::string implementation_symbol =
        std::string(implementation.module()) + "." +
        std::string(implementation.name());
    const std::vector<Fn> visible = resolve_fns(mod, implementation_symbol);
    if (std::find(visible.begin(), visible.end(), implementation) ==
            visible.end() &&
        !mod.use(*this, std::string(implementation.module())))
      return rollback();
  }

  const auto lexical_target = [&](Fn context, Op op) {
    if (!context || !op || op.kind() != Op::Kind::call)
      return Fn{};
    const Ty applied{std::string(op.callee())};
    if (!applied.valid())
      return Fn{};
    return match(op, resolve_fns(context, applied.name()));
  };
  std::vector<Fn> local_dependencies;
  std::vector<Fn> active_dependencies;
  bool recursive_local_dependency = false;
  const auto collect_locals = [&](const auto& self, Fn context) -> void {
    for (Op op : context.ops()) {
      const Fn target = lexical_target(context, op);
      if (!target || !target.local() || target.module() == mod.name())
        continue;
      if (std::find(active_dependencies.begin(), active_dependencies.end(),
                    target) != active_dependencies.end()) {
        recursive_local_dependency = true;
        continue;
      }
      if (std::find(local_dependencies.begin(), local_dependencies.end(),
                    target) != local_dependencies.end())
        continue;
      local_dependencies.push_back(target);
      active_dependencies.push_back(target);
      self(self, target);
      active_dependencies.pop_back();
    }
  };
  for (Fn implementation : implementations) {
    active_dependencies.push_back(implementation);
    collect_locals(collect_locals, implementation);
    active_dependencies.pop_back();
  }
  if (recursive_local_dependency) {
    detail::add_diag(mod.impl_->store.diags,
                     "expand cannot materialize a recursive local function "
                     "dependency");
    return rollback();
  }

  struct LocalDependency {
    Fn source;
    Fn copy;
  };
  std::vector<LocalDependency> materialized;
  std::map<std::string, std::string, std::less<>> local_names;
  const auto binding = [](std::string_view text) {
    if (text.empty() ||
        (!std::isalpha(static_cast<unsigned char>(text.front())) &&
         text.front() != '_'))
      return false;
    return std::all_of(text.begin() + 1, text.end(), [](char ch) {
      return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
    });
  };
  for (Fn dependency : local_dependencies) {
    const std::string group = std::string(dependency.module()) + "\n" +
                              std::string(dependency.name());
    auto found = local_names.find(group);
    if (found == local_names.end()) {
      std::string name = binding(dependency.name())
                             ? std::string(dependency.name())
                             : std::string("helper");
      if (!mod.find_fns(name).empty()) {
        name = std::string(dependency.module()) + '_' +
               std::string(dependency.name());
        for (char& ch : name)
          if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_')
            ch = '_';
        const std::string base = name;
        for (std::size_t suffix = 1; !mod.find_fns(name).empty(); ++suffix)
          name = base + '_' + std::to_string(suffix);
      }
      found = local_names.emplace(group, std::move(name)).first;
    }
    const Fn copy = mod.clone(*this, dependency, found->second);
    if (!copy)
      return rollback();
    materialized.push_back({dependency, copy});
  }

  const auto mapped_local = [&](Fn source) {
    for (const LocalDependency& dependency : materialized)
      if (dependency.source == source)
        return dependency.copy;
    return Fn{};
  };
  const auto materialized_local = [&](Fn target) {
    for (const LocalDependency& dependency : materialized)
      if (dependency.copy == target)
        return true;
    return false;
  };
  const auto destination_target = [&](Op op) {
    const Ty applied{std::string(op.callee())};
    if (!applied.valid())
      return Fn{};
    return match(op, resolve_fns(mod, applied.name()));
  };
  const auto preserve_calls = [&](Fn source, Fn copy) {
    for (Op op : copy.ops()) {
      const Fn target = lexical_target(source, op);
      if (!target)
        continue;
      const Fn local = mapped_local(target);
      if (local) {
        if (!mod.retarget(*this, op, local))
          return false;
      } else if (!target.local() && target.module() != mod.name() &&
                 destination_target(op) != target &&
                 !mod.retarget(*this, op, target)) {
        return false;
      }
    }
    return true;
  };
  for (const LocalDependency& dependency : materialized)
    if (!preserve_calls(dependency.source, dependency.copy))
      return rollback();

  mod.impl_->store.revision = backup.revision;
  mod.impl_->store.queries.clear();
  for (std::size_t index = 0; index < calls.size(); ++index) {
    const std::vector<Op> before_ops = mod.ops();
    if (!mod.expand(*this, calls[index], implementations[index],
                    semantics[index]))
      return rollback();
    for (Op op : mod.ops()) {
      if (op.kind() != Op::Kind::call ||
          std::find(before_ops.begin(), before_ops.end(), op) !=
              before_ops.end())
        continue;
      const Fn target = lexical_target(implementations[index], op);
      if (!target)
        continue;
      const Fn local = mapped_local(target);
      if (local) {
        if (!mod.retarget(*this, op, local))
          return rollback();
      } else if (!target.local() && target.module() != mod.name() &&
                 destination_target(op) != target &&
                 !mod.retarget(*this, op, target)) {
        return rollback();
      }
    }
  }
  const std::size_t closure_limit = materialized.size() + 1;
  bool closure_complete = materialized.empty();
  for (std::size_t round = 0; round < closure_limit && !closure_complete;
       ++round) {
    std::vector<Op> pending;
    std::vector<Fn> bodies;
    for (Op op : mod.ops()) {
      if (op.kind() != Op::Kind::call)
        continue;
      const Fn target = resolve(mod, op);
      if (!materialized_local(target))
        continue;
      pending.push_back(op);
      bodies.push_back(target);
    }
    closure_complete = pending.empty();
    for (std::size_t index = 0; index < pending.size(); ++index)
      if (!mod.expand(*this, pending[index], bodies[index],
                      bodies[index].name()))
        return rollback();
  }
  if (!closure_complete) {
    detail::add_diag(mod.impl_->store.diags,
                     "expand local function closure did not converge");
    return rollback();
  }
  std::vector<bool> erased(materialized.size(), false);
  std::size_t erased_count = 0;
  while (erased_count != materialized.size()) {
    bool progress = false;
    for (std::size_t index = 0; index < materialized.size(); ++index) {
      if (erased[index])
        continue;
      bool called = false;
      for (Op op : mod.ops())
        if (op.kind() == Op::Kind::call &&
            resolve(mod, op) == materialized[index].copy) {
          called = true;
          break;
        }
      if (called)
        continue;
      if (!mod.erase(*this, materialized[index].copy))
        return rollback();
      erased[index] = true;
      ++erased_count;
      progress = true;
    }
    if (!progress) {
      detail::add_diag(mod.impl_->store.diags,
                       "expand could not release its local function closure");
      return rollback();
    }
  }
  mod.impl_->store.revision = backup.revision + calls.size();
  mod.impl_->store.queries.clear();
  const detail::Dom dom(mod.impl_->store);
  for (std::uint32_t id = 0; id < mod.impl_->store.ops.size(); ++id) {
    const detail::OpData& op = mod.impl_->store.ops[id].data;
    if (!mod.impl_->store.ops[id].live)
      continue;
    for (const std::uint32_t argument : op.args) {
      if (dom.has(argument, id))
        continue;
      detail::add_diag(mod.impl_->store.diags,
                       "expanded body violates value dominance", op.loc);
      return rollback();
    }
  }
  return true;
}

Fn Env::resolve(const Mod& from, Op call, std::string_view callee,
                std::span<const Val> values,
                std::vector<Ty>* resolved_returns) const {
  if (!call || call.kind() != Op::Kind::call)
    return {};
  const Ty applied{std::string(callee)};
  const std::string_view symbol =
      applied.args().empty() ? callee : applied.name();
  const std::vector<Ty> explicit_arguments =
      applied.args().empty() ? std::vector<Ty>{} : applied.args();
  std::vector<Ty> arguments;
  for (const Val value : values)
    arguments.push_back(value.type());
  std::vector<Ty> returns;
  for (const Val value : call.outs())
    returns.push_back(value.type());
  const std::vector<Fn> candidates = resolve_fns(from, symbol);
  const std::vector<Val> context = call.blk().fn().generics();
  return detail::resolve_overload(candidates, arguments, explicit_arguments,
                                  resolved_returns, nullptr, context, nullptr,
                                  returns);
}

bool Env::bound(std::string_view symbol) const noexcept {
  return impl_->natives.contains(symbol);
}

bool Env::call(std::string_view symbol, std::span<const Attr> args,
               std::vector<Attr>& returns) {
  const auto found = impl_->natives.find(symbol);
  if (found == impl_->natives.end()) {
    detail::add_diag(impl_->diags,
                     "native function is not bound: " + std::string(symbol));
    return false;
  }
  const Native& native = found->second;
  std::vector<Ty> argument_types;
  argument_types.reserve(args.size());
  for (const Attr& argument : args)
    argument_types.push_back(scalar_type(argument));
  bool ambiguous = false;
  std::vector<Ty> result_types;
  const Fn declaration = detail::resolve_overload(
      native.declarations, argument_types, {}, &result_types, &ambiguous);
  if (!declaration) {
    detail::add_diag(impl_->diags,
                     ambiguous
                         ? "native call is ambiguous: " + std::string(symbol)
                         : "arguments do not match a native declaration: " +
                               std::string(symbol));
    return false;
  }
  const std::vector<Val> params = declaration.params();
  for (std::size_t index = 0; index < args.size(); ++index) {
    if (!scalar_matches(params[index].type(), args[index])) {
      detail::add_diag(impl_->diags,
                       "argument type does not match native declaration: " +
                           std::string(symbol));
      return false;
    }
  }
  std::vector<Attr> produced(result_types.size());
  CallState state{args,
                  &produced,
                  std::vector<bool>(produced.size(), false),
                  &impl_->diags,
                  std::string(symbol),
                  false};
  joggle_call call{&native_api, &state};
  if (!native.function(&call, native.data) || state.failed)
    return false;
  if (std::find(state.written.begin(), state.written.end(), false) !=
      state.written.end()) {
    detail::add_diag(impl_->diags,
                     "native function did not write every return: " +
                         std::string(symbol));
    return false;
  }
  for (std::size_t index = 0; index < produced.size(); ++index) {
    if (!scalar_matches(result_types[index], produced[index])) {
      detail::add_diag(impl_->diags,
                       "return type does not match native declaration: " +
                           std::string(symbol));
      return false;
    }
  }
  returns = std::move(produced);
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
