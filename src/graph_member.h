#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "joggle/diagnostic.h"
#include "joggle/graph.h"

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

struct TermDefinition {
  enum class Kind { Variable, Operation };
  Kind kind = Kind::Variable;
  std::string name;
  std::vector<std::size_t> arguments;
};

struct RuleDefinition {
  std::vector<TermDefinition> terms;
  std::size_t match = 0;
  std::size_t replacement = 0;
  std::optional<SourceRange> source;
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
  std::optional<ValueSyntax> type;
  SyntaxRange range;
};

struct StatementSyntax {
  std::vector<BindingSyntax> bindings;
  ExpressionSyntax expression;
  SyntaxRange range;
};

struct BlockArgumentSyntax {
  std::string name;
  ValueSyntax type;
  SyntaxRange range;
};

struct SuccessorSyntax {
  std::string target;
  std::vector<ExpressionSyntax> arguments;
  SyntaxRange range;
};

struct TerminatorSyntax {
  enum class Kind { Return, Branch, CondBranch };

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
  std::vector<StatementSyntax> instructions;
  TerminatorSyntax terminator;
  SyntaxRange range;
};

struct FunctionBody {
  std::vector<BlockSyntax> blocks;
  std::vector<RuleDefinition> rules;
  std::string source;
  SyntaxRange range;
};

struct FunctionArgumentSyntax {
  std::string name;
  ValueSyntax type;
};

// Transient surface form used only when formatting the transitional Graph
// API. Module-owned function signatures remain authoritative.
struct FunctionSyntax {
  std::string name;
  std::vector<FunctionArgumentSyntax> arguments;
  std::vector<ValueSyntax> result_types;
  FunctionBody body;
};

std::optional<FunctionBody> parse_function_body(Lexer& lexer, Token& current,
                                                Diagnostics& diagnostics,
                                                std::string source,
    std::span<const Module::FunctionDecl::GenericDecl> variables = {});
bool verify_function_body(const FunctionBody& body, Diagnostics& diagnostics);
std::string format_function_body(const FunctionBody& body,
                                 std::size_t indent = 0);
std::string format_function_syntax(const FunctionSyntax& function,
                                   std::size_t indent = 0);
std::optional<Graph> instantiate_function(Compiler& compiler,
                                          Module::FunctionDecl function,
                                          const FunctionBody& body,
                                          Diagnostics& diagnostics);

}  // namespace detail
}  // namespace joggle
