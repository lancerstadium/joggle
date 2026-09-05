#include "joggle/mod.h"

#include "ir/mod.h"
#include "joggle/ir.h"
#include "lang/expr.h"
#include "lang/fn.h"
#include "lang/prelude.h"

#include <algorithm>
#include <cstddef>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace joggle {
namespace {

using Parameter = Mod::ParamDecl;

std::string type_expression_text(const detail::TypeExpr& expression,
                                 int parent_precedence = 0,
                                 bool right_operand = false);

std::string parameter_text(const Parameter& parameter) {
  std::string result =
      parameter.name + ": " + type_expression_text(parameter.domain);
  if (parameter.variadic) {
    result += "...";
  }
  if (parameter.default_value) {
    result += " = " + type_expression_text(*parameter.default_value);
  }
  return result;
}

std::string parameter_text(const Parameter& parameter,
                           const detail::TypeExpr& annotation) {
  Parameter displayed = parameter;
  displayed.domain = annotation;
  return parameter_text(displayed);
}

int operator_precedence(std::string_view symbol) {
  if (symbol == "[]") {
    return 80;
  }
  if (symbol.empty()) {
    return 0;
  }
  switch (symbol.front()) {
  case '|':
    return 10;
  case '^':
    return 20;
  case '&':
    return 30;
  case '=':
  case '!':
  case '<':
  case '>':
    return 40;
  case '+':
  case '-':
    return 50;
  case '*':
  case '/':
  case '%':
    return 60;
  default:
    return 45;
  }
}

int type_expression_precedence(const detail::TypeExpr& expression) {
  using Kind = detail::TypeExpr::Kind;
  switch (expression.kind) {
  case Kind::Infer:
    return 100;
  case Kind::FnType:
    return 1;
  case Kind::Lambda:
    return 2;
  case Kind::If:
    return 5;
  case Kind::Infix:
    return operator_precedence(expression.text);
  case Kind::Prefix:
    return 70;
  case Kind::Postfix:
    return 80;
  case Kind::Evaluate:
    return 90;
  case Kind::Number:
  case Kind::Boolean:
  case Kind::String:
  case Kind::List:
  case Kind::Reference:
  case Kind::Variable:
  case Kind::Call:
  case Kind::Block:
    return 100;
  }
  return 0;
}

std::string type_expression_text(const detail::TypeExpr& expression,
                                 int parent_precedence, bool right_operand) {
  return detail::format_expression(expression, parent_precedence,
                                   right_operand);
}

constexpr std::size_t canonical_line_width = 88U;

std::string indentation(std::size_t width) { return std::string(width, ' '); }

void write_indented(std::ostringstream& output, std::string_view source) {
  std::size_t begin = 0;
  while (begin < source.size()) {
    const std::size_t end = source.find('\n', begin);
    const std::string_view line = source.substr(
        begin,
        end == std::string_view::npos ? source.size() - begin : end - begin);
    if (line.find_first_not_of(" \t\r") != std::string_view::npos) {
      output << "  " << line;
    }
    output << '\n';
    if (end == std::string_view::npos) {
      break;
    }
    begin = end + 1U;
  }
}

std::string type_expression_layout(const detail::TypeExpr& expression,
                                   std::size_t column,
                                   int parent_precedence = 0,
                                   bool right_operand = false) {
  const std::string flat =
      type_expression_text(expression, parent_precedence, right_operand);
  if (column + flat.size() <= canonical_line_width) {
    return flat;
  }

  using Kind = detail::TypeExpr::Kind;
  const int precedence = type_expression_precedence(expression);
  const bool parenthesized =
      precedence < parent_precedence ||
      (right_operand && precedence == parent_precedence && precedence < 100);
  const std::size_t content_column = column + (parenthesized ? 1U : 0U);
  std::string result = parenthesized ? "(" : "";

  if (expression.kind == Kind::If && expression.arguments.size() == 3U) {
    result += "if ";
    result +=
        type_expression_layout(expression.arguments[0], content_column + 3U);
    result += " {\n";
    result += indentation(content_column + 2U);
    result +=
        type_expression_layout(expression.arguments[1], content_column + 2U);
    result += "\n" + indentation(content_column) + "} else {\n";
    result += indentation(content_column + 2U);
    result +=
        type_expression_layout(expression.arguments[2], content_column + 2U);
    result += "\n" + indentation(content_column) + "}";
  } else {
    const bool delimited =
        expression.kind == Kind::List || expression.kind == Kind::Reference ||
        expression.kind == Kind::Call || expression.kind == Kind::Evaluate;
    if (delimited && !expression.arguments.empty()) {
      const std::string opening =
          expression.kind == Kind::List       ? "["
          : expression.kind == Kind::Evaluate ? "@("
          : expression.kind == Kind::Call
              ? expression.text + "("
              : std::string(detail::display_type_name(expression.text)) + "<";
      const char closing =
          expression.kind == Kind::List ? ']'
          : expression.kind == Kind::Call || expression.kind == Kind::Evaluate
              ? ')'
              : '>';
      result += opening + '\n';
      const std::size_t argument_column = content_column + 2U;
      for (std::size_t index = 0; index < expression.arguments.size();
           ++index) {
        result += indentation(argument_column);
        const bool labeled = expression.kind == Kind::Call &&
                             index < expression.labels.size() &&
                             !expression.labels[index].empty();
        if (labeled) {
          result += expression.labels[index] + ": ";
        }
        result += type_expression_layout(
            expression.arguments[index],
            argument_column +
                (labeled ? expression.labels[index].size() + 2U : 0U));
        if (index + 1U != expression.arguments.size()) {
          result += ',';
        }
        result += '\n';
      }
      result += indentation(content_column);
      result.push_back(closing);
    } else if (expression.kind == Kind::Prefix) {
      result += expression.text;
      result += type_expression_layout(expression.arguments.front(),
                                       content_column + expression.text.size(),
                                       precedence, true);
    } else if (expression.kind == Kind::Postfix) {
      result += type_expression_layout(expression.arguments.front(),
                                       content_column, precedence, false);
      result += expression.text;
    } else if (expression.kind == Kind::Infix) {
      if (expression.text == "[]" && expression.arguments.size() >= 2U) {
        result += type_expression_layout(expression.arguments[0],
                                         content_column, precedence, false);
        result += '[';
        for (std::size_t index = 1U; index < expression.arguments.size();
             ++index) {
          if (index != 1U) {
            result += ", ";
          }
          result +=
              type_expression_layout(expression.arguments[index],
                                     content_column + result.size(), 0, false);
        }
        result += ']';
      } else {
        result += type_expression_layout(expression.arguments[0],
                                         content_column, precedence, false);
        result += " " + expression.text;
        result += '\n';
        const std::size_t rhs_column = content_column + 2U;
        result += indentation(rhs_column);
        result += type_expression_layout(expression.arguments[1], rhs_column,
                                         precedence, true);
      }
    } else {
      return flat;
    }
  }

  if (parenthesized) {
    result += ')';
  }
  return result;
}

}  // namespace

std::string format(const Mod& mod) {
  std::ostringstream output;
  output << "joggle 1;\n\nmod " << mod.name() << '@' << to_string(mod.version())
         << " {\n";
  std::vector<Mod::Import> imports(mod.imports().begin(), mod.imports().end());
  for (const Mod::Dependency& dependency : mod.dependencies()) {
    if (dependency.name == detail::prelude_mod_name ||
        dependency.name == mod.name()) {
      continue;
    }
    const auto found =
        std::find_if(imports.begin(), imports.end(), [&](const auto& import) {
          return import.name == dependency.name;
        });
    if (found == imports.end()) {
      imports.push_back({dependency.name,
                         {VersionRange::Kind::Exact, dependency.version},
                         {}});
    } else if (found->alias.empty()) {
      found->version = {VersionRange::Kind::Exact, dependency.version};
    }
  }
  std::sort(imports.begin(), imports.end(),
            [](const auto& left, const auto& right) {
              return left.name < right.name;
            });
  for (const Mod::Import& import : imports) {
    output << "  import " << import.name << '@' << to_string(import.version);
    if (!import.alias.empty()) {
      output << " as " << import.alias;
    }
    output << ";\n";
  }

  bool wrote_group = !imports.empty();
  const auto begin_group = [&](bool present) {
    if (!present) {
      return;
    }
    if (wrote_group) {
      output << '\n';
    }
    wrote_group = true;
  };
  begin_group(!mod.storage_->types.empty());
  for (const auto& type : mod.storage_->types) {
    const std::string head =
        std::string("  ") + (type.exported ? "pub " : "") + "type " + type.name;
    std::string flat = head + '(';
    for (std::size_t index = 0; index < type.parameters.size(); ++index) {
      if (index != 0U) {
        flat += ", ";
      }
      flat += parameter_text(type.parameters[index]);
    }
    flat += ')';
    if (!type.derived_parameters.empty()) {
      if (flat.size() <= canonical_line_width || type.parameters.empty()) {
        output << flat << " {\n";
      } else {
        output << head << "(\n";
        for (std::size_t index = 0; index < type.parameters.size(); ++index) {
          output << "    " << parameter_text(type.parameters[index]);
          if (index + 1U != type.parameters.size()) {
            output << ',';
          }
          output << '\n';
        }
        output << "  ) {\n";
      }
      for (const auto& derived : type.derived_parameters) {
        const std::string field_head = "    " + derived.name + ": " +
                                       type_expression_text(derived.domain) +
                                       " = ";
        const std::string value = type_expression_text(derived.value);
        if (field_head.size() + value.size() <= canonical_line_width) {
          output << field_head << value << ";\n";
        } else {
          output << field_head << "\n      "
                 << type_expression_layout(derived.value, 6U) << ";\n";
        }
      }
      output << "  }\n";
      continue;
    }
    flat += ";\n";
    if (flat.size() <= 89U || type.parameters.empty()) {
      output << flat;
      continue;
    }
    output << head << "(\n";
    for (std::size_t index = 0; index < type.parameters.size(); ++index) {
      output << "    " << parameter_text(type.parameters[index]);
      if (index + 1U != type.parameters.size()) {
        output << ',';
      }
      output << '\n';
    }
    output << "  );\n";
  }
  const bool has_declarations =
      std::any_of(mod.storage_->fns.begin(), mod.storage_->fns.end(),
                  [](const detail::FnMember& fn) {
                    return fn.declaration.has_value() && !fn.ir;
                  });
  begin_group(has_declarations);
  for (const detail::FnMember& member : mod.storage_->fns) {
    if (!member.declaration || member.ir) {
      continue;
    }
    const detail::FnDef& fn = *member.declaration;
    std::string head = std::string("  ") + (fn.exported ? "pub " : "") + "fn ";
    if (fn.operator_fixity) {
      if (fn.operator_fixity == Mod::FnDecl::Fixity::Postfix) {
        head += "postfix ";
      } else if (fn.operator_fixity == Mod::FnDecl::Fixity::Infix &&
                 fn.inputs.size() != 2U) {
        head += "infix ";
      }
      head += '(' + fn.name + ')';
    } else {
      head += fn.name;
    }
    if (!fn.generics.empty()) {
      std::vector<std::string> generics;
      generics.reserve(fn.generics.size());
      for (std::size_t index = 0; index < fn.generics.size(); ++index) {
        const auto& generic = fn.generics[index];
        std::string text = generic.name;
        if (generic.domain != Mod::Expr::reference("type")) {
          text += ": " + type_expression_text(generic.domain);
        }
        generics.push_back(std::move(text));
      }
      std::string flat_generics = head + '<';
      for (std::size_t index = 0; index < generics.size(); ++index) {
        if (index != 0U) {
          flat_generics += ", ";
        }
        flat_generics += generics[index];
      }
      flat_generics += '>';
      if (flat_generics.size() <= canonical_line_width) {
        head = std::move(flat_generics);
      } else {
        head += "<\n";
        for (std::size_t index = 0; index < generics.size(); ++index) {
          head += "    " + generics[index];
          if (index + 1U != generics.size()) {
            head += ',';
          }
          head += '\n';
        }
        head += "  >";
      }
    }
    std::vector<std::string> inputs;
    inputs.reserve(fn.inputs.size());
    for (std::size_t index = 0; index < fn.inputs.size(); ++index) {
      const auto& input = fn.inputs[index];
      if (index < fn.types.bindings.size() && fn.types.bindings[index]) {
        inputs.push_back(parameter_text(input, *fn.types.bindings[index]));
      } else {
        inputs.push_back(parameter_text(input));
      }
    }
    std::string result_text;
    if (!fn.results.empty()) {
      result_text = " -> ";
      if (fn.results.size() > 1U) {
        result_text += '(';
      }
      for (std::size_t index = 0; index < fn.results.size(); ++index) {
        if (index != 0U) {
          result_text += ", ";
        }
        result_text += type_expression_text(fn.results[index].domain);
      }
      if (fn.results.size() > 1U) {
        result_text += ')';
      }
    }
    const std::string tail = ")" + result_text;
    std::string flat = head + '(';
    for (std::size_t index = 0; index < inputs.size(); ++index) {
      if (index != 0U) {
        flat += ", ";
      }
      flat += inputs[index];
    }
    flat += tail;
    const bool multiline = flat.size() > 88U;
    if (!multiline) {
      output << flat;
    } else {
      output << head << "(\n";
      for (std::size_t index = 0; index < inputs.size(); ++index) {
        output << "    " << inputs[index];
        if (index + 1U != inputs.size()) {
          output << ',';
        }
        output << '\n';
      }
      if (2U + tail.size() <= canonical_line_width) {
        output << "  " << tail;
      } else {
        output << "  )";
        if (!fn.results.empty()) {
          output << " ->\n";
          if (fn.results.size() == 1U) {
            output << "    "
                   << type_expression_layout(fn.results.front().domain, 4U);
          } else {
            output << "    (\n";
            for (std::size_t index = 0; index < fn.results.size(); ++index) {
              output << "      "
                     << type_expression_layout(fn.results[index].domain, 6U);
              if (index + 1U != fn.results.size()) {
                output << ',';
              }
              output << '\n';
            }
            output << "    )";
          }
        }
      }
    }
    if (fn.body) {
      output << ' ' << detail::format_fn_body(*fn.body, 1U);
      continue;
    }
    output << ";\n";
  }
  const bool has_materialized =
      std::any_of(mod.storage_->fns.begin(), mod.storage_->fns.end(),
                  [](const detail::FnMember& fn) { return fn.ir != nullptr; });
  begin_group(has_materialized);
  std::vector<const detail::FnMember*> materialized;
  materialized.reserve(mod.storage_->fns.size());
  for (const detail::FnMember& fn : mod.storage_->fns) {
    if (fn.ir) {
      materialized.push_back(&fn);
    }
  }
  std::sort(materialized.begin(), materialized.end(),
            [](const detail::FnMember* left, const detail::FnMember* right) {
              return left->name < right->name;
            });
  for (const detail::FnMember* fn : materialized) {
    std::string source = joggle::format(*fn->ir, fn->name);
    if (fn->declaration && fn->declaration->exported) {
      source.insert(0U, "pub ");
    }
    write_indented(output, source);
  }
  output << "}\n";
  return output.str();
}

}  // namespace joggle
