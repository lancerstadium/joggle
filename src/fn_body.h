#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "execution.h"
#include "joggle/diag.h"
#include "joggle/ir.h"

namespace joggle {

class Compiler;
class Diag;

namespace detail {

class Lexer;
struct Token;

struct SyntaxRange {
  SourcePosition begin;
  SourcePosition end;
};

struct ValSyntax {
  enum class Kind { Number, Boolean, String, List, Reference };

  Kind kind = Kind::Number;
  std::string text;
  std::vector<ValSyntax> elements;
  SyntaxRange range;
};

struct LocalUseSyntax {
  std::string name;
  SyntaxRange range;
};

struct ExprSyntax {
  Mod::Expr value;
  SyntaxRange range;
};

struct BindingSyntax {
  std::string name;
  std::optional<ExprSyntax> type;
  SyntaxRange range;
  bool rebind = false;
};

struct StatementSyntax {
  enum class Kind { Expr, If, While, For, Return, Break, Continue };

  Kind kind = Kind::Expr;
  std::vector<BindingSyntax> bindings;
  std::optional<BindingSyntax> iterator;
  ExprSyntax expression;
  std::vector<StatementSyntax> body;
  std::vector<StatementSyntax> otherwise;
  std::vector<ExprSyntax> values;
  bool has_else = false;
  SyntaxRange range;
};

struct BlkArgSyntax {
  std::string name;
  ExprSyntax type;
  SyntaxRange range;
};

struct SuccessorSyntax {
  std::string target;
  std::vector<ExprSyntax> arguments;
  SyntaxRange range;
};

struct TerminatorSyntax {
  enum class Kind { Return, Jump, Branch };

  Kind kind = Kind::Return;
  std::optional<ExprSyntax> condition;
  std::vector<ExprSyntax> values;
  std::vector<SuccessorSyntax> successors;
  SyntaxRange range;
};

struct BlkSyntax {
  // Every fn has exactly one `entry` block. Other names are local to the
  // fn and are referenced only by terminator successors.
  std::string name;
  std::vector<BlkArgSyntax> arguments;
  std::vector<StatementSyntax> statements;
  // Structured source uses only `statements`, including return statements.
  // Explicit low-level Blks carry a terminator.
  std::optional<TerminatorSyntax> terminator;
  SyntaxRange range;
};

struct FnBody {
  std::vector<BlkSyntax> blocks;
  std::string source;
  SyntaxRange range;
};

struct FnArgSyntax {
  std::string name;
  ValSyntax type;
};

// Surface form used when formatting an in-memory Fn. Mod-owned
// fn signatures remain authoritative for named fns.
struct FnSyntax {
  std::string name;
  std::vector<FnArgSyntax> arguments;
  std::vector<ValSyntax> result_types;
  FnBody body;
};

std::optional<FnBody>
parse_fn_body(Lexer& lexer, Token& current, Diag& diagnostics,
              std::string source,
              std::span<const Mod::FnDecl::GenericDecl> variables = {});
bool verify_fn_body(const FnBody& body, Diag& diagnostics);
std::string format_fn_body(const FnBody& body, std::size_t indent = 0);
std::string format_fn_syntax(const FnSyntax& fn, std::size_t indent = 0);
FnSyntax materialized_fn_syntax(const Fn& fn, std::string_view name,
                                bool qualify_types = false);
std::optional<Fn> instantiate_fn(Compiler& compiler, Mod::FnDecl fn,
                                 const FnBody& body, Diag& diagnostics,
                                 std::vector<Val> known_arguments = {},
                                 KnownBindings bindings = {});
std::optional<Fn> instantiate_lambda(
    Compiler& compiler, std::string_view owner, const Mod::Expr& expression,
    const SourceRange& source, Diag& diagnostics,
    const KnownBindings& bindings = {},
    std::optional<std::vector<Type>> expected_inputs = std::nullopt,
    std::optional<std::vector<Type>> expected_results = std::nullopt,
    bool allow_guarded_evaluation = true);

}  // namespace detail
}  // namespace joggle
