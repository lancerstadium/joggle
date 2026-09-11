#ifndef JOGGLE_JOGGLE_H
#define JOGGLE_JOGGLE_H

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

extern "C" {

struct jog_api;
struct jog_module;

enum jog_value_kind : std::uint32_t {
  JOG_NIL,
  JOG_BOOL,
  JOG_I64,
  JOG_F64,
  JOG_STR,
  JOG_HANDLE,
  JOG_BYTES
};

struct jog_slice {
  const char* data;
  std::size_t size;
};

struct jog_value {
  jog_value_kind kind;
  union {
    bool boolean;
    std::int64_t integer;
    double real;
    jog_slice string;
    jog_slice bytes;
    void* handle;
  } data;
};

struct jog_call {
  const jog_api* api;
  void* state;
};

using jog_fn = bool (*)(jog_call* call, void* data);
using jog_module_entry = bool (*)(const jog_api* api, jog_module* module);

struct jog_api {
  std::uint32_t version;
  std::uint32_t size;
  bool (*bind)(jog_module* module, const char* symbol, jog_fn function,
               void* data);
  std::size_t (*arg_count)(const jog_call* call);
  bool (*arg)(const jog_call* call, std::size_t index, jog_value* value);
  bool (*ret)(jog_call* call, std::size_t index, const jog_value* value);
  bool (*fail)(jog_call* call, const char* message);
};
}

#if defined(_WIN32)
#define JOGGLE_MODULE_EXPORT extern "C" __declspec(dllexport)
#else
#define JOGGLE_MODULE_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace joggle {

inline constexpr unsigned version_major = 0;
inline constexpr unsigned version_minor = 1;
inline constexpr unsigned version_patch = 0;
inline constexpr std::uint32_t abi_version = 1;

inline bool compatible(const jog_api* api) noexcept {
  return api && api->version == abi_version && api->size >= sizeof(jog_api);
}

enum class Severity : std::uint8_t { note, warning, error };

struct Loc {
  std::string file;
  std::size_t line = 0;
  std::size_t column = 0;

  friend bool operator==(const Loc&, const Loc&) = default;
};

struct Diag {
  Severity severity = Severity::error;
  std::string message;
  Loc loc;
};

class Attr {
public:
  using Bytes = std::vector<std::uint8_t>;
  using List = std::vector<Attr>;
  using Dict = std::map<std::string, Attr, std::less<>>;
  using Data = std::variant<std::monostate, bool, std::int64_t, double,
                            std::string, Bytes, List, Dict>;

  Attr() = default;
  Attr(bool value);
  Attr(std::int64_t value);
  Attr(double value);
  Attr(std::string value);
  Attr(const char* value);
  Attr(Bytes value);
  Attr(List value);
  Attr(Dict value);

  const Data& data() const noexcept;
  bool empty() const noexcept;
  std::optional<bool> boolean() const noexcept;
  std::optional<std::int64_t> integer() const noexcept;
  std::optional<double> real() const noexcept;
  std::optional<std::string_view> string() const& noexcept;
  std::optional<std::string_view> string() const&& = delete;
  const Bytes* bytes() const& noexcept;
  const Bytes* bytes() const&& = delete;
  const List* list() const& noexcept;
  const List* list() const&& = delete;
  const Dict* dict() const& noexcept;
  const Dict* dict() const&& = delete;

  friend bool operator==(const Attr&, const Attr&) = default;

private:
  Data data_;
};

class Ty {
public:
  Ty() = default;
  explicit Ty(std::string text);

  bool empty() const noexcept;
  bool valid() const noexcept;
  std::string_view text() const noexcept;
  std::string_view name() const noexcept;
  const std::vector<Ty>& args() const noexcept;

  friend bool operator==(const Ty& left, const Ty& right) {
    return left.text_ == right.text_;
  }

private:
  std::string text_;
  std::string name_;
  std::vector<Ty> args_;
  bool valid_ = false;
};

namespace detail {
struct Store;
class Eval;
}  // namespace detail

class Mod;
class Fn;
class Blk;
class Op;
class Val;

class Val {
public:
  Val() = default;

  bool valid() const noexcept;
  explicit operator bool() const noexcept;
  std::string_view name() const noexcept;
  Ty type() const;
  const Attr::Dict& meta() const noexcept;
  const Attr* meta(std::string_view key) const noexcept;
  Op def() const noexcept;
  std::vector<Op> users() const;
  bool is_const() const noexcept;
  Attr constant() const;

  friend bool operator==(const Val&, const Val&) = default;

private:
  detail::Store* store_ = nullptr;
  std::uint32_t id_ = 0;
  std::uint32_t generation_ = 0;

  Val(detail::Store*, std::uint32_t, std::uint32_t) noexcept;
  friend class Mod;
  friend class Fn;
  friend class Blk;
  friend class Op;
  friend class Parser;
};

class Op {
public:
  enum class Kind : std::uint8_t { call, constant, loop, branch, ret, yield };

  Op() = default;

  bool valid() const noexcept;
  explicit operator bool() const noexcept;
  Kind kind() const noexcept;
  std::string_view callee() const noexcept;
  std::vector<Val> args() const;
  std::vector<Val> outs() const;
  std::vector<Blk> blks() const;
  Blk blk() const noexcept;
  const Attr::Dict& meta() const noexcept;
  const Attr* meta(std::string_view key) const noexcept;
  Loc loc() const;

  friend bool operator==(const Op&, const Op&) = default;

private:
  detail::Store* store_ = nullptr;
  std::uint32_t id_ = 0;
  std::uint32_t generation_ = 0;

  Op(detail::Store*, std::uint32_t, std::uint32_t) noexcept;
  friend class Mod;
  friend class Val;
  friend class Blk;
  friend class Parser;
};

class Blk {
public:
  Blk() = default;

  bool valid() const noexcept;
  explicit operator bool() const noexcept;
  std::vector<Val> args() const;
  std::vector<Op> ops() const;
  Fn fn() const noexcept;

  friend bool operator==(const Blk&, const Blk&) = default;

private:
  detail::Store* store_ = nullptr;
  std::uint32_t id_ = 0;
  std::uint32_t generation_ = 0;

  Blk(detail::Store*, std::uint32_t, std::uint32_t) noexcept;
  friend class Mod;
  friend class Fn;
  friend class Op;
  friend class Parser;
};

class Fn {
public:
  Fn() = default;

  bool valid() const noexcept;
  explicit operator bool() const noexcept;
  std::string_view name() const noexcept;
  std::string_view module() const noexcept;
  std::vector<Val> generics() const;
  std::vector<Val> params() const;
  std::vector<Ty> returns() const;
  bool external() const noexcept;
  const Attr::Dict& meta() const noexcept;
  const Attr* meta(std::string_view key) const noexcept;
  Blk body() const noexcept;
  std::vector<Blk> blks() const;
  std::vector<Op> ops() const;
  Loc loc() const;

  friend bool operator==(const Fn&, const Fn&) = default;

private:
  detail::Store* store_ = nullptr;
  std::uint32_t id_ = 0;
  std::uint32_t generation_ = 0;

  Fn(detail::Store*, std::uint32_t, std::uint32_t) noexcept;
  friend class Mod;
  friend class Blk;
  friend class Env;
  friend class Parser;
  friend class detail::Eval;
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
  bool loaded(std::string_view name) const noexcept;
  std::vector<Fn> fns(std::string_view module) const;
  std::vector<Fn> find_fns(std::string_view symbol) const;
  Fn find_fn(std::string_view symbol) const;
  std::vector<Fn> resolve_fns(const Mod& from, std::string_view symbol) const;
  std::vector<Fn> resolve_fns(Fn from, std::string_view symbol) const;
  Fn resolve(const Mod& from, std::string_view symbol) const;
  Fn resolve(Fn from, std::string_view symbol) const;
  Fn resolve(const Mod& from, Op call) const;
  Fn match(Op call, std::span<const Fn> candidates,
           bool* ambiguous = nullptr) const;
  bool accepts(Op call, Fn candidate) const;
  bool expand(Mod& mod, Op call, Fn implementation) const;
  bool bound(std::string_view symbol) const noexcept;
  bool call(std::string_view symbol, std::span<const Attr> args,
            std::vector<Attr>& returns);
  std::vector<std::string> modules() const;
  const std::vector<Diag>& diags() const noexcept;
  void clear_diags() noexcept;
  int print_diags(std::FILE* file) const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;

  void error(std::string message, Loc loc = {});
  bool load_one(std::string_view name);
  std::vector<Fn> resolve_fns(const detail::Store& from,
                              std::string_view symbol) const;
  std::uint64_t cache_id() const noexcept;
  std::uint64_t cache_epoch() const noexcept;
  Fn resolve(const Mod& from, Op call, std::string_view callee,
             std::span<const Val> args,
             std::vector<Ty>* returns = nullptr) const;

  friend class Parser;
  friend class Mod;
  friend bool run(Env&, std::string_view, Mod&);
  friend bool run(Env&, std::string_view, Mod&, Attr&);
  friend bool run(Env&, std::span<const std::string_view>, Mod&, Attr&);
  friend bool query(Env&, std::string_view, const Mod&, Attr&,
                    std::span<const Attr>, bool*);
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
  std::vector<std::string> uses() const;
  bool use(std::string module);
  std::vector<Fn> fns() const;
  std::vector<Op> ops() const;
  std::vector<Fn> find_fns(std::string_view name) const;
  Fn find_fn(std::string_view name) const;
  std::uint64_t revision() const noexcept;

  Op call(Op before, std::string callee, std::span<const Val> args,
          std::span<const Ty> types);
  Val call(Op before, std::string callee, std::span<const Val> args, Ty type);
  Val constant(Op before, Attr value, Ty type);
  Op loop(Op before, std::span<const std::string> names,
          std::span<const Val> sources, std::span<const Val> carried);
  Op branch(Op before, Val condition, std::span<const Val> carried);
  Op clone(Op op, Op before);
  bool expand(Op call, Fn callee);
  bool move(Op op, Op before);
  bool args(const Env& env, Op op, std::span<const Val> values);
  bool fuse(const Env& env, std::span<const Op> ops, std::string callee);
  bool replace(Val old_value, Val new_value);
  bool replace(Val old_value, Val new_value, Op user);
  bool erase(Op op);
  bool type(Val value, Ty type);
  bool rename(Val value, std::string name);
  bool rename(Op call, std::string callee);
  bool retarget(const Env& env, Op call, std::string callee,
                std::span<const Val> args);
  bool set(Fn fn, std::string key, Attr value);
  bool set(Val item, std::string key, Attr value);
  bool set(Op op, std::string key, Attr value);
  bool unset(Fn fn, std::string_view key);
  bool unset(Val item, std::string_view key);
  bool unset(Op op, std::string_view key);
  bool verify(const Env& env);

  bool ok() const noexcept;
  const std::vector<Diag>& diags() const noexcept;
  void clear_diags() noexcept;
  int print_diags(std::FILE* file) const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;

  bool expand(Op call, Fn callee, std::string_view semantic);

  friend class Parser;
  friend class Env;
  friend bool run(Env&, std::string_view, Mod&);
  friend bool run(Env&, std::string_view, Mod&, Attr&);
  friend bool run(Env&, std::span<const std::string_view>, Mod&, Attr&);
  friend bool query(Env&, std::string_view, const Mod&, Attr&,
                    std::span<const Attr>, bool*);
  friend std::string print(const Mod&);
};

bool parse(Env& env, std::string_view source, Mod& out,
           std::string_view file = {});
std::string print(const Mod& mod);
bool print(std::FILE* file, const Mod& mod);
std::string print(const Attr& value);
bool print(std::FILE* file, const Attr& value);
bool structurally_equal(const Mod& left, const Mod& right);
bool run(Env& env, std::string_view function, Mod& mod);
bool run(Env& env, std::string_view function, Mod& mod, Attr& report);
bool run(Env& env, std::span<const std::string_view> functions, Mod& mod);
bool run(Env& env, std::span<const std::string_view> functions, Mod& mod,
         Attr& report);
bool query(Env& env, std::string_view function, const Mod& mod, Attr& result,
           std::span<const Attr> args = {}, bool* cached = nullptr);

}  // namespace joggle

#endif
