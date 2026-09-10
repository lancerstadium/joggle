#include "detail.h"

#include <algorithm>
#include <array>
#include <atomic>
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

struct jog_module {
  void* state;
};

namespace joggle {

namespace {

std::uint64_t next_env_id() {
  static std::atomic<std::uint64_t> next{1};
  return next.fetch_add(1, std::memory_order_relaxed);
}

struct Native {
  jog_fn function = nullptr;
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

jog_module_entry entry(void* handle) {
#if defined(_WIN32)
  return reinterpret_cast<jog_module_entry>(
      GetProcAddress(static_cast<HMODULE>(handle), "joggle_module"));
#else
  return reinterpret_cast<jog_module_entry>(dlsym(handle, "joggle_module"));
#endif
}

bool bind_native(jog_module* opaque, const char* symbol, jog_fn function,
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

std::size_t call_arg_count(const jog_call* call) {
  if (!call || !call->state)
    return 0;
  return static_cast<const CallState*>(call->state)->args.size();
}

bool encode(const Attr& input, jog_value& output) {
  if (input.empty()) {
    output.kind = JOG_NIL;
    output.data.handle = nullptr;
  } else if (const auto value = input.boolean()) {
    output.kind = JOG_BOOL;
    output.data.boolean = *value;
  } else if (const auto value = input.integer()) {
    output.kind = JOG_I64;
    output.data.integer = *value;
  } else if (const auto value = input.real()) {
    output.kind = JOG_F64;
    output.data.real = *value;
  } else if (const auto value = input.string()) {
    output.kind = JOG_STR;
    output.data.string = {value->data(), value->size()};
  } else if (const auto* value = input.bytes()) {
    output.kind = JOG_BYTES;
    output.data.bytes = {reinterpret_cast<const char*>(value->data()),
                         value->size()};
  } else
    return false;
  return true;
}

bool decode(const jog_value& input, Attr& output) {
  switch (input.kind) {
  case JOG_NIL:
    output = Attr{};
    return true;
  case JOG_BOOL:
    output = Attr(input.data.boolean);
    return true;
  case JOG_I64:
    output = Attr(input.data.integer);
    return true;
  case JOG_F64:
    output = Attr(input.data.real);
    return true;
  case JOG_STR:
    if (!input.data.string.data && input.data.string.size)
      return false;
    output = Attr(std::string(input.data.string.data, input.data.string.size));
    return true;
  case JOG_HANDLE:
    return false;
  case JOG_BYTES:
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

bool call_arg(const jog_call* call, std::size_t index, jog_value* value) {
  if (!call || !call->state || !value)
    return false;
  const auto& state = *static_cast<const CallState*>(call->state);
  return index < state.args.size() && encode(state.args[index], *value);
}

bool call_ret(jog_call* call, std::size_t index, const jog_value* value) {
  if (!call || !call->state || !value)
    return false;
  auto& state = *static_cast<CallState*>(call->state);
  if (index >= state.returns->size() ||
      !decode(*value, (*state.returns)[index]))
    return false;
  state.written[index] = true;
  return true;
}

bool call_fail(jog_call* call, const char* message) {
  if (!call || !call->state || !message)
    return false;
  auto& state = *static_cast<CallState*>(call->state);
  detail::add_diag(*state.diags,
                   "native function '" + state.symbol + "': " + message);
  state.failed = true;
  return false;
}

const jog_api native_api{abi_version,    sizeof(jog_api), bind_native,
                         call_arg_count, call_arg,        call_ret,
                         call_fail};

bool scalar_matches(const Ty& type, const Attr& value) {
  const std::string_view name = type.text();
  if (name == "bool")
    return value.boolean().has_value();
  if (name == "f16" || name == "f32" || name == "f64")
    return value.real().has_value();
  if (name == "str")
    return value.string().has_value();
  if (name == "bytes")
    return value.bytes() != nullptr;
  if (name == "int" || name == "index" ||
      (name.size() > 1 && (name.front() == 'i' || name.front() == 'u')))
    return value.integer().has_value();
  return true;
}

Ty scalar_type(const Attr& value) {
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
    const jog_module_entry init = entry(handle);
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
    jog_module opaque{&state};
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
  ++impl_->epoch;
  return true;
}

bool Env::loaded(std::string_view name) const noexcept {
  return impl_->modules.contains(name);
}

std::vector<Fn> Env::fns(std::string_view module) const {
  const auto found = impl_->modules.find(module);
  return found == impl_->modules.end() ? std::vector<Fn>{}
                                       : found->second->fns();
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

std::vector<Fn> Env::resolve_fns(const Mod& from,
                                 std::string_view symbol) const {
  const std::string own_prefix = std::string(from.name()) + ".";
  if (symbol.starts_with(own_prefix))
    return from.find_fns(symbol.substr(own_prefix.size()));

  std::vector<std::string> pending = from.uses();
  std::set<std::string, std::less<>> visited;
  while (!pending.empty()) {
    std::string name = std::move(pending.back());
    pending.pop_back();
    if (!visited.insert(name).second)
      continue;
    const auto dependency = impl_->modules.find(name);
    if (dependency == impl_->modules.end())
      continue;
    const auto next = dependency->second->uses();
    pending.insert(pending.end(), next.begin(), next.end());
  }

  if (symbol.find('.') != std::string_view::npos) {
    std::vector<Fn> matches = find_fns(symbol);
    if (!matches.empty() &&
        visited.contains(std::string(matches.front().module())))
      return matches;
    return {};
  }

  std::vector<Fn> matches = from.find_fns(symbol);
  for (const std::string& name : visited) {
    const auto module = impl_->modules.find(name);
    if (module == impl_->modules.end())
      continue;
    std::vector<Fn> candidates = module->second->find_fns(symbol);
    matches.insert(matches.end(), candidates.begin(), candidates.end());
  }
  return matches;
}

std::vector<Fn> Env::resolve_fns(Fn from, std::string_view symbol) const {
  if (!from)
    return {};
  const auto module = impl_->modules.find(std::string(from.module()));
  return module == impl_->modules.end() ? std::vector<Fn>{}
                                        : resolve_fns(*module->second, symbol);
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
  const std::vector<Val> context = call.blk().fn().generics();
  std::vector<Fn> live;
  for (Fn candidate : candidates)
    if (candidate)
      live.push_back(candidate);
  return detail::resolve_overload(live, arguments, {}, nullptr,
                                  ambiguous, context, nullptr, returns);
}

bool Env::expand(Mod& mod, Op call, Fn implementation) const {
  if (!call || !implementation)
    return false;
  std::string semantic;
  const Fn source = resolve(mod, call);
  if (source)
    semantic = std::string(source.module()) + "." +
               std::string(source.name());

  detail::Store backup = mod.impl_->store;
  const std::string implementation_symbol =
      std::string(implementation.module()) + "." +
      std::string(implementation.name());
  const std::vector<Fn> visible = resolve_fns(mod, implementation_symbol);
  if (std::find(visible.begin(), visible.end(), implementation) ==
      visible.end())
    mod.use(std::string(implementation.module()));
  if (mod.expand(call, implementation, semantic))
    return true;

  std::vector<Diag> diagnostics = std::move(mod.impl_->store.diags);
  mod.impl_->store = std::move(backup);
  mod.impl_->store.diags = std::move(diagnostics);
  return false;
}

Fn Env::resolve(const Mod& from, Op call, std::string_view callee,
                std::span<const Val> values) const {
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
                                  nullptr, nullptr, context, nullptr, returns);
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
  returns.assign(result_types.size(), Attr{});
  CallState state{args,
                  &returns,
                  std::vector<bool>(returns.size(), false),
                  &impl_->diags,
                  std::string(symbol),
                  false};
  jog_call call{&native_api, &state};
  if (!native.function(&call, native.data) || state.failed)
    return false;
  if (std::find(state.written.begin(), state.written.end(), false) !=
      state.written.end()) {
    detail::add_diag(impl_->diags,
                     "native function did not write every return: " +
                         std::string(symbol));
    return false;
  }
  for (std::size_t index = 0; index < returns.size(); ++index) {
    if (!scalar_matches(result_types[index], returns[index])) {
      detail::add_diag(impl_->diags,
                       "return type does not match native declaration: " +
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
