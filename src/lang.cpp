#include "detail.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <exception>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace joggle {

namespace {

enum class Tk : std::uint8_t { end, name, number, string, symbol };

struct Token {
  Tk kind = Tk::end;
  std::string text;
  Loc loc;
};

class Lexer {
public:
  Lexer(std::string_view source, std::string_view file)
      : source_(source), file_(file) {}

  std::vector<Token> run() {
    std::vector<Token> out;
    for (;;) {
      skip_space();
      const Loc begin{file_, line_, column_};
      if (at_end()) {
        out.push_back({Tk::end, {}, begin});
        return out;
      }
      const char ch = peek();
      if (std::isalpha(static_cast<unsigned char>(ch)) || ch == '_') {
        std::string text;
        while (!at_end()) {
          const char next = peek();
          if (std::isalnum(static_cast<unsigned char>(next)) || next == '_')
            text.push_back(take());
          else if (next == '.' && peek(1) != '.')
            text.push_back(take());
          else
            break;
        }
        out.push_back({Tk::name, std::move(text), begin});
        continue;
      }
      if (std::isdigit(static_cast<unsigned char>(ch))) {
        std::string text;
        bool dot = false;
        while (!at_end()) {
          const char next = peek();
          if (std::isdigit(static_cast<unsigned char>(next)))
            text.push_back(take());
          else if (next == '.' && peek(1) != '.' && !dot) {
            dot = true;
            text.push_back(take());
          } else
            break;
        }
        if ((peek() == 'e' || peek() == 'E') &&
            (std::isdigit(static_cast<unsigned char>(peek(1))) ||
             ((peek(1) == '+' || peek(1) == '-') &&
              std::isdigit(static_cast<unsigned char>(peek(2)))))) {
          text.push_back(take());
          if (peek() == '+' || peek() == '-')
            text.push_back(take());
          while (std::isdigit(static_cast<unsigned char>(peek())))
            text.push_back(take());
        }
        out.push_back({Tk::number, std::move(text), begin});
        continue;
      }
      if (ch == '"') {
        take();
        std::string text;
        while (!at_end() && peek() != '"') {
          if (peek() == '\\') {
            take();
            if (at_end())
              break;
            const char escaped = take();
            text.push_back(escaped == 'n' ? '\n' : escaped);
          } else
            text.push_back(take());
        }
        if (!at_end())
          take();
        out.push_back({Tk::string, std::move(text), begin});
        continue;
      }

      std::string text(1, take());
      const std::string pair = text + std::string(1, peek());
      if (pair == "->" || pair == ".." || pair == "+=" || pair == "-=" ||
          pair == "*=" || pair == "/=" || pair == "%=" || pair == "|=" ||
          pair == "^=" || pair == "&=" || pair == "==" || pair == "!=" ||
          pair == "<=" || pair == ">=" || pair == "&&" || pair == "||" ||
          pair == "<<" || pair == ">>")
        text.push_back(take());
      if ((text == "<<" || text == ">>") && peek() == '=')
        text.push_back(take());
      out.push_back({Tk::symbol, std::move(text), begin});
    }
  }

private:
  bool at_end() const { return offset_ >= source_.size(); }
  char peek(std::size_t ahead = 0) const {
    const std::size_t at = offset_ + ahead;
    return at < source_.size() ? source_[at] : '\0';
  }
  char take() {
    const char ch = source_[offset_++];
    if (ch == '\n') {
      ++line_;
      column_ = 1;
    } else
      ++column_;
    return ch;
  }
  void skip_space() {
    for (;;) {
      while (!at_end() && std::isspace(static_cast<unsigned char>(peek())))
        take();
      if (peek() != '/' || peek(1) != '/')
        return;
      while (!at_end() && take() != '\n') {
      }
    }
  }

  std::string_view source_;
  std::string file_;
  std::size_t offset_ = 0;
  std::size_t line_ = 1;
  std::size_t column_ = 1;
};

struct Binding {
  std::uint32_t value = detail::none;
  bool mutable_value = false;
};
using Scope = std::unordered_map<std::string, Binding>;

struct Decl {
  std::string name;
  Ty type{"_"};
  Attr::Dict meta;
};

std::string operator_name(std::string_view spelling) {
  return "operator " + std::string(spelling);
}

std::optional<std::size_t>
generic_index(const std::vector<std::string>& generics, std::string_view name) {
  const auto found = std::find(generics.begin(), generics.end(), name);
  return found == generics.end()
             ? std::nullopt
             : std::optional<std::size_t>(
                   static_cast<std::size_t>(found - generics.begin()));
}

bool same_type_pattern(const Ty& left,
                       const std::vector<std::string>& left_generics,
                       const Ty& right,
                       const std::vector<std::string>& right_generics) {
  const auto left_generic = generic_index(left_generics, left.name());
  const auto right_generic = generic_index(right_generics, right.name());
  if (left.args().empty() && right.args().empty() &&
      (left_generic || right_generic))
    return left_generic == right_generic;
  if (left.name() != right.name() || left.args().size() != right.args().size())
    return false;
  for (std::size_t index = 0; index < left.args().size(); ++index)
    if (!same_type_pattern(left.args()[index], left_generics,
                           right.args()[index], right_generics))
      return false;
  return true;
}

struct GenericInfo {
  std::string_view name;
  Ty type;
};

std::vector<GenericInfo> generic_info(Fn fn) {
  std::vector<GenericInfo> out;
  for (const Val generic : fn.generics())
    out.push_back({generic.name(), generic.type()});
  return out;
}

std::vector<GenericInfo> generic_info(const detail::Store& store,
                                      const detail::FnData& fn) {
  std::vector<GenericInfo> out;
  out.reserve(fn.generic_vals.size());
  for (const std::uint32_t id : fn.generic_vals)
    out.push_back({store.vals[id].data.name, store.vals[id].data.type});
  return out;
}

std::vector<std::string> generic_names(std::span<const GenericInfo> generics) {
  std::vector<std::string> out;
  out.reserve(generics.size());
  for (const GenericInfo& generic : generics)
    out.emplace_back(generic.name);
  return out;
}

int precedence(std::string_view op) {
  if (op == "||")
    return 1;
  if (op == "&&")
    return 2;
  if (op == "==" || op == "!=")
    return 3;
  if (op == "<" || op == "<=" || op == ">" || op == ">=")
    return 4;
  if (op == "|" || op == "^" || op == "&")
    return 5;
  if (op == "<<" || op == ">>")
    return 6;
  if (op == "+" || op == "-")
    return 7;
  if (op == "*" || op == "/" || op == "%")
    return 8;
  return -1;
}

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

}  // namespace

class Parser {
public:
  Parser(Env&, std::string_view source, Mod& mod, std::string_view file)
      : store_(mod.impl_->store), tokens_(Lexer(source, file).run()) {}

  bool run() {
    store_ = {};
    if (!word("module"))
      return fail("expected 'module'");
    if (peek().kind != Tk::name)
      return fail("expected module name");
    store_.name = take().text;
    semi();
    while (word("use")) {
      if (peek().kind != Tk::name)
        return fail("expected module name after 'use'");
      store_.uses.push_back(take().text);
      semi();
    }
    while (!at_end()) {
      Attr::Dict meta;
      if (!parse_meta(meta) || !parse_fn(std::move(meta)))
        return false;
    }
    detail::rebuild_uses(store_);
    return store_.diags.empty();
  }

private:
  const Token& peek(std::size_t ahead = 0) const {
    const std::size_t at = std::min(pos_ + ahead, tokens_.size() - 1);
    return tokens_[at];
  }
  Token take() { return tokens_[pos_++]; }
  bool at_end() const { return peek().kind == Tk::end; }
  bool is(std::string_view text) const { return peek().text == text; }
  bool match(std::string_view text) {
    if (!is(text))
      return false;
    ++pos_;
    return true;
  }
  bool word(std::string_view text) {
    if (peek().kind != Tk::name || peek().text != text)
      return false;
    ++pos_;
    return true;
  }
  void semi() { match(";"); }
  bool fail(std::string message, Loc loc = {}) {
    if (loc.line == 0)
      loc = peek().loc;
    detail::add_diag(store_.diags, std::move(message), std::move(loc));
    return false;
  }
  bool expect(std::string_view text) {
    return match(text) ? true : fail("expected '" + std::string(text) + "'");
  }
  std::string take_name(std::string_view what) {
    if (peek().kind != Tk::name) {
      fail("expected " + std::string(what));
      return {};
    }
    return take().text;
  }

  bool parse_meta(Attr::Dict& out) {
    while (match("[")) {
      if (is("]"))
        return fail("attribute list cannot be empty");
      do {
        const Token key = take();
        if (key.kind != Tk::name)
          return fail("expected metadata name", key.loc);
        Attr value(true);
        if (match(":")) {
          Ty ignored;
          auto parsed = attr_literal(ignored);
          if (!parsed)
            return false;
          value = std::move(*parsed);
        }
        if (!out.emplace(key.text, std::move(value)).second)
          return fail("duplicate attribute '" + key.text + "'", key.loc);
      } while (match(","));
      if (!expect("]"))
        return false;
    }
    return true;
  }

  std::uint32_t add_val(detail::ValData data) {
    const auto id = static_cast<std::uint32_t>(store_.vals.size());
    store_.vals.push_back({std::move(data), 1, true});
    return id;
  }

  std::uint32_t add_blk(std::uint32_t fn, std::uint32_t parent) {
    const auto id = static_cast<std::uint32_t>(store_.blks.size());
    detail::BlkData data;
    data.fn = fn;
    data.parent_op = parent;
    store_.blks.push_back({std::move(data), 1, true});
    store_.fns[fn].data.blks.push_back(id);
    return id;
  }

  std::uint32_t add_op(std::uint32_t blk, detail::OpData data,
                       std::vector<std::pair<std::string, Ty>> results = {}) {
    const auto id = static_cast<std::uint32_t>(store_.ops.size());
    data.blk = blk;
    for (std::size_t index = 0; index < results.size(); ++index) {
      detail::ValData value;
      value.name = std::move(results[index].first);
      value.type = std::move(results[index].second);
      value.def = id;
      value.index = index;
      data.outs.push_back(add_val(std::move(value)));
    }
    store_.ops.push_back({std::move(data), 1, true});
    store_.blks[blk].data.ops.push_back(id);
    return id;
  }

  std::uint32_t add_call(std::uint32_t blk, std::string callee,
                         std::vector<std::uint32_t> args, Ty type,
                         Loc loc = {}) {
    detail::OpData data;
    data.kind = Op::Kind::call;
    data.callee = std::move(callee);
    data.args = std::move(args);
    data.loc = std::move(loc);
    const auto op = add_op(blk, std::move(data), {{"", std::move(type)}});
    return store_.ops[op].data.outs.front();
  }

  std::uint32_t add_const(std::uint32_t blk, Attr value, Ty type, Loc loc) {
    detail::OpData data;
    data.kind = Op::Kind::constant;
    data.literal = std::move(value);
    data.loc = std::move(loc);
    const auto op = add_op(blk, std::move(data), {{"", std::move(type)}});
    return store_.ops[op].data.outs.front();
  }

  void show(std::uint32_t value, detail::Form form, std::string name = {}) {
    auto& val = store_.vals[value].data;
    if (!name.empty())
      val.name = std::move(name);
    if (val.def != detail::none)
      store_.ops[val.def].data.form = form;
  }

  bool attach(std::uint32_t value, Attr::Dict meta) {
    if (meta.empty())
      return true;
    if (value >= store_.vals.size() ||
        store_.vals[value].data.def == detail::none)
      return fail("attributes require an operation statement");
    store_.ops[store_.vals[value].data.def].data.meta = std::move(meta);
    return true;
  }

  std::string type_text(std::string_view close) {
    std::string out;
    int angle = 0;
    int square = 0;
    int paren = 0;
    while (!at_end()) {
      Token& token = tokens_[pos_];
      if (angle == 0 && square == 0 && paren == 0 &&
          (token.text == close || token.text == "," || token.text == "{" ||
           token.text == ";"))
        break;
      if (token.text == ">>" && angle == 1 && close == ">") {
        --angle;
        out += '>';
        token.text = ">";
        ++token.loc.column;
        continue;
      }
      if (token.text == "<")
        ++angle;
      else if (token.text == ">>" && angle >= 2)
        angle -= 2;
      else if (token.text == ">")
        --angle;
      else if (token.text == "[")
        ++square;
      else if (token.text == "]")
        --square;
      else if (token.text == "(")
        ++paren;
      else if (token.text == ")") {
        if (paren == 0 && close == ")")
          break;
        --paren;
      }
      const std::string text = take().text;
      out += text == "," ? ", " : text;
    }
    return out;
  }

  bool parse_fn(Attr::Dict meta) {
    if (!word("fn"))
      return fail("expected function declaration");
    const Token first = take();
    const Loc loc = first.loc;
    std::string name;
    if (first.kind == Tk::name)
      name = first.text;
    else if (first.kind == Tk::symbol) {
      std::string spelling = first.text;
      if (spelling == "[") {
        if (!expect("]"))
          return false;
        spelling = "[]";
        if (match("="))
          spelling += '=';
      }
      if (precedence(spelling) < 0 && spelling != "!" && spelling != "~" &&
          spelling != ".." && spelling != "[]" && spelling != "[]=")
        return fail("unsupported function symbol '" + spelling + "'", loc);
      name = operator_name(spelling);
    } else
      return fail("expected function name", loc);
    if (name.empty())
      return false;

    detail::FnData data;
    data.name = name;
    data.loc = loc;
    data.meta = std::move(meta);
    std::vector<Decl> generics;
    if (match("<")) {
      do {
        Attr::Dict generic_meta;
        if (!parse_meta(generic_meta))
          return false;
        std::string generic = take_name("generic parameter");
        if (generic.empty())
          return false;
        if (std::any_of(
                generics.begin(), generics.end(),
                [&](const Decl& item) { return item.name == generic; }))
          return fail("duplicate generic parameter '" + generic + "'");
        Ty type("_");
        if (match(":")) {
          const std::string text = type_text(">");
          if (text.empty())
            return fail("expected generic parameter type");
          type = Ty(text);
          if (!type.valid())
            return fail("malformed generic parameter type '" + text + "'");
        }
        generics.push_back(
            {std::move(generic), std::move(type), std::move(generic_meta)});
      } while (match(","));
      if (!expect(">"))
        return false;
    }
    if (!expect("("))
      return false;
    std::vector<Decl> params;
    if (!is(")")) {
      do {
        Attr::Dict param_meta;
        if (!parse_meta(param_meta))
          return false;
        std::string param = take_name("parameter name");
        if (param.empty() || !expect(":"))
          return false;
        if (std::any_of(
                generics.begin(), generics.end(),
                [&](const Decl& item) { return item.name == param; }) ||
            std::any_of(params.begin(), params.end(),
                        [&](const Decl& item) { return item.name == param; }))
          return fail("duplicate parameter '" + param + "'");
        const std::string type = type_text(")");
        if (type.empty())
          return fail("expected parameter type");
        Ty parsed(type);
        if (!parsed.valid())
          return fail("malformed parameter type '" + type + "'");
        params.push_back(
            {std::move(param), std::move(parsed), std::move(param_meta)});
      } while (match(","));
    }
    if (!expect(")") || !expect("->"))
      return false;
    if (match("(")) {
      if (!is(")")) {
        do {
          const std::string type = type_text(")");
          if (type.empty())
            return fail("expected return type");
          Ty parsed(type);
          if (!parsed.valid())
            return fail("malformed return type '" + type + "'");
          data.returns.push_back(std::move(parsed));
        } while (match(","));
      }
      if (!expect(")"))
        return false;
    } else {
      const std::string type = type_text("{");
      if (type.empty())
        return fail("expected return type");
      Ty parsed(type);
      if (!parsed.valid())
        return fail("malformed return type '" + type + "'");
      data.returns.push_back(std::move(parsed));
    }

    const auto overloads = store_.symbols.find(name);
    if (overloads != store_.symbols.end()) {
      for (const std::uint32_t id : overloads->second) {
        const detail::FnData& existing = store_.fns[id].data;
        if (existing.generic_vals.size() != generics.size() ||
            existing.params.size() != params.size())
          continue;
        const std::vector<std::string> existing_generics =
            generic_names(generic_info(store_, existing));
        std::vector<std::string> parsed_generics;
        parsed_generics.reserve(generics.size());
        for (const Decl& generic : generics)
          parsed_generics.push_back(generic.name);
        bool same = true;
        for (std::size_t index = 0; index < params.size(); ++index)
          same = same &&
                 same_type_pattern(
                     store_.vals[existing.params[index]].data.type,
                     existing_generics, params[index].type, parsed_generics);
        if (same)
          return fail("duplicate function signature '" + name + "'", loc);
      }
    }
    const auto fn = static_cast<std::uint32_t>(store_.fns.size());
    store_.fns.push_back({std::move(data), 1, true});
    store_.symbols[name].push_back(fn);

    Scope scope;
    for (Decl& generic : generics) {
      detail::ValData value;
      value.kind = detail::ValKind::generic;
      value.name = generic.name;
      value.type = std::move(generic.type);
      value.meta = std::move(generic.meta);
      const auto id = add_val(std::move(value));
      store_.fns[fn].data.generic_vals.push_back(id);
      scope.emplace(generic.name, Binding{id, false});
    }
    for (Decl& param : params) {
      detail::ValData value;
      value.kind = detail::ValKind::param;
      value.name = param.name;
      value.type = std::move(param.type);
      value.meta = std::move(param.meta);
      const auto id = add_val(std::move(value));
      store_.fns[fn].data.params.push_back(id);
      scope.emplace(param.name, Binding{id, false});
    }
    if (match(";")) {
      store_.fns[fn].data.external = true;
      return true;
    }
    if (!expect("{"))
      return false;
    const auto body = add_blk(fn, detail::none);
    if (!parse_blk(fn, body, scope, false) || !expect("}"))
      return false;
    semi();
    return true;
  }

  bool parse_blk(std::uint32_t fn, std::uint32_t blk, Scope& scope,
                   bool nested) {
    while (!at_end() && !is("}")) {
      Attr::Dict meta;
      if (!parse_meta(meta))
        return false;
      if (word("let") || word("var")) {
        const bool mut = tokens_[pos_ - 1].text == "var";
        const detail::Form form = mut ? detail::Form::var : detail::Form::let;
        std::vector<Decl> names;
        do {
          Attr::Dict value_meta;
          if (!parse_meta(value_meta))
            return false;
          std::string name = take_name("binding name");
          if (name.empty())
            return false;
          if (std::any_of(names.begin(), names.end(),
                          [&](const Decl& item) { return item.name == name; }))
            return fail("duplicate binding '" + name + "'");
          Ty type("_");
          if (match(":")) {
            const std::string text = type_text("=");
            if (text.empty())
              return fail("expected binding type");
            type = Ty(text);
            if (!type.valid())
              return fail("malformed binding type '" + text + "'");
          }
          names.push_back(
              {std::move(name), std::move(type), std::move(value_meta)});
        } while (match(","));
        if (!expect("="))
          return false;
        std::uint32_t value = expression(blk, scope);
        if (value == detail::none)
          return false;
        if (names.size() > 1) {
          const std::uint32_t def = store_.vals[value].data.def;
          if (def == detail::none ||
              store_.ops[def].data.kind != Op::Kind::call ||
              store_.ops[def].data.form != detail::Form::hidden ||
              store_.ops[def].data.outs.size() != 1)
            return fail("multiple bindings require one direct call");
          for (std::size_t index = 1; index < names.size(); ++index) {
            detail::ValData result;
            result.type = names[index].type;
            result.meta = names[index].meta;
            result.def = def;
            result.index = index;
            result.type_annotation = names[index].type.text() != "_";
            store_.ops[def].data.outs.push_back(add_val(std::move(result)));
          }
        }
        const std::uint32_t expression_def = store_.vals[value].data.def;
        if (expression_def == detail::none ||
            (store_.ops[expression_def].data.kind != Op::Kind::call &&
             store_.ops[expression_def].data.kind != Op::Kind::constant) ||
            store_.ops[expression_def].data.form != detail::Form::hidden)
          value = add_call(blk, "base.copy", {value},
                           store_.vals[value].data.type, peek().loc);
        const std::uint32_t def = store_.vals[value].data.def;
        if (names.front().type.text() != "_") {
          store_.vals[value].data.type = names.front().type;
          store_.vals[value].data.type_annotation = true;
        }
        store_.vals[value].data.meta = names.front().meta;
        show(value, form, names.front().name);
        if (!attach(value, std::move(meta)))
          return false;
        const std::vector<std::uint32_t>& outs = store_.ops[def].data.outs;
        for (std::size_t index = 0; index < names.size(); ++index) {
          store_.vals[outs[index]].data.name = names[index].name;
          scope[names[index].name] = {outs[index], mut};
        }
        semi();
      } else if (word("for")) {
        if (!parse_for(fn, blk, scope, std::move(meta)))
          return false;
      } else if (word("if")) {
        if (!parse_if(fn, blk, scope, std::move(meta)))
          return false;
      } else if (word("return")) {
        detail::OpData data;
        data.kind = Op::Kind::ret;
        data.meta = std::move(meta);
        data.loc = tokens_[pos_ - 1].loc;
        if (!is(";") && !is("}")) {
          do {
            const auto value = expression(blk, scope);
            if (value == detail::none)
              return false;
            data.args.push_back(value);
          } while (match(","));
        }
        add_op(blk, std::move(data));
        semi();
      } else if (!parse_assignment(blk, scope, std::move(meta)))
        return false;
    }
    return !(nested && at_end()) || fail("unterminated block");
  }

  std::vector<std::pair<std::string, Binding>> carried(const Scope& scope) {
    std::vector<std::pair<std::string, Binding>> out;
    for (const auto& item : scope)
      if (item.second.mutable_value)
        out.push_back(item);
    std::sort(out.begin(), out.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    return out;
  }

  bool parse_for(std::uint32_t fn, std::uint32_t blk, Scope& scope,
                 Attr::Dict meta) {
    detail::OpData data;
    data.kind = Op::Kind::loop;
    data.meta = std::move(meta);
    data.loc = tokens_[pos_ - 1].loc;
    do {
      const std::string iter = take_name("loop variable");
      if (iter.empty() || !word("in"))
        return fail("expected 'in' after loop variable");
      auto source = expression(blk, scope);
      if (source == detail::none)
        return false;
      if (match("..")) {
        const auto upper = expression(blk, scope);
        if (upper == detail::none)
          return false;
        source =
            add_call(blk, operator_name(".."), {source, upper}, Ty("range"));
      }
      data.iter_names.push_back(iter);
      data.args.push_back(source);
    } while (match(","));

    const auto captures = carried(scope);
    data.carried_count = captures.size();
    std::vector<std::pair<std::string, Ty>> results;
    for (const auto& [name, binding] : captures) {
      data.args.push_back(binding.value);
      results.emplace_back(name, store_.vals[binding.value].data.type);
    }
    const auto op = add_op(blk, std::move(data), std::move(results));
    for (std::size_t index = 0; index < captures.size(); ++index)
      store_.vals[store_.ops[op].data.outs[index]].data.meta =
          store_.vals[captures[index].second.value].data.meta;
    const auto body = add_blk(fn, op);
    store_.ops[op].data.blks.push_back(body);

    Scope inner = scope;
    for (const std::string& iter : store_.ops[op].data.iter_names) {
      detail::ValData value;
      value.kind = detail::ValKind::blk_arg;
      value.name = iter;
      value.type = Ty("index");
      const auto id = add_val(std::move(value));
      store_.blks[body].data.args.push_back(id);
      inner[iter] = {id, false};
    }
    for (const auto& [name, binding] : captures) {
      detail::ValData value;
      value.kind = detail::ValKind::blk_arg;
      value.name = name;
      value.type = store_.vals[binding.value].data.type;
      value.meta = store_.vals[binding.value].data.meta;
      const auto id = add_val(std::move(value));
      store_.blks[body].data.args.push_back(id);
      inner[name] = {id, true};
    }
    if (!expect("{") || !parse_blk(fn, body, inner, true) || !expect("}"))
      return false;
    detail::OpData yield;
    yield.kind = Op::Kind::yield;
    for (const auto& [name, ignored] : captures) {
      (void)ignored;
      yield.args.push_back(inner.at(name).value);
    }
    add_op(body, std::move(yield));
    for (std::size_t index = 0; index < captures.size(); ++index)
      scope[captures[index].first].value = store_.ops[op].data.outs[index];
    return true;
  }

  bool parse_if(std::uint32_t fn, std::uint32_t blk, Scope& scope,
                Attr::Dict meta) {
    detail::OpData data;
    data.kind = Op::Kind::branch;
    data.meta = std::move(meta);
    data.loc = tokens_[pos_ - 1].loc;
    const auto condition = expression(blk, scope);
    if (condition == detail::none)
      return false;
    data.args.push_back(condition);
    const auto captures = carried(scope);
    data.carried_count = captures.size();
    std::vector<std::pair<std::string, Ty>> results;
    for (const auto& [name, binding] : captures) {
      data.args.push_back(binding.value);
      results.emplace_back(name, store_.vals[binding.value].data.type);
    }
    const auto op = add_op(blk, std::move(data), std::move(results));
    for (std::size_t index = 0; index < captures.size(); ++index)
      store_.vals[store_.ops[op].data.outs[index]].data.meta =
          store_.vals[captures[index].second.value].data.meta;

    auto arm = [&](bool present) {
      const auto body = add_blk(fn, op);
      store_.ops[op].data.blks.push_back(body);
      Scope inner = scope;
      for (const auto& [name, binding] : captures) {
        detail::ValData value;
        value.kind = detail::ValKind::blk_arg;
        value.name = name;
        value.type = store_.vals[binding.value].data.type;
        value.meta = store_.vals[binding.value].data.meta;
        const auto id = add_val(std::move(value));
        store_.blks[body].data.args.push_back(id);
        inner[name] = {id, true};
      }
      if (present && !parse_blk(fn, body, inner, true))
        return false;
      detail::OpData yield;
      yield.kind = Op::Kind::yield;
      for (const auto& [name, ignored] : captures) {
        (void)ignored;
        yield.args.push_back(inner.at(name).value);
      }
      add_op(body, std::move(yield));
      return true;
    };

    if (!expect("{") || !arm(true) || !expect("}"))
      return false;
    if (word("else")) {
      if (!expect("{") || !arm(true) || !expect("}"))
        return false;
    } else if (!arm(false))
      return false;
    for (std::size_t index = 0; index < captures.size(); ++index)
      scope[captures[index].first].value = store_.ops[op].data.outs[index];
    return true;
  }

  bool parse_assignment(std::uint32_t blk, Scope& scope, Attr::Dict meta) {
    const std::size_t start = pos_;
    static constexpr std::string_view assignments[] = {
        "=",  "+=", "-=", "*=",  "/=",  "%=",
        "|=", "^=", "&=", "<<=", ">>="};
    const bool assignment_token = std::find(
        std::begin(assignments), std::end(assignments), peek(1).text) !=
                                  std::end(assignments);
    if (peek().kind == Tk::name && assignment_token) {
      const Token name = take();
      const std::string assignment = take().text;
      auto found = scope.find(name.text);
      if (found == scope.end())
        return fail("unknown name '" + name.text + "'", name.loc);
      if (!found->second.mutable_value)
        return fail("cannot assign to immutable '" + name.text + "'", name.loc);
      const auto rhs = expression(blk, scope);
      if (rhs == detail::none)
        return false;
      std::uint32_t value = rhs;
      detail::Form form = detail::Form::assign;
      if (assignment != "=") {
        value = add_call(blk,
                         operator_name(assignment.substr(0,
                                                         assignment.size() - 1)),
                         {found->second.value, rhs},
                         store_.vals[found->second.value].data.type, name.loc);
        form = detail::Form::compound;
      } else
        value = add_call(blk, "base.copy", {rhs},
                         store_.vals[found->second.value].data.type, name.loc);
      store_.vals[value].data.meta =
          store_.vals[found->second.value].data.meta;
      show(value, form, name.text);
      if (!attach(value, std::move(meta)))
        return false;
      found->second.value = value;
      semi();
      return true;
    }

    pos_ = start;
    if (peek().kind == Tk::name && peek(1).text == "[") {
      const Token base = take();
      auto found = scope.find(base.text);
      if (found == scope.end())
        return fail("unknown name '" + base.text + "'", base.loc);
      if (!found->second.mutable_value)
        return fail("cannot assign through immutable '" + base.text + "'",
                    base.loc);
      take();
      std::vector<std::uint32_t> args{found->second.value};
      if (!is("]")) {
        do {
          const auto index = expression(blk, scope);
          if (index == detail::none)
            return false;
          args.push_back(index);
        } while (match(","));
      }
      if (!expect("]") || !expect("="))
        return false;
      const auto rhs = expression(blk, scope);
      if (rhs == detail::none)
        return false;
      args.push_back(rhs);
      const auto value =
          add_call(blk, operator_name("[]="), std::move(args),
                   store_.vals[found->second.value].data.type, base.loc);
      store_.vals[value].data.meta =
          store_.vals[found->second.value].data.meta;
      show(value, detail::Form::index_assign, base.text);
      if (!attach(value, std::move(meta)))
        return false;
      found->second.value = value;
      semi();
      return true;
    }

    pos_ = start;
    const auto value = expression(blk, scope);
    if (value == detail::none)
      return false;
    show(value, detail::Form::expr);
    if (!attach(value, std::move(meta)))
      return false;
    semi();
    return true;
  }

  std::uint32_t expression(std::uint32_t blk, Scope& scope, int minimum = 0) {
    std::uint32_t left = unary(blk, scope);
    if (left == detail::none)
      return left;
    for (;;) {
      const int level = precedence(peek().text);
      if (level < minimum)
        break;
      const Token op = take();
      const auto right = expression(blk, scope, level + 1);
      if (right == detail::none)
        return right;
      Ty type = store_.vals[left].data.type;
      if (op.text == "==" || op.text == "!=" || op.text == "<" ||
          op.text == "<=" || op.text == ">" || op.text == ">=" ||
          op.text == "&&" || op.text == "||")
        type = Ty("bool");
      left =
          add_call(blk, operator_name(op.text), {left, right}, type, op.loc);
    }
    return left;
  }

  std::uint32_t unary(std::uint32_t blk, Scope& scope) {
    if (is("+") || is("-") || is("!") || is("~")) {
      const Token op = take();
      const auto arg = unary(blk, scope);
      if (arg == detail::none)
        return arg;
      return add_call(blk, operator_name(op.text), {arg},
                      store_.vals[arg].data.type, op.loc);
    }
    return postfix(blk, scope);
  }

  std::uint32_t postfix(std::uint32_t blk, Scope& scope) {
    std::uint32_t value = primary(blk, scope);
    if (value == detail::none)
      return value;
    while (is("[") && peek().loc.line == tokens_[pos_ - 1].loc.line) {
      const Token open = take();
      std::vector<std::uint32_t> args{value};
      if (!is("]")) {
        do {
          const auto index = expression(blk, scope);
          if (index == detail::none)
            return index;
          args.push_back(index);
        } while (match(","));
      }
      if (!expect("]"))
        return detail::none;
      value = add_call(blk, operator_name("[]"), std::move(args), Ty("_"),
                       open.loc);
    }
    return value;
  }

  std::optional<std::string> template_suffix() {
    if (!match("<"))
      return std::nullopt;
    std::string out = "<";
    int depth = 1;
    while (!at_end() && depth > 0) {
      const std::string text = take().text;
      if (text == "<")
        ++depth;
      else if (text == ">")
        --depth;
      else if (text == ">>")
        depth -= 2;
      out += text == "," ? ", " : text;
    }
    return depth == 0 ? std::optional<std::string>(std::move(out))
                      : std::nullopt;
  }

  std::optional<Attr> attr_literal(Ty& type) {
    const bool negative = match("-");
    if (peek().kind == Tk::number) {
      const Token token = take();
      const std::string text = negative ? "-" + token.text : token.text;
      if (token.text.find_first_of(".eE") != std::string::npos) {
        double value = 0;
        std::size_t consumed = 0;
        try {
          value = std::stod(text, &consumed);
        } catch (const std::exception&) {
          fail("invalid real literal", token.loc);
          return std::nullopt;
        }
        if (consumed != text.size()) {
          fail("invalid real literal", token.loc);
          return std::nullopt;
        }
        type = Ty("f64");
        return Attr(value);
      }
      std::int64_t value = 0;
      const auto result =
          std::from_chars(text.data(), text.data() + text.size(), value);
      if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        fail("invalid integer literal", token.loc);
        return std::nullopt;
      }
      type = Ty("int");
      return Attr(value);
    }
    if (negative) {
      fail("expected a number after '-'");
      return std::nullopt;
    }
    if (peek().kind == Tk::string) {
      type = Ty("str");
      return Attr(take().text);
    }
    if (word("true")) {
      type = Ty("bool");
      return Attr(true);
    }
    if (word("false")) {
      type = Ty("bool");
      return Attr(false);
    }
    if (word("nil")) {
      type = Ty("nil");
      return Attr{};
    }
    if (word("hex")) {
      if (peek().kind != Tk::string) {
        fail("expected a string after 'hex'");
        return std::nullopt;
      }
      const Token token = take();
      if (token.text.size() % 2) {
        fail("hex literal must contain whole bytes", token.loc);
        return std::nullopt;
      }
      auto nibble = [](char ch) -> int {
        if (ch >= '0' && ch <= '9')
          return ch - '0';
        if (ch >= 'a' && ch <= 'f')
          return ch - 'a' + 10;
        if (ch >= 'A' && ch <= 'F')
          return ch - 'A' + 10;
        return -1;
      };
      Attr::Bytes bytes;
      bytes.reserve(token.text.size() / 2);
      for (std::size_t index = 0; index < token.text.size(); index += 2) {
        const int high = nibble(token.text[index]);
        const int low = nibble(token.text[index + 1]);
        if (high < 0 || low < 0) {
          fail("hex literal contains a non-hex digit", token.loc);
          return std::nullopt;
        }
        bytes.push_back(static_cast<std::uint8_t>((high << 4) | low));
      }
      type = Ty("bytes");
      return Attr(std::move(bytes));
    }
    if (match("[")) {
      Attr::List list;
      if (!is("]")) {
        do {
          Ty ignored;
          auto item = attr_literal(ignored);
          if (!item)
            return std::nullopt;
          list.push_back(std::move(*item));
        } while (match(","));
      }
      if (!expect("]"))
        return std::nullopt;
      type = Ty("list");
      return Attr(std::move(list));
    }
    if (match("{")) {
      Attr::Dict dict;
      if (!is("}")) {
        do {
          if (peek().kind != Tk::name && peek().kind != Tk::string) {
            fail("expected attribute name");
            return std::nullopt;
          }
          const Token name = take();
          if (!expect(":"))
            return std::nullopt;
          Ty ignored;
          auto item = attr_literal(ignored);
          if (!item)
            return std::nullopt;
          if (!dict.emplace(name.text, std::move(*item)).second) {
            fail("duplicate attribute '" + name.text + "'", name.loc);
            return std::nullopt;
          }
        } while (match(","));
      }
      if (!expect("}"))
        return std::nullopt;
      type = Ty("dict");
      return Attr(std::move(dict));
    }
    fail("expected attribute literal");
    return std::nullopt;
  }

  std::uint32_t primary(std::uint32_t blk, Scope& scope) {
    if (match("[")) {
      const Loc loc = tokens_[pos_ - 1].loc;
      std::vector<std::uint32_t> items;
      if (!is("]")) {
        do {
          const auto item = expression(blk, scope);
          if (item == detail::none)
            return detail::none;
          items.push_back(item);
        } while (match(","));
      }
      if (!expect("]"))
        return detail::none;
      return add_call(blk, "base.list", std::move(items), Ty("list"), loc);
    }
    if (peek().kind == Tk::number || peek().kind == Tk::string || is("true") ||
        is("false") || is("{") || is("hex") || is("nil")) {
      const Loc loc = peek().loc;
      Ty type;
      auto value = attr_literal(type);
      return value ? add_const(blk, std::move(*value), std::move(type), loc)
                   : detail::none;
    }
    const Token token = take();
    if (token.text == "(") {
      const auto value = expression(blk, scope);
      return expect(")") ? value : detail::none;
    }
    if (token.kind != Tk::name) {
      fail("expected expression", token.loc);
      return detail::none;
    }

    std::string callee = token.text;
    if (is("<")) {
      const std::size_t save = pos_;
      const auto suffix = template_suffix();
      if (suffix && is("("))
        callee += *suffix;
      else
        pos_ = save;
    }
    if (match("(")) {
      std::vector<std::uint32_t> args;
      if (!is(")")) {
        do {
          const auto arg = expression(blk, scope);
          if (arg == detail::none)
            return arg;
          args.push_back(arg);
        } while (match(","));
      }
      if (!expect(")"))
        return detail::none;
      Ty result("_");
      if (callee.find('<') != std::string::npos || scope.contains(callee))
        result = Ty(callee);
      return add_call(blk, std::move(callee), std::move(args),
                      std::move(result), token.loc);
    }

    const auto found = scope.find(token.text);
    if (found == scope.end()) {
      fail("unknown name '" + token.text + "'", token.loc);
      return detail::none;
    }
    return found->second.value;
  }

  detail::Store& store_;
  std::vector<Token> tokens_;
  std::size_t pos_ = 0;
};

namespace {

std::string render_value(const detail::Store& store, std::uint32_t value,
                         int parent = 0, bool right = false);

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
  std::string out = op.callee + "(";
  for (std::size_t index = 0; index < op.args.size(); ++index) {
    if (index)
      out += ", ";
    out += render_value(store, op.args[index]);
  }
  return out + ")";
}

std::string render_value(const detail::Store& store, std::uint32_t value,
                         int parent, bool right) {
  if (value >= store.vals.size() || !store.vals[value].live)
    return "<invalid>";
  const detail::ValData& data = store.vals[value].data;
  if (data.def == detail::none)
    return data.name;
  const detail::OpData& op = store.ops[data.def].data;
  if (op.kind == Op::Kind::constant && op.form == detail::Form::hidden)
    return attr_text(op.literal);
  if (op.kind == Op::Kind::call && op.form == detail::Form::hidden) {
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
    if (((op.kind == Op::Kind::call || op.kind == Op::Kind::constant) &&
         op.form == detail::Form::hidden) ||
        op.kind == Op::Kind::yield)
      continue;
    render_meta(out, op.meta, depth);
    indent(out, depth);
    if (op.kind == Op::Kind::call || op.kind == Op::Kind::constant) {
      const auto result = op.outs.empty() ? detail::none : op.outs[0];
      const std::string name =
          result == detail::none ? "" : store.vals[result].data.name;
      if (op.form == detail::Form::let || op.form == detail::Form::var) {
        out << (op.form == detail::Form::var ? "var " : "let ");
        for (std::size_t index = 0; index < op.outs.size(); ++index) {
          if (index)
            out << ", ";
          const detail::ValData& value = store.vals[op.outs[index]].data;
          render_inline_meta(out, value.meta);
          out << value.name;
          if (value.type_annotation)
            out << ": " << value.type.text();
        }
        out << " = "
            << (op.kind == Op::Kind::constant ? attr_text(op.literal)
                                              : render_call(store, op));
      } else if (op.form == detail::Form::assign)
        out << name << " = " << render_value(store, op.args.back());
      else if (op.form == detail::Form::compound) {
        const std::string_view symbol(
            op.callee.data() + std::string_view("operator ").size(),
            op.callee.size() - std::string_view("operator ").size());
        out << name << ' ' << symbol << "= "
            << render_value(store, op.args.back());
      }
      else if (op.form == detail::Form::index_assign) {
        out << name << "[";
        for (std::size_t index = 1; index + 1 < op.args.size(); ++index) {
          if (index != 1)
            out << ", ";
          out << render_value(store, op.args[index]);
        }
        out << "] = " << render_value(store, op.args.back());
      } else
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

}  // namespace

bool parse(Env& env, std::string_view source, Mod& out, std::string_view file) {
  return Parser(env, source, out, file).run();
}

std::string print(const Mod& mod) {
  const detail::Store& store = mod.impl_->store;
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

namespace {

std::uint32_t arg_blk(const detail::Store& store, std::uint32_t value) {
  for (std::uint32_t id = 0; id < store.blks.size(); ++id) {
    const auto& args = store.blks[id].data.args;
    if (std::find(args.begin(), args.end(), value) != args.end())
      return id;
  }
  return detail::none;
}

std::uint32_t arg_fn(const detail::Store& store, std::uint32_t value) {
  for (std::uint32_t id = 0; id < store.fns.size(); ++id) {
    const auto& fn = store.fns[id].data;
    if (std::find(fn.generic_vals.begin(), fn.generic_vals.end(), value) !=
            fn.generic_vals.end() ||
        std::find(fn.params.begin(), fn.params.end(), value) != fn.params.end())
      return id;
  }
  return detail::none;
}

std::size_t op_index(const detail::Store& store, std::uint32_t blk,
                     std::uint32_t op) {
  const auto& ops = store.blks[blk].data.ops;
  const auto found = std::find(ops.begin(), ops.end(), op);
  return found == ops.end() ? ops.size()
                            : static_cast<std::size_t>(found - ops.begin());
}

bool blk_within(const detail::Store& store, std::uint32_t child,
                std::uint32_t ancestor) {
  while (child != detail::none) {
    if (child == ancestor)
      return true;
    const auto parent = store.blks[child].data.parent_op;
    child =
        parent == detail::none ? detail::none : store.ops[parent].data.blk;
  }
  return false;
}

}  // namespace

bool detail::dominates(const detail::Store& store, std::uint32_t value,
                       std::uint32_t use) {
  const detail::ValData& val = store.vals[value].data;
  const std::uint32_t use_blk = store.ops[use].data.blk;
  if (val.kind == detail::ValKind::generic ||
      val.kind == detail::ValKind::param)
    return arg_fn(store, value) == store.blks[use_blk].data.fn;
  if (val.kind == detail::ValKind::blk_arg) {
    const std::uint32_t def_blk = arg_blk(store, value);
    return def_blk != detail::none &&
           blk_within(store, use_blk, def_blk);
  }
  if (val.def == detail::none || val.def >= store.ops.size())
    return false;
  const std::uint32_t def_blk = store.ops[val.def].data.blk;
  if (def_blk == use_blk)
    return op_index(store, def_blk, val.def) <
           op_index(store, use_blk, use);

  std::uint32_t child = use_blk;
  while (child != detail::none) {
    const std::uint32_t parent = store.blks[child].data.parent_op;
    if (parent == detail::none)
      return false;
    const std::uint32_t parent_blk = store.ops[parent].data.blk;
    if (parent_blk == def_blk)
      return op_index(store, def_blk, val.def) <
             op_index(store, parent_blk, parent);
    child = parent_blk;
  }
  return false;
}

namespace {

using Bindings = std::map<std::string, Ty, std::less<>>;

bool generic(const std::vector<std::string>& names, std::string_view name) {
  return std::find(names.begin(), names.end(), name) != names.end();
}

bool sized_integer(std::string_view name) {
  if (name.size() < 2 || (name.front() != 'i' && name.front() != 'u'))
    return false;
  return std::all_of(name.begin() + 1, name.end(), [](char ch) {
    return std::isdigit(static_cast<unsigned char>(ch));
  });
}

bool index_integer(std::string_view left, std::string_view right) {
  return (left == "index" && (right == "int" || sized_integer(right))) ||
         (right == "index" && (left == "int" || sized_integer(left)));
}

bool merge_binding(Ty& bound, const Ty& actual) {
  if (bound == actual || actual.name() == "_")
    return true;
  if (bound.name() == "_") {
    bound = actual;
    return true;
  }
  if (bound.name() == "int" && sized_integer(actual.name())) {
    bound = actual;
    return true;
  }
  return actual.name() == "int" && sized_integer(bound.name());
}

bool unify(const Ty& formal, const Ty& actual,
           const std::vector<std::string>& generics, Bindings& bindings) {
  if (formal.empty() || actual.empty() || formal.name() == "_" ||
      formal.name() == "Attr" || actual.name() == "_" ||
      actual.name() == "Attr")
    return true;
  if (formal.args().empty() && generic(generics, formal.name())) {
    const auto found = bindings.find(std::string(formal.name()));
    if (found == bindings.end()) {
      bindings.emplace(std::string(formal.name()), actual);
      return true;
    }
    return merge_binding(found->second, actual);
  }
  if ((formal.name() == "int" && sized_integer(actual.name())) ||
      (actual.name() == "int" && sized_integer(formal.name())) ||
      index_integer(formal.name(), actual.name()))
    return true;
  if (formal.name() != actual.name())
    return false;
  if (formal.args().empty() || actual.args().empty())
    return true;
  if (formal.args().size() != actual.args().size())
    return false;
  for (std::size_t index = 0; index < formal.args().size(); ++index)
    if (!unify(formal.args()[index], actual.args()[index], generics, bindings))
      return false;
  return true;
}

std::size_t specificity(const Ty& type,
                        const std::vector<std::string>& generics) {
  if (type.name() == "_" ||
      (type.args().empty() && generic(generics, type.name())))
    return 0;
  std::size_t score = 1;
  for (const Ty& arg : type.args())
    score += specificity(arg, generics);
  return score;
}

Ty substitute(const Ty& type, const std::vector<std::string>& generics,
              const Bindings& bindings) {
  if (type.args().empty() && generic(generics, type.name())) {
    const auto found = bindings.find(std::string(type.name()));
    return found == bindings.end() ? Ty("_") : found->second;
  }
  if (type.args().empty())
    return type;
  std::string text = type.name() == "[]" ? "[" : std::string(type.name()) + '<';
  for (std::size_t index = 0; index < type.args().size(); ++index) {
    if (index)
      text += ", ";
    text += substitute(type.args()[index], generics, bindings).text();
  }
  text += type.name() == "[]" ? ']' : '>';
  return Ty(std::move(text));
}

bool integer_term(std::string_view text) {
  if (text.empty())
    return false;
  std::size_t index = text.front() == '-' || text.front() == '+' ? 1 : 0;
  if (index == text.size())
    return false;
  return std::all_of(
      text.begin() + static_cast<std::ptrdiff_t>(index), text.end(),
      [](char ch) { return std::isdigit(static_cast<unsigned char>(ch)); });
}

Ty term_kind(const Ty& term, std::span<const GenericInfo> context) {
  if (term.args().empty()) {
    if (term.name() == "_")
      return Ty("_");
    for (const GenericInfo& generic : context)
      if (generic.name == term.name())
        return generic.type;
    if (integer_term(term.name()))
      return Ty("int");
    if (term.name() == "true" || term.name() == "false")
      return Ty("bool");
    return Ty("Ty");
  }
  if (term.name() != "[]")
    return Ty("Ty");
  Ty element("_");
  if (!term.args().empty()) {
    element = term_kind(term.args().front(), context);
    for (std::size_t index = 1; index < term.args().size(); ++index)
      if (term_kind(term.args()[index], context) != element)
        element = Ty("_");
  }
  return Ty("list<" + std::string(element.text()) + ">");
}

bool accepts_kind(const Ty& expected, const Ty& actual) {
  if (expected.text() == "_" || expected.text() == "Attr" ||
      expected.text() == "meta" || actual.text() == "_")
    return true;
  if (expected.name() == "list" && expected.args().size() == 1 &&
      actual.name() == "list" && actual.args().size() == 1)
    return accepts_kind(expected.args().front(), actual.args().front());
  return expected == actual;
}

bool accepts_term(const Ty& expected, const Ty& term,
                  std::span<const GenericInfo> context = {}) {
  return accepts_kind(expected, term_kind(term, context));
}

Fn select_overload(std::span<const Fn> candidates,
                   std::span<const Ty> arguments,
                   std::span<const Ty> explicit_arguments,
                   std::vector<Ty>* returns, bool* ambiguous,
                   std::span<const GenericInfo> context,
                   std::vector<Ty>* resolved_generics = nullptr,
                   std::span<const Ty> expected_returns = {}) {
  Fn best;
  std::vector<Ty> best_returns;
  std::vector<Ty> best_generics;
  std::size_t best_score = 0;
  bool tied = false;
  for (const Fn candidate : candidates) {
    const std::vector<Val> params = candidate.params();
    const std::vector<Ty> candidate_returns = candidate.returns();
    const std::vector<GenericInfo> info = generic_info(candidate);
    const std::vector<std::string> generics = generic_names(info);
    if (params.size() != arguments.size() ||
        (!expected_returns.empty() &&
         candidate_returns.size() != expected_returns.size()) ||
        (!explicit_arguments.empty() &&
         explicit_arguments.size() != generics.size()))
      continue;
    Bindings bindings;
    for (std::size_t index = 0; index < explicit_arguments.size(); ++index)
      bindings.emplace(generics[index], explicit_arguments[index]);
    bool matches = true;
    std::size_t score = 0;
    for (std::size_t index = 0; index < params.size(); ++index) {
      const Ty formal = params[index].type();
      score += specificity(formal, generics);
      if (!unify(formal, arguments[index], generics, bindings)) {
        matches = false;
        break;
      }
    }
    for (std::size_t index = 0;
         matches && index < expected_returns.size(); ++index) {
      if (expected_returns[index].text() == "_")
        continue;
      const Ty& formal = candidate_returns[index];
      Bindings inferred = bindings;
      if (unify(formal, expected_returns[index], generics, inferred))
        bindings = std::move(inferred);
    }
    for (std::size_t index = 0; matches && index < info.size(); ++index) {
      const auto bound = bindings.find(std::string(info[index].name));
      if (bound != bindings.end() &&
          !accepts_term(info[index].type, bound->second, context))
        matches = false;
    }
    if (!matches)
      continue;
    score =
        score * 1024 + (1023 - std::min<std::size_t>(generics.size(), 1023));
    std::vector<Ty> substituted;
    for (const Ty& type : candidate_returns)
      substituted.push_back(substitute(type, generics, bindings));
    std::vector<Ty> bound_generics;
    bound_generics.reserve(generics.size());
    for (const std::string& generic : generics) {
      const auto bound = bindings.find(generic);
      bound_generics.push_back(bound == bindings.end() ? Ty("_")
                                                       : bound->second);
    }
    if (!best || score > best_score) {
      best = candidate;
      best_returns = std::move(substituted);
      best_generics = std::move(bound_generics);
      best_score = score;
      tied = false;
    } else if (score == best_score)
      tied = true;
  }
  if (ambiguous)
    *ambiguous = tied;
  if (!best || tied)
    return {};
  if (returns)
    *returns = std::move(best_returns);
  if (resolved_generics)
    *resolved_generics = std::move(best_generics);
  return best;
}

std::vector<Fn> declarations(const Mod& mod, const Env& env,
                             std::string_view callee,
                             std::vector<Ty>& explicit_args,
                             std::string& symbol) {
  if (callee == "base.list")
    return {};
  const Ty applied{std::string(callee)};
  symbol = callee;
  if (!applied.args().empty()) {
    symbol = applied.name();
    explicit_args = applied.args();
  }
  return env.resolve_fns(mod, symbol);
}

void infer_list(detail::Store& store, detail::OpData& op) {
  if (op.args.empty() && !op.outs.empty()) {
    detail::ValData& out = store.vals[op.outs.front()].data;
    if (out.type_annotation && out.type.name() == "list" &&
        out.type.args().size() == 1)
      return;
  }
  Ty element("_");
  if (!op.args.empty()) {
    element = store.vals[op.args.front()].data.type;
    for (std::size_t index = 1; index < op.args.size(); ++index)
      if (store.vals[op.args[index]].data.type != element)
        element = Ty("_");
  }
  if (!op.outs.empty())
    store.vals[op.outs.front()].data.type =
        Ty("list<" + std::string(element.text()) + ">");
}

Ty return_context(const detail::Store& store, std::uint32_t value) {
  for (const std::uint32_t user : store.vals[value].data.users) {
    if (user >= store.ops.size() || !store.ops[user].live)
      continue;
    const detail::OpData& op = store.ops[user].data;
    if (op.kind != Op::Kind::ret || op.blk >= store.blks.size())
      continue;
    const std::uint32_t owner = store.blks[op.blk].data.fn;
    if (owner >= store.fns.size() || !store.fns[owner].live)
      continue;
    const std::vector<Ty>& returns = store.fns[owner].data.returns;
    for (std::size_t index = 0;
         index < op.args.size() && index < returns.size(); ++index)
      if (op.args[index] == value)
        return returns[index];
  }
  return Ty("_");
}

void infer_call(detail::Store& store, const Mod& mod, const Env& env,
                detail::OpData& op, bool diagnose) {
  if (op.callee == "base.copy" && op.args.size() == 1 && op.outs.size() == 1) {
    store.vals[op.outs.front()].data.type =
        store.vals[op.args.front()].data.type;
    return;
  }
  if (op.callee == "base.list") {
    infer_list(store, op);
    return;
  }
  if (op.callee == "operator []" && op.args.size() == 2 && !op.outs.empty()) {
    const Ty& container = store.vals[op.args.front()].data.type;
    if (container.name() == "list" && container.args().size() == 1) {
      store.vals[op.outs.front()].data.type = container.args().front();
      return;
    }
  }

  std::vector<Ty> explicit_args;
  std::string symbol;
  const std::vector<Fn> candidates =
      declarations(mod, env, op.callee, explicit_args, symbol);
  if (candidates.empty()) {
    if (diagnose) {
      const std::vector<Fn> hidden = env.find_fns(symbol);
      if (!hidden.empty())
        detail::add_diag(store.diags,
                         "call to '" + std::string(op.callee) +
                             "' requires 'use " +
                             std::string(hidden.front().module()) + "'",
                         op.loc);
    }
    return;
  }
  std::vector<Ty> arguments;
  arguments.reserve(op.args.size());
  for (const std::uint32_t argument : op.args)
    arguments.push_back(store.vals[argument].data.type);
  std::vector<Ty> returns;
  std::vector<Ty> expected_returns;
  expected_returns.reserve(op.outs.size());
  for (const std::uint32_t output : op.outs) {
    const detail::ValData& value = store.vals[output].data;
    expected_returns.push_back(value.type_annotation
                                   ? value.type
                                   : return_context(store, output));
  }
  bool ambiguous = false;
  const std::uint32_t owner = store.blks[op.blk].data.fn;
  const std::vector<GenericInfo> context =
      owner == detail::none ? std::vector<GenericInfo>{}
                            : generic_info(store, store.fns[owner].data);
  const Fn fn = select_overload(candidates, arguments, explicit_args, &returns,
                                &ambiguous, context, nullptr,
                                expected_returns);
  if (!fn) {
    if (diagnose) {
      std::string message;
      if (ambiguous)
        message = "call to '" + std::string(op.callee) + "' is ambiguous";
      else if (candidates.size() == 1) {
        const Fn candidate = candidates.front();
        const std::vector<Val> params = candidate.params();
        const std::vector<std::string> generics =
            generic_names(generic_info(candidate));
        if (params.size() != arguments.size())
          message = "call to '" + std::string(op.callee) + "' expects " +
                    std::to_string(params.size()) + " arguments, got " +
                    std::to_string(arguments.size());
        else if (!explicit_args.empty() &&
                 explicit_args.size() != generics.size())
          message = "call to '" + std::string(op.callee) + "' expects " +
                    std::to_string(generics.size()) +
                    " generic arguments, got " +
                    std::to_string(explicit_args.size());
        else {
          Bindings bindings;
          for (std::size_t index = 0; index < explicit_args.size(); ++index)
            bindings.emplace(generics[index], explicit_args[index]);
          for (std::size_t index = 0; index < params.size(); ++index) {
            const Ty formal = params[index].type();
            if (unify(formal, arguments[index], generics, bindings))
              continue;
            message =
                "argument " + std::to_string(index + 1) + " of '" +
                std::string(op.callee) + "' has type '" +
                std::string(arguments[index].text()) + "', expected '" +
                std::string(substitute(formal, generics, bindings).text()) +
                "'";
            break;
          }
          const std::vector<GenericInfo> info = generic_info(candidate);
          for (std::size_t index = 0; message.empty() && index < info.size();
               ++index) {
            const auto bound = bindings.find(std::string(info[index].name));
            if (bound == bindings.end() ||
                accepts_term(info[index].type, bound->second, context))
              continue;
            message = "generic argument '" + std::string(info[index].name) +
                      "' of '" + std::string(op.callee) + "' has type '" +
                      std::string(term_kind(bound->second, context).text()) +
                      "', expected '" + std::string(info[index].type.text()) +
                      "'";
          }
        }
      }
      if (message.empty())
        message = "no overload of '" + std::string(op.callee) +
                  "' accepts the argument types";
      detail::add_diag(store.diags, std::move(message), op.loc);
    }
    return;
  }
  if (returns.size() != op.outs.size()) {
    if (diagnose)
      detail::add_diag(store.diags,
                       "result count of '" + std::string(op.callee) +
                           "' does not match its declaration",
                       op.loc);
    return;
  }
  for (std::size_t index = 0; index < returns.size(); ++index) {
    detail::ValData& result = store.vals[op.outs[index]].data;
    Ty& type = result.type;
    if (!result.type_annotation || type.empty() || type.text() == "_")
      type = returns[index];
    else if (!returns[index].empty() && returns[index].text() != "_" &&
             type != returns[index] && diagnose)
      detail::add_diag(store.diags,
                       "result " + std::to_string(index + 1) + " of '" +
                           std::string(op.callee) + "' has declared type '" +
                           std::string(type.text()) + "', expected '" +
                           std::string(returns[index].text()) + "'",
                       op.loc);
  }
}

void infer_regions(detail::Store& store, const detail::OpData& op) {
  if (op.kind == Op::Kind::loop && op.blks.size() == 1) {
    auto& args = store.blks[op.blks.front()].data.args;
    for (std::size_t index = 0; index < op.iter_names.size(); ++index) {
      const Ty& source = store.vals[op.args[index]].data.type;
      store.vals[args[index]].data.type =
          source.name() == "list" && source.args().size() == 1
              ? source.args().front()
          : source.name() == "range" ? Ty("index")
                                     : Ty("_");
    }
    for (std::size_t index = 0; index < op.carried_count; ++index) {
      const Ty type =
          store.vals[op.args[op.iter_names.size() + index]].data.type;
      store.vals[args[op.iter_names.size() + index]].data.type = type;
      store.vals[op.outs[index]].data.type = type;
    }
  } else if (op.kind == Op::Kind::branch) {
    for (const std::uint32_t blk : op.blks)
      for (std::size_t index = 0; index < op.carried_count; ++index)
        store.vals[store.blks[blk].data.args[index]].data.type =
            store.vals[op.args[index + 1]].data.type;
    for (std::size_t index = 0; index < op.carried_count; ++index)
      store.vals[op.outs[index]].data.type =
          store.vals[op.args[index + 1]].data.type;
  }
}

bool intrinsic_type(std::string_view name) {
  static constexpr std::string_view names[] = {
      "_",    "nil", "bool",  "int",  "index", "f16",   "f32",
      "f64",  "str", "bytes", "dict", "list",  "range", "Ty",
      "Attr", "Mod", "Fn",    "Blk",  "Op",    "Val",   "meta"};
  for (const std::string_view intrinsic : names)
    if (intrinsic == name)
      return true;
  if (name.size() < 2 || (name.front() != 'i' && name.front() != 'u'))
    return false;
  return std::all_of(name.begin() + 1, name.end(), [](char ch) {
    return std::isdigit(static_cast<unsigned char>(ch));
  });
}

struct TypeLookup {
  Fn constructor;
  bool seen = false;
  bool ambiguous = false;
  std::vector<std::size_t> arities;
  std::optional<std::size_t> mismatch;
  Ty expected;
  Ty actual;
};

TypeLookup type_declaration(const Mod& mod, const Env& env, const Ty& type,
                            std::span<const GenericInfo> context) {
  std::vector<Fn> candidates = mod.find_fns(type.name());
  if (candidates.empty()) {
    const std::string symbol =
        type.name().find('.') == std::string_view::npos
            ? std::string(type.name()) + "." + std::string(type.name())
            : std::string(type.name());
    candidates = env.resolve_fns(mod, symbol);
  }
  TypeLookup result;
  result.seen = !candidates.empty();
  for (const Fn candidate : candidates) {
    const std::vector<Ty> returns = candidate.returns();
    if (!candidate.params().empty() || returns.size() != 1 ||
        returns.front().name() != "Ty")
      continue;
    result.arities.push_back(candidate.generics().size());
    if (candidate.generics().size() != type.args().size())
      continue;
    const std::vector<Val> generics = candidate.generics();
    bool compatible = true;
    for (std::size_t index = 0; index < generics.size(); ++index) {
      const Ty expected = generics[index].type();
      if (accepts_term(expected, type.args()[index], context))
        continue;
      if (!result.mismatch) {
        result.mismatch = index;
        result.expected = expected;
        result.actual = term_kind(type.args()[index], context);
      }
      compatible = false;
      break;
    }
    if (!compatible)
      continue;
    if (result.constructor) {
      result.constructor = {};
      result.ambiguous = true;
      return result;
    }
    result.constructor = candidate;
  }
  return result;
}

bool verify_type(detail::Store& store, const Mod& mod, const Env& env,
                 const Ty& type, const std::vector<std::string>& generics,
                 std::span<const GenericInfo> context, Loc loc) {
  if (!type.valid()) {
    detail::add_diag(store.diags,
                     "malformed type '" + std::string(type.text()) + "'",
                     std::move(loc));
    return false;
  }
  if (type.args().empty() &&
      (intrinsic_type(type.name()) || generic(generics, type.name())))
    return true;
  if (type.name() == "[]")
    return true;
  if (type.name() == "list") {
    if (type.args().size() != 1) {
      detail::add_diag(store.diags, "type 'list' expects 1 argument",
                       std::move(loc));
      return false;
    }
    return verify_type(store, mod, env, type.args().front(), generics, context,
                       std::move(loc));
  }

  const TypeLookup lookup = type_declaration(mod, env, type, context);
  if (!lookup.constructor) {
    if (lookup.ambiguous) {
      detail::add_diag(store.diags,
                       "type constructor '" + std::string(type.name()) +
                           "' is ambiguous",
                       std::move(loc));
      return false;
    }
    if (lookup.seen) {
      if (lookup.mismatch)
        detail::add_diag(
            store.diags,
            "type argument " + std::to_string(*lookup.mismatch + 1) + " of '" +
                std::string(type.name()) + "' has type '" +
                std::string(lookup.actual.text()) + "', expected '" +
                std::string(lookup.expected.text()) + "'",
            std::move(loc));
      else if (lookup.arities.size() == 1)
        detail::add_diag(store.diags,
                         "type '" + std::string(type.name()) + "' expects " +
                             std::to_string(lookup.arities.front()) +
                             (lookup.arities.front() == 1
                                  ? " type argument, got "
                                  : " type arguments, got ") +
                             std::to_string(type.args().size()),
                         std::move(loc));
      else
        detail::add_diag(store.diags,
                         lookup.arities.empty()
                             ? "function family '" + std::string(type.name()) +
                                   "' has no type-constructor overload"
                             : "no type-constructor overload of '" +
                                   std::string(type.name()) + "' accepts " +
                                   std::to_string(type.args().size()) +
                                   " arguments",
                         std::move(loc));
      return false;
    }
    std::string symbol(type.name());
    if (symbol.find('.') == std::string::npos)
      symbol += "." + symbol;
    const std::vector<Fn> hidden = env.find_fns(symbol);
    if (hidden.empty())
      return true;
    detail::add_diag(store.diags,
                     "type '" + std::string(type.name()) + "' requires 'use " +
                         std::string(hidden.front().module()) + "'",
                     std::move(loc));
    return false;
  }
  const std::vector<Val> constructor_generics = lookup.constructor.generics();
  const auto verify_argument = [&](auto&& self, const Ty& expected,
                                   const Ty& argument) -> bool {
    if (expected.name() == "Ty")
      return verify_type(store, mod, env, argument, generics, context, loc);
    if (expected.name() != "list" || expected.args().size() != 1 ||
        argument.name() != "[]")
      return true;
    for (const Ty& item : argument.args())
      if (!self(self, expected.args().front(), item))
        return false;
    return true;
  };
  for (std::size_t index = 0; index < constructor_generics.size(); ++index)
    if (!verify_argument(verify_argument, constructor_generics[index].type(),
                         type.args()[index]))
      return false;
  return true;
}

}  // namespace

Fn detail::resolve_overload(std::span<const Fn> candidates,
                            std::span<const Ty> arguments,
                            std::span<const Ty> explicit_arguments,
                            std::vector<Ty>* returns, bool* ambiguous,
                            std::span<const Val> context,
                            std::vector<Ty>* generics,
                            std::span<const Ty> expected_returns) {
  std::vector<GenericInfo> info;
  info.reserve(context.size());
  for (const Val generic : context)
    info.push_back({generic.name(), generic.type()});
  return select_overload(candidates, arguments, explicit_arguments, returns,
                         ambiguous, info, generics, expected_returns);
}

bool Mod::verify(const Env& env) {
  detail::Store& store = impl_->store;
  store.diags.clear();
  detail::rebuild_uses(store);
  if (store.name.empty())
    detail::add_diag(store.diags, "module has no name");
  for (const auto& fn_slot : store.fns) {
    if (!fn_slot.live)
      continue;
    const detail::FnData& fn = fn_slot.data;
    const std::vector<GenericInfo> context = generic_info(store, fn);
    const std::vector<std::string> generics = generic_names(context);
    for (const std::uint32_t generic : fn.generic_vals)
      verify_type(store, *this, env, store.vals[generic].data.type, generics,
                  context, fn.loc);
    for (const std::uint32_t param : fn.params)
      verify_type(store, *this, env, store.vals[param].data.type, generics,
                  context, fn.loc);
    for (const Ty& type : fn.returns)
      verify_type(store, *this, env, type, generics, context, fn.loc);
  }
  for (std::size_t iteration = 0; iteration <= store.ops.size(); ++iteration) {
    std::vector<Ty> before;
    before.reserve(store.vals.size());
    for (const auto& value : store.vals)
      before.push_back(value.data.type);
    for (auto& op : store.ops) {
      if (!op.live)
        continue;
      if (op.data.kind == Op::Kind::call)
        infer_call(store, *this, env, op.data, false);
      infer_regions(store, op.data);
    }
    bool stable = before.size() == store.vals.size();
    for (std::size_t index = 0; stable && index < before.size(); ++index)
      stable = before[index] == store.vals[index].data.type;
    if (stable)
      break;
  }
  for (auto& op : store.ops)
    if (op.live && op.data.kind == Op::Kind::call)
      infer_call(store, *this, env, op.data, true);
  for (const auto& fn_slot : store.fns) {
    if (!fn_slot.live || fn_slot.data.external)
      continue;
    const detail::FnData& fn = fn_slot.data;
    if (fn.blks.empty()) {
      detail::add_diag(store.diags, "function '" + fn.name + "' has no body",
                       fn.loc);
      continue;
    }
    const auto& body_ops = store.blks[fn.blks.front()].data.ops;
    if (body_ops.empty() ||
        store.ops[body_ops.back()].data.kind != Op::Kind::ret) {
      detail::add_diag(store.diags,
                       "function '" + fn.name + "' must end with return",
                       fn.loc);
      continue;
    }
    const detail::OpData& ret = store.ops[body_ops.back()].data;
    if (ret.args.size() != fn.returns.size())
      detail::add_diag(store.diags,
                       "return arity does not match function signature",
                       ret.loc);
    for (std::size_t index = 0;
         index < std::min(ret.args.size(), fn.returns.size()); ++index) {
      const Ty& actual = store.vals[ret.args[index]].data.type;
      const Ty& expected = fn.returns[index];
      if (!actual.empty() && actual.text() != "_" && !expected.empty() &&
          expected.text() != "_" && actual != expected)
        detail::add_diag(store.diags,
                         "return type '" + std::string(actual.text()) +
                             "' does not match '" +
                             std::string(expected.text()) + "'",
                         ret.loc);
    }
  }
  for (const auto& blk_slot : store.blks) {
    if (!blk_slot.live || blk_slot.data.parent_op == detail::none)
      continue;
    const auto& ops = blk_slot.data.ops;
    if (ops.empty() || store.ops[ops.back()].data.kind != Op::Kind::yield) {
      detail::add_diag(store.diags, "nested block must end with yield");
      continue;
    }
    const auto& parent = store.ops[blk_slot.data.parent_op].data;
    if (store.ops[ops.back()].data.args.size() != parent.carried_count)
      detail::add_diag(store.diags,
                       "yield arity does not match carried values");
  }
  for (std::uint32_t op_id = 0; op_id < store.ops.size(); ++op_id) {
    const auto& op_slot = store.ops[op_id];
    if (!op_slot.live)
      continue;
    const detail::OpData& op = op_slot.data;
    if (op.blk >= store.blks.size() || !store.blks[op.blk].live)
      detail::add_diag(store.diags, "operation has an invalid parent block",
                       op.loc);
    if (op.kind == Op::Kind::call && op.callee.empty())
      detail::add_diag(store.diags, "call has no callee", op.loc);
    if ((op.kind == Op::Kind::call || op.kind == Op::Kind::constant) &&
        op.outs.size() > 1) {
      if (op.form == detail::Form::hidden)
        detail::add_diag(store.diags,
                         "multi-result operation must have named bindings",
                         op.loc);
      else if (std::any_of(op.outs.begin(), op.outs.end(),
                           [&](std::uint32_t id) {
                             return id >= store.vals.size() ||
                                    store.vals[id].data.name.empty();
                           }))
        detail::add_diag(store.diags,
                         "multi-result operation has an unnamed result",
                         op.loc);
    }
    const auto blk_args = [&](std::size_t index, std::size_t count) {
      if (index >= op.blks.size() || op.blks[index] >= store.blks.size())
        return false;
      const auto& child = store.blks[op.blks[index]];
      return child.live && child.data.parent_op == op_id &&
             child.data.args.size() == count;
    };
    if (op.kind == Op::Kind::loop &&
        (op.blks.size() != 1 ||
         op.args.size() != op.iter_names.size() + op.carried_count ||
         op.outs.size() != op.carried_count ||
         !blk_args(0, op.iter_names.size() + op.carried_count)))
      detail::add_diag(store.diags, "loop structure is inconsistent", op.loc);
    if (op.kind == Op::Kind::branch &&
        (op.blks.size() != 2 || op.args.size() != 1 + op.carried_count ||
         op.outs.size() != op.carried_count ||
         !blk_args(0, op.carried_count) || !blk_args(1, op.carried_count)))
      detail::add_diag(store.diags, "if structure is inconsistent", op.loc);
    for (const auto arg : op.args) {
      if (arg >= store.vals.size() || !store.vals[arg].live) {
        detail::add_diag(store.diags, "operation uses an invalid value",
                         op.loc);
        continue;
      }
      if (!detail::dominates(store, arg, op_id))
        detail::add_diag(store.diags, "value does not dominate its use",
                         op.loc);
    }
    for (std::size_t index = 0; index < op.outs.size(); ++index) {
      const auto value = op.outs[index];
      if (value >= store.vals.size() || !store.vals[value].live ||
          store.vals[value].data.def != op_id ||
          store.vals[value].data.index != index)
        detail::add_diag(store.diags, "operation result is inconsistent",
                         op.loc);
    }
  }
  return store.diags.empty();
}

}  // namespace joggle
