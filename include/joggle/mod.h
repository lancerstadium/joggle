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

#include "joggle/diag.h"

namespace joggle {

using Bytes = std::vector<std::byte>;

namespace detail {
struct ModAccess;
struct FnTypeAccess;
}  // namespace detail

class Fn;

class Compiler;

struct Version {
  std::uint32_t major = 0;
  std::uint32_t minor = 0;
  std::uint32_t patch = 0;

  auto operator<=>(const Version&) const = default;
};

struct VersionRange {
  enum class Kind { Exact, Major, Minor, Caret };

  Kind kind = Kind::Exact;
  Version base;

  bool contains(Version candidate) const;
  auto operator<=>(const VersionRange&) const = default;
};

class Mod {
  struct Storage;

public:
  // Creates an empty, named IR Mod. Parsing uses the same Mod type and
  // additionally populates its declarations.
  Mod(std::string name, Version version);
  Mod(const Mod& other);
  Mod& operator=(const Mod& other);
  Mod(Mod&&) noexcept = default;
  Mod& operator=(Mod&&) noexcept = default;

  // Immutable declaration expression. The parser, formatter, linker, and type
  // solver all use this representation; fn signatures do not keep a
  // second private spelling of their types.
  struct Expr {
    enum class Kind {
      Infer,
      Number,
      Boolean,
      String,
      List,
      Reference,
      Variable,
      Call,
      If,
      FnType,
      Lambda,
      Block,
      Evaluate,
      Prefix,
      Infix,
      Postfix,
    };

    Kind kind = Kind::Number;
    std::string text;
    std::vector<Expr> arguments;
    // Empty call entries are positional. Lambda labels name parameters whose
    // type expressions occupy the corresponding argument positions. Kind::Infer
    // requests the callable context for an omitted parameter annotation. A
    // A lambda has either [inputs..., body] or [inputs..., result type, body].
    // An expression body is stored directly. A statement body is one
    // canonical braced source fragment in a final Kind::Block expression;
    // it is parsed by the ordinary Fn body parser when the lambda is
    // materialized, so lambdas do not introduce a second statement AST.
    std::vector<std::string> labels;

    Expr() = default;
    Expr(Kind kind, std::string text, std::vector<Expr> arguments,
         std::vector<std::string> labels = {})
        : kind(kind), text(std::move(text)), arguments(std::move(arguments)),
          labels(std::move(labels)) {}

    static Expr reference(std::string name) {
      return {Kind::Reference, std::move(name), {}};
    }

    static Expr list_domain(Expr element) {
      return {Kind::Reference, "list", {std::move(element)}};
    }

    bool operator==(const Expr&) const = default;
  };

  struct ParamDecl {
    std::string name;
    Expr domain = Expr::reference("int");
    bool variadic = false;
    std::optional<Expr> default_value;

    bool operator==(const ParamDecl&) const = default;
  };

  class Symbol {
  public:
    enum class Kind { Type, Fn };

    std::string_view mod_name() const { return mod_name_; }
    Version mod_version() const { return mod_version_; }
    // Declaration provenance of the Mod snapshot that produced this Symbol.
    // Logical Symbol equality uses its versioned qualified declaration name.
    std::string_view declaration_digest() const { return declaration_digest_; }
    Kind kind() const { return kind_; }
    std::string_view local_name() const { return local_name_; }

    std::string qualified_name() const;
    std::string stable_name() const;
    bool operator==(const Symbol& other) const;

  private:
    Symbol(std::string mod_name, Version mod_version,
           std::string declaration_digest, Kind kind, std::string local_name,
           std::string discriminator = {});

    std::string mod_name_;
    Version mod_version_;
    std::string declaration_digest_;
    Kind kind_ = Kind::Type;
    std::string local_name_;
    std::string discriminator_;

    friend class Mod;
    friend class TypeDecl;
    friend class FnDecl;
  };

  class TypeDecl {
  public:
    struct DerivedParamDecl {
      std::string name;
      Expr domain = Expr::reference("int");
      Expr value;

      bool operator==(const DerivedParamDecl&) const = default;
    };

    std::string_view name() const;
    std::span<const ParamDecl> parameters() const;
    std::span<const DerivedParamDecl> derived_parameters() const;
    Symbol symbol() const;
    bool operator==(const TypeDecl& other) const;

  private:
    TypeDecl(std::shared_ptr<const Storage> storage, std::size_t index);
    std::shared_ptr<const Storage> storage_;
    std::size_t index_ = 0;
    friend class Mod;
  };

  class FnDecl {
  public:
    // A fn is either declared by the environment or defined by one
    // body. Known evaluation and residualization are execution outcomes, not
    // declaration kinds.
    enum class Form { External, Body };
    enum class Fixity { Prefix, Infix, Postfix };

    struct GenericDecl {
      std::string name;
      Expr domain = Expr::reference("type");

      bool operator==(const GenericDecl&) const = default;
    };

    std::string_view name() const;
    std::span<const GenericDecl> generics() const;
    std::span<const ParamDecl> inputs() const;
    std::span<const ParamDecl> results() const;
    std::optional<Fixity> operator_fixity() const;
    Form form() const;
    const Fn* body() const;
    std::string signature() const;
    Symbol symbol() const;
    bool operator==(const FnDecl& other) const;

  private:
    FnDecl(std::shared_ptr<const Storage> storage, std::size_t index);
    std::shared_ptr<const Storage> storage_;
    std::size_t index_ = 0;
    friend class Mod;
    friend struct detail::ModAccess;
    friend struct detail::FnTypeAccess;
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
  // SHA-256 of the current canonical Mod. Committed edits to a materialized
  // body change this artifact identity.
  std::string_view digest() const;
  // SHA-256 of imports and declarations with Fn bodies erased. Symbols
  // retain it as provenance and Compiler boundaries use it for exact
  // declaration compatibility. Logical Symbol names remain stable when members
  // are added.
  std::string_view declaration_digest() const;
  std::span<const Import> imports() const;

  // Large immutable payloads (for example model weights) travel with the
  // Mod without becoming textual declarations or separate pass results.
  // store() content-addresses and deduplicates bytes; copies share payloads.
  std::string store(Bytes bytes);
  std::optional<std::span<const std::byte>> data(std::string_view name) const;
  std::vector<std::string> data() const;

  std::optional<TypeDecl> type(std::string_view name) const;
  std::optional<FnDecl> fn(std::string_view name) const;
  std::vector<FnDecl> overloads(std::string_view name) const;
  std::optional<Symbol> symbol(Symbol::Kind kind, std::string_view name) const;
  std::vector<Symbol> members() const;
  std::vector<TypeDecl> types() const;
  std::vector<FnDecl> fns() const;

  // A FnDecl always occupies one Mod member and has one canonical
  // signature. insert() adds a declaration whose body is already materialized
  // as editable IR and fixes that Fn's argument and result contract;
  // subsequent body edits may change the CFG but not the member signature.
  // body() is absent for external and not-yet-materialized declarations.
  // Mutable body lookup uses the complete declaration rather than an
  // overload-ambiguous name and detaches only that body.
  bool insert(std::string name, Fn fn, Diag& diagnostics);
  Fn* body(FnDecl declaration);
  std::vector<Dependency> dependencies() const;
  bool operator==(const Mod& other) const;

private:
  explicit Mod(std::shared_ptr<const Storage> storage);
  static std::string_view
  current_digest(const std::shared_ptr<const Storage>& storage);
  static std::string
  compute_digest(const std::shared_ptr<const Storage>& storage);
  static std::string
  compute_declaration_digest(const std::shared_ptr<const Storage>& storage);
  static Mod declaration_view(const std::shared_ptr<const Storage>& storage);
  std::shared_ptr<const Storage> storage_;

  friend class Compiler;
  friend struct detail::ModAccess;
  friend std::optional<Mod> parse_mod(std::string_view, Diag&, std::string);
  friend std::string format(const Mod&);
};

std::optional<Mod> parse_mod(std::string_view text, Diag& diagnostics,
                             std::string source = "<memory>");
std::string format(const Mod& mod);
std::string to_string(Version version);
std::string to_string(VersionRange range);

}  // namespace joggle
