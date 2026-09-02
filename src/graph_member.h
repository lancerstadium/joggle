#pragma once

#include <optional>
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

struct ValueSyntax {
  enum class Kind { Number, Boolean, String, List, Reference };

  Kind kind = Kind::Number;
  std::string text;
  std::vector<ValueSyntax> elements;
  SyntaxRange range;
};

struct GraphArgumentSyntax {
  std::string name;
  ValueSyntax type;
  SyntaxRange range;
};

struct GraphPropertySyntax {
  std::string name;
  ValueSyntax value;
  SyntaxRange range;
};

struct GraphUseSyntax {
  std::string name;
  SyntaxRange range;
};

struct GraphRegionSyntax;

struct GraphOperationSyntax {
  std::vector<std::string> results;
  std::vector<std::optional<ValueSyntax>> result_types;
  std::string operation;
  std::vector<GraphUseSyntax> operands;
  std::vector<GraphPropertySyntax> properties;
  std::vector<GraphRegionSyntax> regions;
  SyntaxRange range;
};

struct GraphRegionSyntax {
  std::string name;
  std::vector<GraphArgumentSyntax> arguments;
  std::vector<GraphOperationSyntax> operations;
};

struct GraphSyntax {
  std::string name;
  std::vector<GraphArgumentSyntax> arguments;
  std::vector<ValueSyntax> result_types;
  std::vector<GraphOperationSyntax> operations;
  std::vector<GraphUseSyntax> returns;
  std::string source;
  SyntaxRange range;
};

std::optional<GraphSyntax> parse_graph_syntax(Lexer& lexer, Token& current,
                                              Diagnostics& diagnostics,
                                              std::string source);
std::string format_graph_syntax(const GraphSyntax& graph,
                                std::size_t indent = 0);
std::optional<Graph> instantiate_graph(Compiler& compiler,
                                       const GraphSyntax& syntax,
                                       std::string_view owner,
                                       Diagnostics& diagnostics);

}  // namespace detail
}  // namespace joggle
