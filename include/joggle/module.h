#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "joggle/diagnostic.h"

namespace joggle {

using Bytes = std::vector<std::byte>;

namespace detail {
struct ModuleAccess;
struct FunctionTypeAccess;
}  // namespace detail

class Function;

class Compiler;

struct Version {
  std::uint32_t major = 0;
  std::uint32_t minor = 0;
  std::uint32_t patch = 0;

  auto operator<=>(const Version&) const = default;
};

enum class VersionRangeKind { Exact, Major, Minor, Caret };

struct VersionRange {
  VersionRangeKind kind = VersionRangeKind::Exact;
  Version base;

  bool contains(Version candidate) const;
  auto operator<=>(const VersionRange&) const = default;
};

class Module {
  struct Storage;

public:
  // Creates an empty, named IR Module. Parsing uses the same Module type and
  // additionally populates its declarations.
  Module(std::string name, Version version);
  Module(const Module& other);
  Module& operator=(const Module& other);
  Module(Module&&) noexcept = default;
  Module& operator=(Module&&) noexcept = default;

  // Immutable declaration expression. The parser, formatter, linker, and type
  // solver all use this representation; function signatures do not keep a
  // second private spelling of their types.
  struct Expression {
    enum class Kind {
      Number,
      Boolean,
      String,
      List,
      Reference,
      Variable,
      Call,
      If,
      FunctionType,
      Lambda,
      Evaluate,
      Prefix,
      Infix,
      Postfix,
    };

    Kind kind = Kind::Number;
    std::string text;
    std::vector<Expression> arguments;
    // Empty call entries are positional. Lambda labels name parameters whose
    // type expressions occupy the corresponding argument positions; the last
    // lambda argument is its body.
    std::vector<std::string> labels;

    Expression() = default;
    Expression(Kind kind, std::string text, std::vector<Expression> arguments,
               std::vector<std::string> labels = {})
        : kind(kind), text(std::move(text)), arguments(std::move(arguments)),
          labels(std::move(labels)) {}

    static Expression reference(std::string name) {
      return {Kind::Reference, std::move(name), {}};
    }

    static Expression list_domain(Expression element) {
      return {Kind::Reference, "list", {std::move(element)}};
    }

    bool operator==(const Expression&) const = default;
  };

  struct ParameterDecl {
    std::string name;
    Expression domain = Expression::reference("int");
    bool variadic = false;
    std::optional<Expression> default_value;

    bool operator==(const ParameterDecl&) const = default;
  };

  enum class SymbolKind {
    Type,
    Function,
  };

  class Symbol {
  public:
    std::string_view module_name() const { return module_name_; }
    Version module_version() const { return module_version_; }
    // Declaration provenance of the Module snapshot that produced this Symbol.
    // Logical Symbol equality uses its versioned qualified declaration name.
    std::string_view declaration_digest() const { return declaration_digest_; }
    SymbolKind kind() const { return kind_; }
    std::string_view local_name() const { return local_name_; }

    std::string qualified_name() const;
    std::string stable_name() const;
    bool operator==(const Symbol& other) const;

  private:
    Symbol(std::string module_name, Version module_version,
           std::string declaration_digest, SymbolKind kind,
           std::string local_name, std::string discriminator = {});

    std::string module_name_;
    Version module_version_;
    std::string declaration_digest_;
    SymbolKind kind_ = SymbolKind::Type;
    std::string local_name_;
    std::string discriminator_;

    friend class Module;
    friend class TypeDecl;
    friend class FunctionDecl;
  };

  class TypeDecl {
  public:
    struct DerivedParameterDecl {
      std::string name;
      Expression domain = Expression::reference("int");
      Expression value;

      bool operator==(const DerivedParameterDecl&) const = default;
    };

    std::string_view name() const;
    std::span<const ParameterDecl> parameters() const;
    std::span<const DerivedParameterDecl> derived_parameters() const;
    Symbol symbol() const;
    bool operator==(const TypeDecl& other) const;

  private:
    TypeDecl(std::shared_ptr<const Storage> storage, std::size_t index);
    std::shared_ptr<const Storage> storage_;
    std::size_t index_ = 0;
    friend class Module;
  };

  class FunctionDecl {
  public:
    // A function is either declared by the environment or defined by one
    // body. Known evaluation and residualization are execution outcomes, not
    // declaration kinds.
    enum class Form { External, Body };
    enum class Fixity { Prefix, Infix, Postfix };

    struct GenericDecl {
      std::string name;
      Expression domain = Expression::reference("type");

      bool operator==(const GenericDecl&) const = default;
    };

    std::string_view name() const;
    std::span<const GenericDecl> generics() const;
    std::span<const ParameterDecl> inputs() const;
    std::span<const ParameterDecl> results() const;
    std::optional<Fixity> operator_fixity() const;
    Form form() const;
    const Function* body() const;
    std::string signature() const;
    Symbol symbol() const;
    bool operator==(const FunctionDecl& other) const;

  private:
    FunctionDecl(std::shared_ptr<const Storage> storage, std::size_t index);
    std::shared_ptr<const Storage> storage_;
    std::size_t index_ = 0;
    friend class Module;
    friend struct detail::ModuleAccess;
    friend struct detail::FunctionTypeAccess;
  };

  struct Import {
    std::string name;
    VersionRange version;
    std::string alias;

    std::string_view prefix() const {
      return alias.empty() ? std::string_view(name) : std::string_view(alias);
    }
  };

  struct Dependency {
    std::string name;
    Version version;

    bool operator==(const Dependency&) const = default;
  };

  std::string_view name() const;
  Version version() const;
  // SHA-256 of the current canonical Module. Committed edits to a materialized
  // body change this artifact identity.
  std::string_view digest() const;
  // SHA-256 of imports and declarations with Function bodies erased. Symbols
  // retain it as provenance and Compiler boundaries use it for exact declaration
  // compatibility. Logical Symbol names remain stable when members are added.
  std::string_view declaration_digest() const;
  std::span<const Import> imports() const;

  // Large immutable payloads (for example model weights) travel with the
  // Module without becoming textual declarations or separate pass results.
  // store() content-addresses and deduplicates bytes; copies share payloads.
  std::string store(Bytes bytes);
  std::optional<std::span<const std::byte>> data(std::string_view name) const;
  std::vector<std::string> data() const;

  std::optional<TypeDecl> type(std::string_view name) const;
  std::optional<FunctionDecl> function(std::string_view name) const;
  std::vector<FunctionDecl> overloads(std::string_view name) const;
  std::optional<Symbol> symbol(SymbolKind kind, std::string_view name) const;
  std::vector<Symbol> members() const;
  std::vector<TypeDecl> types() const;
  std::vector<FunctionDecl> functions() const;

  // A FunctionDecl always occupies one Module member and has one canonical
  // signature. insert() adds a declaration whose body is already materialized
  // as editable IR and fixes that Function's argument and result contract;
  // subsequent body edits may change the CFG but not the member signature.
  // body() is absent for external and not-yet-materialized declarations.
  // Mutable body lookup uses the complete declaration rather than an
  // overload-ambiguous name and detaches only that body.
  bool insert(std::string name, Function function, Diagnostics& diagnostics);
  Function* body(FunctionDecl declaration);
  std::vector<Dependency> dependencies() const;
  bool operator==(const Module& other) const;

private:
  explicit Module(std::shared_ptr<const Storage> storage);
  static std::string_view
  current_digest(const std::shared_ptr<const Storage>& storage);
  static std::string
  compute_digest(const std::shared_ptr<const Storage>& storage);
  static std::string
  compute_declaration_digest(const std::shared_ptr<const Storage>& storage);
  static Module
  declaration_view(const std::shared_ptr<const Storage>& storage);
  std::shared_ptr<const Storage> storage_;

  friend class Compiler;
  friend struct detail::ModuleAccess;
  friend std::optional<Module> parse_module(std::string_view, Diagnostics&,
                                            std::string);
  friend std::string format(const Module&);
};

std::optional<Module> parse_module(std::string_view text,
                                   Diagnostics& diagnostics,
                                   std::string source = "<memory>");
std::string format(const Module& module);
std::string to_string(Version version);
std::string to_string(VersionRange range);

}  // namespace joggle
