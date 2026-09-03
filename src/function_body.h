#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "execution.h"
#include "joggle/diagnostic.h"
#include "joggle/ir.h"

namespace joggle {

class Compiler;
class Diagnostics;

namespace detail {

class Lexer;
struct Token;

struct SyntaxRange {
  SourcePosition begin;
  SourcePosition end;
};

struct ValueSyntax {
  enum class Kind { Number, Boolean, String, List, Reference };

  Kind kind = Kind::Number;
  std::string text;
  std::vector<ValueSyntax> elements;
  SyntaxRange range;
};

struct LocalUseSyntax {
  std::string name;
  SyntaxRange range;
};

struct ExpressionSyntax {
  Module::Expression value;
  SyntaxRange range;
};

struct BindingSyntax {
  std::string name;
  std::optional<ExpressionSyntax> type;
  SyntaxRange range;
  bool rebind = false;
};

struct StatementSyntax {
  enum class Kind { Expression, If, While, For, Return, Break, Continue };

  Kind kind = Kind::Expression;
  std::vector<BindingSyntax> bindings;
  std::optional<BindingSyntax> iterator;
  ExpressionSyntax expression;
  std::vector<StatementSyntax> body;
  std::vector<StatementSyntax> otherwise;
  std::vector<ExpressionSyntax> values;
  bool has_else = false;
  SyntaxRange range;
};

struct BlockArgumentSyntax {
  std::string name;
  ExpressionSyntax type;
  SyntaxRange range;
};

struct SuccessorSyntax {
  std::string target;
  std::vector<ExpressionSyntax> arguments;
  SyntaxRange range;
};

struct TerminatorSyntax {
  enum class Kind { Return, Jump, Branch };

  Kind kind = Kind::Return;
  std::optional<ExpressionSyntax> condition;
  std::vector<ExpressionSyntax> values;
  std::vector<SuccessorSyntax> successors;
  SyntaxRange range;
};

struct BlockSyntax {
  // Every function has exactly one `entry` block. Other names are local to the
  // function and are referenced only by terminator successors.
  std::string name;
  std::vector<BlockArgumentSyntax> arguments;
  std::vector<StatementSyntax> statements;
  // Structured source uses only `statements`, including return statements.
  // Explicit low-level Blocks carry a terminator.
  std::optional<TerminatorSyntax> terminator;
  SyntaxRange range;
};

struct FunctionBody {
  std::vector<BlockSyntax> blocks;
  std::string source;
  SyntaxRange range;
};

struct FunctionArgumentSyntax {
  std::string name;
  ValueSyntax type;
};

// Surface form used when formatting an in-memory Function. Module-owned
// function signatures remain authoritative for named functions.
struct FunctionSyntax {
  std::string name;
  std::vector<FunctionArgumentSyntax> arguments;
  std::vector<ValueSyntax> result_types;
  FunctionBody body;
};

std::optional<FunctionBody> parse_function_body(
    Lexer& lexer, Token& current, Diagnostics& diagnostics, std::string source,
    std::span<const Module::FunctionDecl::GenericDecl> variables = {});
bool verify_function_body(const FunctionBody& body, Diagnostics& diagnostics);
std::string format_function_body(const FunctionBody& body,
                                 std::size_t indent = 0);
std::string format_function_syntax(const FunctionSyntax& function,
                                   std::size_t indent = 0);
FunctionSyntax materialized_function_syntax(const Function& function,
                                            std::string_view name);
std::optional<Function>
instantiate_function(Compiler& compiler, Module::FunctionDecl function,
                     const FunctionBody& body, Diagnostics& diagnostics,
                     std::vector<Value> known_arguments = {},
                     KnownBindings bindings = {});

}  // namespace detail
}  // namespace joggle
