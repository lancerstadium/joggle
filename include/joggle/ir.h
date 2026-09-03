#pragma once

// Executable intermediate representation. Module declarations remain in
// `joggle`; all executable ownership and handle types live in `joggle::ir`.

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

namespace ir {

class Function;
class Block;
class Value;
class Instruction;
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
  std::optional<Module::Function> referenced_function() const;
  std::optional<Instruction> defining_instruction() const;
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
  friend class Instruction;
  friend class Terminator;
  friend struct joggle::detail::FunctionAccess;
};

class Instruction {
public:
  bool valid() const;
  Module::Function callee() const;
  Block parent() const;
  std::vector<Value> arguments() const;
  std::vector<Value> results() const;
  Value value() const;
  Value result(std::size_t index) const;

  template <typename T> std::optional<T> get(std::string_view name) const {
    const auto value = argument(name);
    return value ? value->get<T>() : std::nullopt;
  }

  bool operator==(const Instruction&) const = default;

private:
  std::optional<Value> argument(std::string_view name) const;
  Instruction(std::shared_ptr<detail::FunctionIdentity> function,
              std::uint64_t id);
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
  std::vector<Instruction> instructions() const;
  Terminator terminator() const;
  bool operator==(const Block&) const = default;

private:
  Block(std::shared_ptr<detail::FunctionIdentity> function, std::uint64_t id);
  std::shared_ptr<detail::FunctionIdentity> function_;
  std::uint64_t id_ = 0;

  friend class Function;
  friend class Instruction;
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
    Value reference(Module::Function function, Type type);
    Block block(std::vector<Type> argument_types = {});
    // Straight-line convenience: append to the entry Block.
    Instruction append(Module::Function schema,
                       std::vector<Value> arguments = {},
                       std::vector<Type> result_types = {});
    Instruction append(Block block, Module::Function schema,
                       std::vector<Value> arguments = {},
                       std::vector<Type> result_types = {});
    Instruction insert(Instruction before, Module::Function schema,
                       std::vector<Value> arguments = {},
                       std::vector<Type> result_types = {});

    void ret(Block block, std::vector<Value> values = {});
    void jump(Block block, Block target, std::vector<Value> arguments = {});
    void branch(Block block, Value condition, Block true_target,
                std::vector<Value> true_arguments, Block false_target,
                std::vector<Value> false_arguments);
    void replace(Value from, Value to);
    Instruction replace(Instruction instruction, Module::Function schema);
    // Replaces every result position and erases the old Instruction. An empty
    // replacement erases a zero-result Instruction.
    void replace(Instruction instruction, std::vector<Value> results);
    void erase(Instruction instruction);

    bool commit(Diagnostics& diagnostics);

  private:
    explicit Edit(std::shared_ptr<detail::FunctionIdentity> function);
    Instruction add(Block block, std::optional<Instruction> before,
                    Module::Function schema, std::vector<Value> arguments,
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
  std::optional<Module::Function> declaration() const;
  std::vector<Type> result_types() const;
  Block entry() const;
  std::vector<Block> blocks() const;
  std::vector<Instruction> instructions() const;
  // Reverse relations over this Function's existing IR. These queries do not
  // create or own a second graph representation.
  std::vector<Block> predecessors(Block block) const;
  std::vector<Instruction> users(Value value) const;
  bool has_uses(Value value) const;
  bool dominates(Block dominator, Block block) const;
  bool dominates(Value definition, Instruction instruction) const;
  Revision revision() const;
  Function clone() const;
  Edit edit();

private:
  struct Snapshot;
  explicit Function(std::vector<Module> modules);
  Function fork() const;
  bool accepts(const Module::Symbol& symbol) const;
  std::shared_ptr<const Snapshot> snapshot() const;
  void restore(std::shared_ptr<const Snapshot> snapshot);
  static Value make_value(std::shared_ptr<detail::FunctionIdentity> function,
                          std::uint64_t id);
  static Instruction
  make_instruction(std::shared_ptr<detail::FunctionIdentity> function,
                   std::uint64_t id);
  static Block make_block(std::shared_ptr<detail::FunctionIdentity> function,
                          std::uint64_t id);
  std::shared_ptr<detail::FunctionIdentity> function_;

  friend class joggle::Compiler;
  friend class joggle::Module;
  friend struct joggle::detail::FunctionAccess;
};

}  // namespace ir

std::string format(const ir::Function& function,
                   std::string_view name = "main");

}  // namespace joggle
