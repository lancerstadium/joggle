#pragma once

// Function-owned intermediate representation.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "joggle/diagnostic.h"
#include "joggle/module.h"
#include "joggle/type.h"

namespace joggle {

class Compiler;
class Function;
class Block;
class Value;
class Instruction;
class Terminator;
class Property;

template <typename T>
Property property(std::string name, T&& value);

namespace detail {
struct FunctionIdentity;
struct FunctionEditState;
struct FunctionAccess;
struct PropertyAccess;
}  // namespace detail

class Property {
public:
  Property(const Property&) = default;
  Property(Property&&) noexcept = default;
  Property& operator=(const Property&) = default;
  Property& operator=(Property&&) noexcept = default;

  std::string_view name() const { return name_; }

private:
  Property(std::string name, detail::ParameterValue value)
      : name_(std::move(name)), value_(std::move(value)) {}

  std::string name_;
  detail::ParameterValue value_;

  template <typename T>
  friend Property property(std::string name, T&& value);
  friend struct detail::PropertyAccess;
};

template <typename T>
Property property(std::string name, T&& value) {
  return Property(std::move(name),
                  detail::encode_parameter(std::forward<T>(value)));
}

class Value {
public:
  bool valid() const;
  Type type() const;
  bool is_function_argument() const;
  bool is_block_argument() const;
  std::optional<Instruction> defining_instruction() const;
  bool operator==(const Value&) const = default;

private:
  Value(std::shared_ptr<detail::FunctionIdentity> function, std::uint64_t id);
  std::shared_ptr<detail::FunctionIdentity> function_;
  std::uint64_t id_ = 0;

  friend class Function;
  friend class Block;
  friend class Instruction;
  friend class Terminator;
  friend struct detail::FunctionAccess;
};

class Instruction {
public:
  bool valid() const;
  Module::FunctionDecl callee() const;
  Block parent() const;
  std::vector<Value> operands() const;
  std::vector<Value> results() const;
  Value value() const;
  Value result(std::size_t index) const;

  template <typename T> std::optional<T> get(std::string_view name) const {
    const auto value = property(name);
    return value ? detail::decode_parameter<T>(*value) : std::nullopt;
  }

  bool operator==(const Instruction&) const = default;

private:
  std::optional<detail::ParameterValue> property(std::string_view name) const;
  Instruction(std::shared_ptr<detail::FunctionIdentity> function, std::uint64_t id);
  std::shared_ptr<detail::FunctionIdentity> function_;
  std::uint64_t id_ = 0;

  friend class Value;
  friend class Block;
  friend class Function;
  friend class Terminator;
  friend struct detail::FunctionAccess;
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
  friend struct detail::FunctionAccess;
};

class Function {
public:
  class Edit {
  public:
    ~Edit();
    Edit(Edit&&) noexcept;
    Edit& operator=(Edit&&) noexcept;
    Edit(const Edit&) = delete;
    Edit& operator=(const Edit&) = delete;

    Value argument(Type type);
    Block block(std::vector<Type> argument_types = {});
    // Straight-line convenience: append to the entry Block.
    Instruction append(Module::FunctionDecl schema,
                       std::vector<Value> operands = {},
                       std::vector<Type> result_types = {});
    template <typename... Rest>
    Instruction append(Module::FunctionDecl schema,
                       std::vector<Value> operands, Property first,
                       Rest&&... rest) {
      return append_with_properties(
          std::move(schema), std::move(operands), {},
          collect_properties(std::move(first), std::forward<Rest>(rest)...));
    }
    template <typename... Rest>
    Instruction append(Module::FunctionDecl schema,
                       std::vector<Value> operands,
                       std::vector<Type> result_types, Property first,
                       Rest&&... rest) {
      return append_with_properties(
          std::move(schema), std::move(operands), std::move(result_types),
          collect_properties(std::move(first), std::forward<Rest>(rest)...));
    }
    Instruction append(Block block, Module::FunctionDecl schema,
                     std::vector<Value> operands = {},
                     std::vector<Type> result_types = {});
    template <typename... Rest>
    Instruction append(Block block, Module::FunctionDecl schema,
                       std::vector<Value> operands, Property first,
                       Rest&&... rest) {
      return append_with_properties(
          std::move(block), std::move(schema), std::move(operands), {},
          collect_properties(std::move(first), std::forward<Rest>(rest)...));
    }
    template <typename... Rest>
    Instruction append(Block block, Module::FunctionDecl schema,
                       std::vector<Value> operands,
                       std::vector<Type> result_types, Property first,
                       Rest&&... rest) {
      return append_with_properties(
          std::move(block), std::move(schema), std::move(operands),
          std::move(result_types),
          collect_properties(std::move(first), std::forward<Rest>(rest)...));
    }
    Instruction insert(Instruction before, Module::FunctionDecl schema,
                     std::vector<Value> operands = {},
                     std::vector<Type> result_types = {});
    template <typename... Rest>
    Instruction insert(Instruction before, Module::FunctionDecl schema,
                     std::vector<Value> operands, Property first,
                     Rest&&... rest) {
      return insert_with_properties(
          std::move(before), std::move(schema), std::move(operands), {},
          collect_properties(std::move(first), std::forward<Rest>(rest)...));
    }
    template <typename... Rest>
    Instruction insert(Instruction before, Module::FunctionDecl schema,
                     std::vector<Value> operands,
                     std::vector<Type> result_types, Property first,
                     Rest&&... rest) {
      return insert_with_properties(
          std::move(before), std::move(schema), std::move(operands),
          std::move(result_types),
          collect_properties(std::move(first), std::forward<Rest>(rest)...));
    }
    template <typename T>
    void set(Instruction instruction, std::string name, T&& value) {
      set_value(instruction, std::move(name),
                detail::encode_parameter(std::forward<T>(value)));
    }

    void ret(Block block, std::vector<Value> values = {});
    void jump(Block block, Block target, std::vector<Value> arguments = {});
    void branch(Block block, Value condition, Block true_target,
                std::vector<Value> true_arguments, Block false_target,
                std::vector<Value> false_arguments);
    void replace(Value from, Value to);
    Instruction replace(Instruction instruction, Module::FunctionDecl schema);
    void erase(Instruction instruction);

    bool commit(Diagnostics& diagnostics);

  private:
    explicit Edit(std::shared_ptr<detail::FunctionIdentity> function);
    template <typename... Rest>
    static std::vector<Property> collect_properties(Property first,
                                                     Rest&&... rest) {
      static_assert(
          (std::is_same_v<std::remove_cvref_t<Rest>, Property> && ...),
          "instruction construction accepts only values made by property()");
      return {std::move(first), std::forward<Rest>(rest)...};
    }
    Instruction append_with_properties(
        Module::FunctionDecl schema, std::vector<Value> operands,
        std::vector<Type> result_types, std::vector<Property> properties);
    Instruction append_with_properties(
        Block block, Module::FunctionDecl schema, std::vector<Value> operands,
        std::vector<Type> result_types,
        std::vector<Property> properties);
    Instruction insert_with_properties(
        Instruction before, Module::FunctionDecl schema,
        std::vector<Value> operands, std::vector<Type> result_types,
        std::vector<Property> properties);
    Instruction add(Block block, std::optional<Instruction> before,
                  Module::FunctionDecl schema, std::vector<Value> operands,
                  std::vector<Type> result_types,
                  std::vector<Property> properties);
    void set_value(Instruction instruction, std::string name,
                   detail::ParameterValue value);
    std::unique_ptr<detail::FunctionEditState> state_;
    friend class Function;
    friend struct detail::FunctionAccess;
  };

  ~Function();
  Function(Function&&) noexcept;
  Function& operator=(Function&&) noexcept;
  Function(const Function&) = delete;
  Function& operator=(const Function&) = delete;

  std::vector<Value> arguments() const;
  std::optional<Module::FunctionDecl> declaration() const;
  std::vector<Type> result_types() const;
  Block entry() const;
  std::vector<Block> blocks() const;
  std::vector<Instruction> instructions() const;
  Edit edit();

private:
  struct Snapshot;
  explicit Function(std::vector<Module> modules);
  bool accepts(const Module::Symbol& symbol) const;
  std::shared_ptr<const Snapshot> snapshot() const;
  void restore(std::shared_ptr<const Snapshot> snapshot);
  static Value make_value(std::shared_ptr<detail::FunctionIdentity> function,
                          std::uint64_t id);
  static Instruction make_instruction(std::shared_ptr<detail::FunctionIdentity> function,
                                  std::uint64_t id);
  static Block make_block(std::shared_ptr<detail::FunctionIdentity> function,
                          std::uint64_t id);
  std::shared_ptr<detail::FunctionIdentity> function_;

  friend class Compiler;
  friend struct detail::FunctionAccess;
};

std::string format(const Function& function, std::string_view name = "main");

}  // namespace joggle
