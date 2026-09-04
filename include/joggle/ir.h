#pragma once

// Executable intermediate representation. Mod declarations, materialized
// Fns, and their handles share the single public `joggle` namespace.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "joggle/diagnostic.h"
#include "joggle/mod.h"
#include "joggle/type.h"

namespace joggle {

class Compiler;

namespace detail {
struct FnIdentity;
struct FnState;
struct FnEditState;
struct KnownValStorage;
struct FnAccess;
}  // namespace detail

class Fn;
class Blk;
class Val;
class Op;
class Terminator;

class Val {
public:
  bool valid() const;
  bool known() const;
  Type type() const;
  template <typename T> std::optional<T> get() const {
    const auto value = known_value();
    return value ? detail::decode_parameter<T>(*value) : std::nullopt;
  }
  bool is_fn_arg() const;
  bool is_blk_arg() const;
  std::optional<Mod::FnDecl> referenced_fn() const;
  std::optional<Fn> inline_fn() const;
  std::optional<Op> defining_op() const;
  // Direct Op users of this Residual value in Fn order. Known values do
  // not retain one owning Fn and therefore return an empty list.
  std::vector<Op> users() const;
  bool operator==(const Val&) const;

private:
  Val(std::shared_ptr<detail::FnIdentity> fn, std::uint64_t id);
  Val(Type type, detail::ParamVal value);
  std::optional<detail::ParamVal> known_value() const;
  std::shared_ptr<detail::FnIdentity> fn_;
  std::shared_ptr<const detail::KnownValStorage> known_;
  std::uint64_t id_ = 0;

  friend class joggle::Compiler;
  friend class Fn;
  friend class Blk;
  friend class Op;
  friend class Terminator;
  friend struct joggle::detail::FnAccess;
};

class Op {
public:
  bool valid() const;
  Mod::FnDecl callee() const;
  Blk parent() const;
  // Residual inputs are SSA edges. Known inputs are immutable properties.
  // Both are derived from the callee's single `fn` signature; an IR schema
  // never maintains a second operand/attribute declaration.
  std::vector<Val> operands() const;
  std::vector<std::pair<std::string, Val>> properties() const;
  std::optional<Val> operand(std::string_view name) const;
  std::optional<Val> property(std::string_view name) const;
  std::vector<Val> arguments() const;
  std::vector<Val> results() const;
  Val value() const;
  Val result(std::size_t index) const;
  // Optional frontend provenance used for diagnostics, not call semantics.
  std::optional<SourceRange> location() const;

  template <typename T> std::optional<T> property(std::string_view name) const {
    const auto value = property(name);
    return value ? value->get<T>() : std::nullopt;
  }

  bool operator==(const Op&) const = default;

private:
  std::optional<Val> argument(std::string_view name) const;
  Op(std::shared_ptr<detail::FnIdentity> fn, std::uint64_t id);
  std::shared_ptr<detail::FnIdentity> fn_;
  std::uint64_t id_ = 0;

  friend class Val;
  friend class Blk;
  friend class Fn;
  friend class Terminator;
  friend struct joggle::detail::FnAccess;
};

class Terminator {
public:
  enum class Kind { Return, Jump, Branch };

  bool valid() const;
  Kind kind() const;
  std::optional<Val> condition() const;
  std::vector<Val> returned() const;
  std::size_t successor_count() const;
  Blk successor(std::size_t index) const;
  std::vector<Val> arguments(std::size_t successor) const;
  bool operator==(const Terminator&) const = default;

private:
  Terminator(std::shared_ptr<detail::FnIdentity> fn, std::uint64_t block);
  std::shared_ptr<detail::FnIdentity> fn_;
  std::uint64_t block_ = 0;

  friend class Blk;
  friend class Fn;
};

class Blk {
public:
  bool valid() const;
  bool is_entry() const;
  std::vector<Val> arguments() const;
  std::vector<Op> ops() const;
  Terminator terminator() const;
  bool operator==(const Blk&) const = default;

private:
  Blk(std::shared_ptr<detail::FnIdentity> fn, std::uint64_t id);
  std::shared_ptr<detail::FnIdentity> fn_;
  std::uint64_t id_ = 0;

  friend class Fn;
  friend class Op;
  friend class Terminator;
  friend struct joggle::detail::FnAccess;
};

class Fn {
public:
  // Opaque identity of one committed Fn state. Analyses retain a
  // Revision and compare it with revision() before reusing cached data.
  class Revision {
  public:
    bool operator==(const Revision&) const = default;

  private:
    explicit Revision(std::shared_ptr<const detail::FnState> state);
    std::shared_ptr<const detail::FnState> state_;
    friend class Fn;
  };

  class Edit {
  public:
    ~Edit();
    Edit(Edit&&) noexcept;
    Edit& operator=(Edit&&) noexcept;
    Edit(const Edit&) = delete;
    Edit& operator=(const Edit&) = delete;

    Val argument(Type type);
    Val reference(Mod::FnDecl fn, Type type);
    Val callable(Fn fn, Type type);
    Blk blk(std::vector<Type> argument_types = {});
    // Straight-line convenience: append to the entry Blk.
    Op append(Mod::FnDecl schema, std::vector<Val> arguments = {},
              std::vector<Type> result_types = {});
    Op append(Blk block, Mod::FnDecl schema, std::vector<Val> arguments = {},
              std::vector<Type> result_types = {});
    Op insert(Op before, Mod::FnDecl schema, std::vector<Val> arguments = {},
              std::vector<Type> result_types = {});

    void ret(Blk block, std::vector<Val> values = {});
    void jump(Blk block, Blk target, std::vector<Val> arguments = {});
    void branch(Blk block, Val condition, Blk true_target,
                std::vector<Val> true_arguments, Blk false_target,
                std::vector<Val> false_arguments);
    // Attaches diagnostic provenance within this transaction.
    void locate(Op op, SourceRange source);
    void replace(Val from, Val to);
    Op replace(Op op, Mod::FnDecl schema);
    // Replaces every result position and erases the old Op. An empty
    // replacement erases a zero-result Op.
    void replace(Op op, std::vector<Val> results);
    void erase(Op op);

    bool commit(Diagnostics& diagnostics);

  private:
    explicit Edit(std::shared_ptr<detail::FnIdentity> fn);
    Op add(Blk block, std::optional<Op> before, Mod::FnDecl schema,
           std::vector<Val> arguments, std::vector<Type> result_types);
    std::unique_ptr<detail::FnEditState> state_;
    friend class Fn;
    friend struct joggle::detail::FnAccess;
  };

  ~Fn();
  // Fn is a copy-on-write IR value. Copies share a committed Revision;
  // edit() detaches the edited copy before exposing mutable operations.
  Fn(const Fn& other);
  Fn& operator=(const Fn& other);
  Fn(Fn&&) noexcept;
  Fn& operator=(Fn&&) noexcept;

  std::vector<Val> arguments() const;
  std::optional<Mod::FnDecl> declaration() const;
  std::vector<Type> result_types() const;
  Blk entry() const;
  std::vector<Blk> blks() const;
  std::vector<Op> ops() const;
  // Reverse relations over this Fn's existing IR. These queries do not
  // create or own a second graph representation.
  std::vector<Blk> predecessors(Blk block) const;
  std::vector<Op> users(Val value) const;
  bool has_uses(Val value) const;
  bool dominates(Blk dominator, Blk block) const;
  bool dominates(Val definition, Op op) const;
  Revision revision() const;
  Edit edit();

private:
  explicit Fn(std::vector<Mod> mods);
  bool accepts(const Mod::Symbol& symbol) const;
  static Val make_value(std::shared_ptr<detail::FnIdentity> fn,
                        std::uint64_t id);
  static Op make_op(std::shared_ptr<detail::FnIdentity> fn, std::uint64_t id);
  static Blk make_blk(std::shared_ptr<detail::FnIdentity> fn, std::uint64_t id);
  std::shared_ptr<detail::FnIdentity> fn_;

  friend class joggle::Compiler;
  friend class joggle::Mod;
  friend struct joggle::detail::FnAccess;
};

std::string format(const Fn& fn, std::string_view name = "main");

}  // namespace joggle
