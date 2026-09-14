#include "detail.h"
#include "language.h"
#include "print.h"

#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <unordered_set>

namespace joggle::detail {

std::string attr_text(const Attr& value) {
  if (value.empty())
    return "nil";
  if (const auto item = value.boolean())
    return *item ? "true" : "false";
  if (const auto item = value.integer())
    return std::to_string(*item);
  if (const auto item = value.real()) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<double>::max_digits10)
        << *item;
    std::string text = out.str();
    if (text.find_first_of(".eE") == std::string::npos)
      text += ".0";
    return text;
  }
  if (const auto item = value.string()) {
    std::string out = "\"";
    for (const char ch : *item) {
      if (ch == '"' || ch == '\\')
        out.push_back('\\');
      if (ch == '\n')
        out += "\\n";
      else
        out.push_back(ch);
    }
    return out + '"';
  }
  if (const auto* item = value.bytes()) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out = "hex\"";
    out.reserve(item->size() * 2 + 5);
    for (const std::uint8_t byte : *item) {
      out.push_back(digits[byte >> 4]);
      out.push_back(digits[byte & 15]);
    }
    return out + '"';
  }
  if (const auto* item = value.list()) {
    std::string out = "[";
    for (std::size_t index = 0; index < item->size(); ++index) {
      if (index)
        out += ", ";
      out += attr_text((*item)[index]);
    }
    return out + ']';
  }
  if (const auto* item = value.dict()) {
    std::string out = "{";
    std::size_t index = 0;
    for (const auto& [name, entry] : *item) {
      if (index++)
        out += ", ";
      out += attr_text(Attr(name)) + ": " + attr_text(entry);
    }
    return out + '}';
  }
  return "nil";
}

}  // namespace joggle::detail

namespace joggle {

using detail::attr_text;
using detail::precedence;

namespace {

std::string render_value(const detail::Store& store, std::uint32_t value,
                         int parent = 0, bool right = false,
                         bool typed_literal = false);

bool needs_literal_type(const detail::Store& store, std::uint32_t value,
                        const Attr& literal) {
  if (value >= store.vals.size() || !store.vals[value].live)
    return false;
  const Ty& type = store.vals[value].data.type;
  return (literal.integer() && type.name() != "int") ||
         (literal.real() && type.name() != "f64");
}

std::string render_literal(const detail::Store& store, std::uint32_t value,
                           const Attr& literal) {
  const std::string text = attr_text(literal);
  if (!needs_literal_type(store, value, literal))
    return text;
  return std::string(store.vals[value].data.type.text()) + "(" + text + ")";
}

bool needs_literal_binding(const detail::Store& store,
                           const detail::OpData& op) {
  if (op.kind != Op::Kind::constant || op.form != Op::Form::hidden ||
      !op.literal.bytes() || op.outs.size() != 1)
    return false;
  const std::uint32_t value = op.outs.front();
  return value < store.vals.size() && store.vals[value].live &&
         store.vals[value].data.type.name() == "tensor";
}

std::string literal_name(const detail::Store& store, std::uint32_t value) {
  if (value < store.vals.size() && !store.vals[value].data.name.empty())
    return store.vals[value].data.name;
  std::string name = "_data" + std::to_string(value);
  const auto occupied = [&](std::string_view candidate) {
    return std::any_of(store.vals.begin(), store.vals.end(),
                       [&](const auto& slot) {
                         return slot.live && slot.data.name == candidate;
                       });
  };
  while (occupied(name))
    name.push_back('_');
  return name;
}

std::string render_call(const detail::Store& store, const detail::OpData& op) {
  if (op.callee == "base.list") {
    std::string out = "[";
    for (std::size_t index = 0; index < op.args.size(); ++index) {
      if (index)
        out += ", ";
      out += render_value(store, op.args[index]);
    }
    return out + "]";
  }
  if (op.callee.starts_with("operator ")) {
    const std::string_view symbol(op.callee.data() + 9, op.callee.size() - 9);
    if (symbol == "[]" && !op.args.empty()) {
      std::string out = render_value(store, op.args.front(), 10) + "[";
      for (std::size_t index = 1; index < op.args.size(); ++index) {
        if (index != 1)
          out += ", ";
        out += render_value(store, op.args[index]);
      }
      return out + "]";
    }
    if (symbol == ".." && op.args.size() == 2)
      return render_value(store, op.args[0]) + ".." +
             render_value(store, op.args[1]);
    if (op.args.size() == 1)
      return std::string(symbol) + render_value(store, op.args[0], 9, true);
    if (op.args.size() == 2) {
      const int level = precedence(symbol);
      return render_value(store, op.args[0], level) + " " +
             std::string(symbol) + " " +
             render_value(store, op.args[1], level, true);
    }
  }
  std::string callee = op.callee;
  const std::size_t qualified = callee.rfind(".operator ");
  if (qualified != std::string::npos)
    callee = callee.substr(0, qualified + 1) + callee.substr(qualified + 10);
  std::string out = callee + "(";
  for (std::size_t index = 0; index < op.args.size(); ++index) {
    if (index)
      out += ", ";
    out += render_value(store, op.args[index]);
  }
  return out + ")";
}

std::string render_value(const detail::Store& store, std::uint32_t value,
                         int parent, bool right, bool typed_literal) {
  if (value >= store.vals.size() || !store.vals[value].live)
    return "<invalid>";
  const detail::ValData& data = store.vals[value].data;
  if (data.def == detail::none)
    return data.name;
  const detail::OpData& op = store.ops[data.def].data;
  if (op.kind == Op::Kind::branch && op.logic != detail::Logic::none &&
      op.args.size() == 2 && op.blks.size() == 2) {
    const std::size_t arm = op.logic == detail::Logic::and_ ? 0 : 1;
    const auto& ops = store.blks[op.blks[arm]].data.ops;
    if (!ops.empty()) {
      const detail::OpData& yield = store.ops[ops.back()].data;
      if (yield.kind == Op::Kind::yield && yield.args.size() == 1) {
        const std::string_view symbol =
            op.logic == detail::Logic::and_ ? "&&" : "||";
        const int level = precedence(symbol);
        std::string text = render_value(store, op.args[0], level) + " " +
                           std::string(symbol) + " " +
                           render_value(store, yield.args[0], level, true);
        if (level < parent || (right && level == parent))
          return "(" + text + ")";
        return text;
      }
    }
  }
  if (op.kind == Op::Kind::constant && op.form == Op::Form::hidden &&
      !needs_literal_binding(store, op))
    return typed_literal ? render_literal(store, value, op.literal)
                         : attr_text(op.literal);
  if (op.kind == Op::Kind::call && op.form == Op::Form::hidden) {
    std::string text = render_call(store, op);
    int level = 10;
    if (op.callee.starts_with("operator ")) {
      const std::string_view symbol(op.callee.data() + 9,
                                    op.callee.size() - 9);
      if (op.args.size() == 1)
        level = 9;
      else if (op.args.size() == 2 && precedence(symbol) >= 0)
        level = precedence(symbol);
    }
    if (level < parent || (right && level == parent))
      return "(" + text + ")";
    return text;
  }
  if (data.name.empty() && needs_literal_binding(store, op))
    return literal_name(store, value);
  return data.name.empty() ? "value" : data.name;
}

void indent(std::ostringstream& out, unsigned depth) {
  out << std::string(depth * 2, ' ');
}

void render_meta_items(std::ostringstream& out, const Attr::Dict& meta) {
  std::size_t index = 0;
  for (const auto& [name, value] : meta) {
    if (index++)
      out << ", ";
    out << name;
    if (value.boolean() != true)
      out << ": " << attr_text(value);
  }
}

void render_inline_meta(std::ostringstream& out, const Attr::Dict& meta) {
  if (meta.empty())
    return;
  out << '[';
  render_meta_items(out, meta);
  out << "] ";
}

void render_meta(std::ostringstream& out, const Attr::Dict& meta,
                 unsigned depth) {
  if (meta.empty())
    return;
  indent(out, depth);
  out << '[';
  render_meta_items(out, meta);
  out << "]\n";
}

void render_blk(std::ostringstream& out, const detail::Store& store,
                std::uint32_t blk, unsigned depth) {
  for (const auto id : store.blks[blk].data.ops) {
    if (id >= store.ops.size() || !store.ops[id].live)
      continue;
    const detail::OpData& op = store.ops[id].data;
    const bool bound_literal = needs_literal_binding(store, op);
    if (((op.kind == Op::Kind::call || op.kind == Op::Kind::constant ||
          (op.kind == Op::Kind::branch &&
           op.logic != detail::Logic::none)) &&
         op.form == Op::Form::hidden && !bound_literal) ||
        op.kind == Op::Kind::yield)
      continue;
    render_meta(out, op.meta, depth);
    indent(out, depth);
    if (op.kind == Op::Kind::call || op.kind == Op::Kind::constant) {
      const auto result = op.outs.empty() ? detail::none : op.outs[0];
      const std::string name =
          result == detail::none ? "" : store.vals[result].data.name;
      if (op.form == Op::Form::let || op.form == Op::Form::var ||
          bound_literal) {
        out << (op.form == Op::Form::var ? "var " : "let ");
        for (std::size_t index = 0; index < op.outs.size(); ++index) {
          if (index)
            out << ", ";
          const detail::ValData& value = store.vals[op.outs[index]].data;
          render_inline_meta(out, value.meta);
          out << (bound_literal ? literal_name(store, op.outs[index])
                                : value.name);
          if (value.type_annotation || bound_literal ||
              (op.kind == Op::Kind::constant &&
               needs_literal_type(store, op.outs[index], op.literal)))
            out << ": " << value.type.text();
        }
        out << " = "
            << (op.kind == Op::Kind::constant ? attr_text(op.literal)
                                              : render_call(store, op));
      } else if (op.form == Op::Form::assign) {
        out << name << " = ";
        if (op.kind == Op::Kind::constant)
          out << attr_text(op.literal);
        else if (!op.args.empty())
          out << render_value(store, op.args.back());
        else
          out << "<invalid>";
      } else if (op.form == Op::Form::compound) {
        if (op.kind == Op::Kind::constant) {
          out << name << " = " << attr_text(op.literal);
        } else if (!op.args.empty() &&
                   std::string_view(op.callee).starts_with("operator ")) {
          const std::string_view symbol(
              op.callee.data() + std::string_view("operator ").size(),
              op.callee.size() - std::string_view("operator ").size());
          out << name << ' ' << symbol << "= "
              << render_value(store, op.args.back());
        } else {
          out << name << " = <invalid>";
        }
      } else if (op.form == Op::Form::index_assign) {
        out << name << "[";
        for (std::size_t index = 1; index + 1 < op.args.size(); ++index) {
          if (index != 1)
            out << ", ";
          out << render_value(store, op.args[index]);
        }
        out << "] = " << render_value(store, op.args.back(), 0, false, true);
      } else if (op.kind == Op::Kind::constant)
        out << attr_text(op.literal);
      else
        out << render_call(store, op);
      out << '\n';
    } else if (op.kind == Op::Kind::loop) {
      out << "for ";
      for (std::size_t index = 0; index < op.iter_names.size(); ++index) {
        if (index)
          out << ", ";
        out << op.iter_names[index] << " in "
            << render_value(store, op.args[index]);
      }
      out << " {\n";
      render_blk(out, store, op.blks.front(), depth + 1);
      indent(out, depth);
      out << "}\n";
    } else if (op.kind == Op::Kind::branch &&
               op.logic != detail::Logic::none) {
      const detail::ValData& value = store.vals[op.outs.front()].data;
      if (op.form == Op::Form::let || op.form == Op::Form::var) {
        out << (op.form == Op::Form::var ? "var " : "let ")
            << value.name;
        if (value.type_annotation)
          out << ": " << value.type.text();
        out << " = ";
      }
      out << render_value(store, op.outs.front()) << '\n';
    } else if (op.kind == Op::Kind::branch) {
      out << "if " << render_value(store, op.args.front()) << " {\n";
      render_blk(out, store, op.blks[0], depth + 1);
      indent(out, depth);
      out << "}";
      const auto& else_ops = store.blks[op.blks[1]].data.ops;
      const bool empty_else =
          else_ops.size() == 1 &&
          store.ops[else_ops.front()].data.kind == Op::Kind::yield;
      if (!empty_else) {
        out << " else {\n";
        render_blk(out, store, op.blks[1], depth + 1);
        indent(out, depth);
        out << "}";
      }
      out << '\n';
    } else if (op.kind == Op::Kind::ret) {
      out << "return";
      for (std::size_t index = 0; index < op.args.size(); ++index)
        out << (index ? ", " : " ") << render_value(store, op.args[index]);
      out << '\n';
    }
  }
}

detail::Store printable_store(const detail::Store& source) {
  detail::Store store = source;
  std::unordered_set<std::string> names;
  for (const auto& slot : store.vals)
    if (slot.live && !slot.data.name.empty())
      names.insert(slot.data.name);
  for (std::uint32_t id = 0; id < store.ops.size(); ++id) {
    auto& slot = store.ops[id];
    detail::OpData& op = slot.data;
    if (!slot.live || op.kind != Op::Kind::call ||
        op.form != Op::Form::hidden || op.callee != "base.list" ||
        op.outs.size() != 1)
      continue;
    detail::ValData& value = store.vals[op.outs.front()].data;
    if (!value.type_annotation || value.type.name() != "list" ||
        value.type.args().size() != 1)
      continue;
    const Ty element = value.type.args().front();
    bool needs_annotation = op.args.empty();
    for (const std::uint32_t arg : op.args)
      needs_annotation =
          needs_annotation || store.vals[arg].data.type != element;
    if (!needs_annotation)
      continue;
    std::string name = "list_" + std::to_string(op.outs.front());
    while (names.contains(name))
      name += '_';
    names.insert(name);
    value.name = std::move(name);
    op.form = Op::Form::let;
  }
  return store;
}

}  // namespace

std::string print(const Attr& value) { return attr_text(value); }

bool print(std::FILE* file, const Attr& value) {
  const std::string text = print(value);
  return std::fwrite(text.data(), 1, text.size(), file) == text.size();
}

std::string print(const Mod& mod) {
  const detail::Store printable = printable_store(mod.impl_->store);
  const detail::Store& store = printable;
  std::ostringstream out;
  out << "module " << store.name << '\n';
  for (const std::string& use : store.uses)
    out << "use " << use << '\n';
  if (!store.uses.empty() && !store.fns.empty())
    out << '\n';
  bool first = true;
  for (const auto& entry : store.fns) {
    if (!entry.live)
      continue;
    if (!first)
      out << '\n';
    first = false;
    const detail::FnData& fn = entry.data;
    render_meta(out, fn.meta, 0);
    if (fn.local)
      out << "local ";
    out << "fn ";
    const bool symbolic = std::string_view(fn.name).starts_with("operator ");
    const std::string_view spelling = symbolic
                                          ? std::string_view(fn.name).substr(9)
                                          : std::string_view(fn.name);
    if (symbolic)
      out << spelling;
    else
      out << fn.name;
    if (!fn.generic_vals.empty()) {
      if (symbolic && spelling.find('<') != std::string_view::npos)
        out << ' ';
      out << '<';
      for (std::size_t index = 0; index < fn.generic_vals.size(); ++index) {
        if (index)
          out << ", ";
        const detail::ValData& generic =
            store.vals[fn.generic_vals[index]].data;
        render_inline_meta(out, generic.meta);
        out << generic.name;
        if (generic.type.text() != "_")
          out << ": " << generic.type.text();
      }
      out << '>';
    }
    out << '(';
    for (std::size_t index = 0; index < fn.params.size(); ++index) {
      if (index)
        out << ", ";
      const detail::ValData& param = store.vals[fn.params[index]].data;
      render_inline_meta(out, param.meta);
      out << param.name << ": " << param.type.text();
    }
    out << ") -> ";
    if (fn.returns.size() != 1)
      out << '(';
    for (std::size_t index = 0; index < fn.returns.size(); ++index) {
      if (index)
        out << ", ";
      out << fn.returns[index].text();
    }
    if (fn.returns.size() != 1)
      out << ')';
    if (fn.external) {
      out << ";\n";
      continue;
    }
    out << " {\n";
    render_blk(out, store, fn.blks.front(), 1);
    out << "}\n";
  }
  return out.str();
}

bool print(std::FILE* file, const Mod& mod) {
  const std::string text = print(mod);
  return std::fwrite(text.data(), 1, text.size(), file) == text.size();
}

bool structurally_equal(const Mod& left, const Mod& right) {
  return print(left) == print(right);
}

}  // namespace joggle
