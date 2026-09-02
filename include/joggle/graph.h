#pragma once

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
class Graph;
class Value;
class Operation;
class Property;

template <typename T>
Property property(std::string name, T&& value);

namespace detail {
struct GraphIdentity;
struct GraphEditState;
struct GraphAccess;
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
  bool is_argument() const;
  std::optional<Operation> defining_operation() const;
  bool operator==(const Value&) const = default;

private:
  Value(std::shared_ptr<detail::GraphIdentity> graph, std::uint64_t id);
  std::shared_ptr<detail::GraphIdentity> graph_;
  std::uint64_t id_ = 0;

  friend class Graph;
  friend class Operation;
  friend struct detail::GraphAccess;
};

class Operation {
public:
  bool valid() const;
  Module::FunctionDecl schema() const;
  std::vector<Value> operands() const;
  std::vector<Value> results() const;
  Value value() const;
  Value result(std::size_t index) const;

  template <typename T> std::optional<T> get(std::string_view name) const {
    const auto value = property(name);
    return value ? detail::decode_parameter<T>(*value) : std::nullopt;
  }

  bool operator==(const Operation&) const = default;

private:
  std::optional<detail::ParameterValue> property(std::string_view name) const;
  Operation(std::shared_ptr<detail::GraphIdentity> graph, std::uint64_t id);
  std::shared_ptr<detail::GraphIdentity> graph_;
  std::uint64_t id_ = 0;

  friend class Value;
  friend class Graph;
  friend struct detail::GraphAccess;
};

class Graph {
public:
  class Edit {
  public:
    ~Edit();
    Edit(Edit&&) noexcept;
    Edit& operator=(Edit&&) noexcept;
    Edit(const Edit&) = delete;
    Edit& operator=(const Edit&) = delete;

    Value argument(Type type);
    Operation append(Module::FunctionDecl schema,
                     std::vector<Value> operands = {},
                     std::vector<Type> result_types = {});
    template <typename... Rest>
    Operation append(Module::FunctionDecl schema, std::vector<Value> operands,
                     Property first, Rest&&... rest) {
      return append_with_properties(
          std::move(schema), std::move(operands), {},
          collect_properties(std::move(first), std::forward<Rest>(rest)...));
    }
    template <typename... Rest>
    Operation append(Module::FunctionDecl schema, std::vector<Value> operands,
                     std::vector<Type> result_types, Property first,
                     Rest&&... rest) {
      return append_with_properties(
          std::move(schema), std::move(operands), std::move(result_types),
          collect_properties(std::move(first), std::forward<Rest>(rest)...));
    }
    Operation insert(Operation before, Module::FunctionDecl schema,
                     std::vector<Value> operands = {},
                     std::vector<Type> result_types = {});
    template <typename... Rest>
    Operation insert(Operation before, Module::FunctionDecl schema,
                     std::vector<Value> operands, Property first,
                     Rest&&... rest) {
      return insert_with_properties(
          std::move(before), std::move(schema), std::move(operands), {},
          collect_properties(std::move(first), std::forward<Rest>(rest)...));
    }
    template <typename... Rest>
    Operation insert(Operation before, Module::FunctionDecl schema,
                     std::vector<Value> operands,
                     std::vector<Type> result_types, Property first,
                     Rest&&... rest) {
      return insert_with_properties(
          std::move(before), std::move(schema), std::move(operands),
          std::move(result_types),
          collect_properties(std::move(first), std::forward<Rest>(rest)...));
    }
    template <typename T>
    void set(Operation operation, std::string name, T&& value) {
      set_value(operation, std::move(name),
                detail::encode_parameter(std::forward<T>(value)));
    }

    void output(Value value);
    void replace(Value from, Value to);
    Operation replace(Operation operation, Module::FunctionDecl schema);
    void erase(Operation operation);

    bool commit(Diagnostics& diagnostics);

  private:
    explicit Edit(std::shared_ptr<detail::GraphIdentity> graph);
    template <typename... Rest>
    static std::vector<Property> collect_properties(Property first,
                                                     Rest&&... rest) {
      static_assert(
          (std::is_same_v<std::remove_cvref_t<Rest>, Property> && ...),
          "operation construction accepts only values made by property()");
      return {std::move(first), std::forward<Rest>(rest)...};
    }
    Operation append_with_properties(
        Module::FunctionDecl schema, std::vector<Value> operands,
        std::vector<Type> result_types,
        std::vector<Property> properties);
    Operation insert_with_properties(
        Operation before, Module::FunctionDecl schema,
        std::vector<Value> operands, std::vector<Type> result_types,
        std::vector<Property> properties);
    Operation add(std::optional<Operation> before,
                  Module::FunctionDecl schema, std::vector<Value> operands,
                  std::vector<Type> result_types,
                  std::vector<Property> properties);
    void set_value(Operation operation, std::string name,
                   detail::ParameterValue value);
    std::unique_ptr<detail::GraphEditState> state_;
    friend class Graph;
    friend struct detail::GraphAccess;
  };

  ~Graph();
  Graph(Graph&&) noexcept;
  Graph& operator=(Graph&&) noexcept;
  Graph(const Graph&) = delete;
  Graph& operator=(const Graph&) = delete;

  std::vector<Value> inputs() const;
  std::vector<Operation> operations() const;
  std::vector<Operation> all_operations() const;
  std::vector<Value> outputs() const;
  Edit edit();

private:
  struct Snapshot;
  explicit Graph(std::vector<Module> modules);
  bool accepts(const Module::Symbol& symbol) const;
  std::shared_ptr<const Snapshot> snapshot() const;
  void restore(std::shared_ptr<const Snapshot> snapshot);
  static Value make_value(std::shared_ptr<detail::GraphIdentity> graph,
                          std::uint64_t id);
  static Operation make_operation(std::shared_ptr<detail::GraphIdentity> graph,
                                  std::uint64_t id);
  std::shared_ptr<detail::GraphIdentity> graph_;

  friend class Compiler;
  friend struct detail::GraphAccess;
};

std::string format(const Graph& graph, std::string_view name = "main");

}  // namespace joggle
