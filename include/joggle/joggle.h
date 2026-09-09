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

struct jog_api_v1;
struct jog_module_v1;

enum jog_value_kind_v1 : std::uint32_t {
  JOG_NIL_V1,
  JOG_BOOL_V1,
  JOG_I64_V1,
  JOG_F64_V1,
  JOG_STR_V1,
  JOG_HANDLE_V1
};

struct jog_str_v1 {
  const char* data;
  std::size_t size;
};

struct jog_value_v1 {
  jog_value_kind_v1 kind;
  union {
    bool boolean;
    std::int64_t integer;
    double real;
    jog_str_v1 string;
    void* handle;
  } data;
};

struct jog_call_v1 {
  const jog_api_v1* api;
  void* state;
};

using jog_host_fn_v1 = bool (*)(jog_call_v1* call, void* data);
using jog_module_entry_v1 = bool (*)(const jog_api_v1* api,
                                     jog_module_v1* module);

struct jog_api_v1 {
  std::uint32_t abi_version;
  bool (*bind)(jog_module_v1* module, const char* symbol,
               jog_host_fn_v1 function, void* data);
  std::size_t (*arg_count)(const jog_call_v1* call);
  bool (*arg)(const jog_call_v1* call, std::size_t index, jog_value_v1* value);
  bool (*ret)(jog_call_v1* call, std::size_t index, const jog_value_v1* value);
  bool (*fail)(jog_call_v1* call, const char* message);
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
inline constexpr std::uint32_t module_abi_version = 1;

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
  using List = std::vector<Attr>;
  using Dict = std::map<std::string, Attr, std::less<>>;
  using Data = std::variant<std::monostate, bool, std::int64_t, double,
                            std::string, List, Dict>;

  Attr() = default;
  Attr(bool value);
  Attr(std::int64_t value);
  Attr(double value);
  Attr(std::string value);
  Attr(const char* value);
  Attr(List value);
  Attr(Dict value);

  const Data& data() const noexcept;
  bool empty() const noexcept;
  std::optional<bool> boolean() const noexcept;
  std::optional<std::int64_t> integer() const noexcept;
  std::optional<double> real() const noexcept;
  std::optional<std::string_view> string() const& noexcept;
  std::optional<std::string_view> string() const&& = delete;
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
  std::string_view text() const noexcept;

  friend bool operator==(const Ty&, const Ty&) = default;

private:
  std::string text_;
};

namespace detail {
struct Store;
}

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
  std::vector<Blk> blocks() const;
  Blk block() const noexcept;
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
  std::vector<std::string> generics() const;
  std::vector<Val> params() const;
  std::vector<Ty> returns() const;
  bool external() const noexcept;
  bool host() const noexcept;
  Blk body() const noexcept;
  std::vector<Blk> blocks() const;
  Loc loc() const;

  friend bool operator==(const Fn&, const Fn&) = default;

private:
  detail::Store* store_ = nullptr;
  std::uint32_t id_ = 0;
  std::uint32_t generation_ = 0;

  Fn(detail::Store*, std::uint32_t, std::uint32_t) noexcept;
  friend class Mod;
  friend class Blk;
  friend class Parser;
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

  friend class Parser;
  friend class Mod;
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
  std::vector<Fn> fns() const;
  Fn find_fn(std::string_view name) const noexcept;

  bool replace(Val old_value, Val new_value);
  bool erase(Op op);
  bool rename(Op call, std::string callee);
  bool verify(const Env& env);

  bool ok() const noexcept;
  const std::vector<Diag>& diags() const noexcept;
  void clear_diags() noexcept;
  int print_diags(std::FILE* file) const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;

  friend class Parser;
  friend class Env;
  friend std::string print(const Mod&);
};

bool parse(Env& env, std::string_view source, Mod& out,
           std::string_view file = {});
std::string print(const Mod& mod);
bool print(std::FILE* file, const Mod& mod);
bool structurally_equal(const Mod& left, const Mod& right);

}  // namespace joggle

#endif
