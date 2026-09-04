#pragma once

// Executable intermediate representation. Module declarations, materialized
// Functions, and their handles share the single public `joggle` namespace.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "joggle/diagnostic.h"
#include "joggle/module.h"
#include "joggle/type.h"

namespace joggle {

class Compiler;

namespace detail {
struct FunctionIdentity;
struct FunctionState;
struct FunctionEditState;
struct KnownValueStorage;
struct FunctionAccess;
}  // namespace detail

class Function;
class Block;
class Value;
class Op;
class Terminator;

class Value {
public:
  bool valid() const;
  bool known() const;
  Type type() const;
  template <typename T> std::optional<T> get() const {
    const auto value = known_value();
    return value ? detail::decode_parameter<T>(*value) : std::nullopt;
  }
  bool is_function_argument() const;
  bool is_block_argument() const;
  std::optional<Module::FunctionDecl> referenced_function() const;
  std::optional<Function> inline_function() const;
  std::optional<Op> defining_op() const;
  // Direct Op users of this Residual value in Function order. Known values do
  // not retain one owning Function and therefore return an empty list.
  std::vector<Op> users() const;
  bool operator==(const Value&) const;

private:
  Value(std::shared_ptr<detail::FunctionIdentity> function, std::uint64_t id);
  Value(Type type, detail::ParameterValue value);
  std::optional<detail::ParameterValue> known_value() const;
  std::shared_ptr<detail::FunctionIdentity> function_;
  std::shared_ptr<const detail::KnownValueStorage> known_;
  std::uint64_t id_ = 0;

  friend class joggle::Compiler;
  friend class Function;
  friend class Block;
  friend class Op;
  friend class Terminator;
  friend struct joggle::detail::FunctionAccess;
};

class Op {
public:
  bool valid() const;
  Module::FunctionDecl callee() const;
  Block parent() const;
  // Residual inputs are SSA edges. Known inputs are immutable properties.
  // Both are derived from the callee's single `fn` signature; an IR schema
  // never maintains a second operand/attribute declaration.
  std::vector<Value> operands() const;
  std::vector<std::pair<std::string, Value>> properties() const;
  std::optional<Value> operand(std::string_view name) const;
  std::optional<Value> property(std::string_view name) const;
  std::vector<Value> arguments() const;
  std::vector<Value> results() const;
  Value value() const;
  Value result(std::size_t index) const;
  // Optional frontend provenance used for diagnostics, not call semantics.
  std::optional<SourceRange> location() const;

  template <typename T>
  std::optional<T> property(std::string_view name) const {
    const auto value = property(name);
    return value ? value->get<T>() : std::nullopt;
  }

  bool operator==(const Op&) const = default;

private:
  std::optional<Value> argument(std::string_view name) const;
  Op(std::shared_ptr<detail::FunctionIdentity> function, std::uint64_t id);
  std::shared_ptr<detail::FunctionIdentity> function_;
  std::uint64_t id_ = 0;

  friend class Value;
  friend class Block;
  friend class Function;
  friend class Terminator;
  friend struct joggle::detail::FunctionAccess;
};

class Terminator {
public:
  enum class Kind { Return, Jump, Branch };

  bool valid() const;
  Kind kind() const;
  std::optional<Value> condition() const;
  std::vector<Value> returned() const;
  std::size_t successor_count() const;
  Block successor(std::size_t index) const;
  std::vector<Value> arguments(std::size_t successor) const;
  bool operator==(const Terminator&) const = default;

private:
  Terminator(std::shared_ptr<detail::FunctionIdentity> function,
             std::uint64_t block);
  std::shared_ptr<detail::FunctionIdentity> function_;
  std::uint64_t block_ = 0;

  friend class Block;
  friend class Function;
};

class Block {
public:
  bool valid() const;
  bool is_entry() const;
  std::vector<Value> arguments() const;
  std::vector<Op> ops() const;
  Terminator terminator() const;
  bool operator==(const Block&) const = default;

private:
  Block(std::shared_ptr<detail::FunctionIdentity> function, std::uint64_t id);
  std::shared_ptr<detail::FunctionIdentity> function_;
  std::uint64_t id_ = 0;

  friend class Function;
  friend class Op;
  friend class Terminator;
  friend struct joggle::detail::FunctionAccess;
};

class Function {
public:
  // Opaque identity of one committed Function state. Analyses retain a
  // Revision and compare it with revision() before reusing cached data.
  class Revision {
  public:
    bool operator==(const Revision&) const = default;

  private:
    explicit Revision(std::shared_ptr<const detail::FunctionState> state);
    std::shared_ptr<const detail::FunctionState> state_;
    friend class Function;
  };

  class Edit {
  public:
    ~Edit();
    Edit(Edit&&) noexcept;
    Edit& operator=(Edit&&) noexcept;
    Edit(const Edit&) = delete;
    Edit& operator=(const Edit&) = delete;

    Value argument(Type type);
    Value reference(Module::FunctionDecl function, Type type);
    Value callable(Function function, Type type);
    Block block(std::vector<Type> argument_types = {});
    // Straight-line convenience: append to the entry Block.
    Op append(Module::FunctionDecl schema, std::vector<Value> arguments = {},
              std::vector<Type> result_types = {});
    Op append(Block block, Module::FunctionDecl schema,
              std::vector<Value> arguments = {},
              std::vector<Type> result_types = {});
    Op insert(Op before, Module::FunctionDecl schema,
              std::vector<Value> arguments = {},
              std::vector<Type> result_types = {});

    void ret(Block block, std::vector<Value> values = {});
    void jump(Block block, Block target, std::vector<Value> arguments = {});
    void branch(Block block, Value condition, Block true_target,
                std::vector<Value> true_arguments, Block false_target,
                std::vector<Value> false_arguments);
    // Attaches diagnostic provenance within this transaction.
    void locate(Op op, SourceRange source);
    void replace(Value from, Value to);
    Op replace(Op op, Module::FunctionDecl schema);
    // Replaces every result position and erases the old Op. An empty
    // replacement erases a zero-result Op.
    void replace(Op op, std::vector<Value> results);
    void erase(Op op);

    bool commit(Diagnostics& diagnostics);

  private:
    explicit Edit(std::shared_ptr<detail::FunctionIdentity> function);
    Op add(Block block, std::optional<Op> before,
                    Module::FunctionDecl schema, std::vector<Value> arguments,
                    std::vector<Type> result_types);
    std::unique_ptr<detail::FunctionEditState> state_;
    friend class Function;
    friend struct joggle::detail::FunctionAccess;
  };

  ~Function();
  // Function is a copy-on-write IR value. Copies share a committed Revision;
  // edit() detaches the edited copy before exposing mutable operations.
  Function(const Function& other);
  Function& operator=(const Function& other);
  Function(Function&&) noexcept;
  Function& operator=(Function&&) noexcept;

  std::vector<Value> arguments() const;
  std::optional<Module::FunctionDecl> declaration() const;
  std::vector<Type> result_types() const;
  Block entry() const;
  std::vector<Block> blocks() const;
  std::vector<Op> ops() const;
  // Reverse relations over this Function's existing IR. These queries do not
  // create or own a second graph representation.
  std::vector<Block> predecessors(Block block) const;
  std::vector<Op> users(Value value) const;
  bool has_uses(Value value) const;
  bool dominates(Block dominator, Block block) const;
  bool dominates(Value definition, Op op) const;
  Revision revision() const;
  Edit edit();

private:
  explicit Function(std::vector<Module> modules);
  bool accepts(const Module::Symbol& symbol) const;
  static Value make_value(std::shared_ptr<detail::FunctionIdentity> function,
                          std::uint64_t id);
  static Op make_op(std::shared_ptr<detail::FunctionIdentity> function,
                    std::uint64_t id);
  static Block make_block(std::shared_ptr<detail::FunctionIdentity> function,
                          std::uint64_t id);
  std::shared_ptr<detail::FunctionIdentity> function_;

  friend class joggle::Compiler;
  friend class joggle::Module;
  friend struct joggle::detail::FunctionAccess;
};

std::string format(const Function& function, std::string_view name = "main");

}  // namespace joggle
