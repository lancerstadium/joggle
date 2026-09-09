#include "detail.h"

#include <algorithm>
#include <charconv>
#include <cctype>
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
          pair == "*=" || pair == "/=" || pair == "==" || pair == "!=" ||
          pair == "<=" || pair == ">=" || pair == "&&" || pair == "||" ||
          pair == "<<" || pair == ">>")
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

std::string operator_name(std::string_view spelling) {
  return "operator " + std::string(spelling);
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
    out << *item;
    return out.str();
  }
  if (const auto item = value.string()) {
    std::string out = "\"";
    for (const char ch : *item) {
      if (ch == '"' || ch == '\\')
        out.push_back('\\');
      out.push_back(ch);
    }
    return out + '"';
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
      bool host = false;
      if (match("[")) {
        if (!word("host") || !expect("]"))
          return false;
        host = true;
      }
      if (!parse_fn(host))
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

  std::uint32_t add_val(detail::ValData data) {
    const auto id = static_cast<std::uint32_t>(store_.vals.size());
    store_.vals.push_back({std::move(data), 1, true});
    return id;
  }

  std::uint32_t add_block(std::uint32_t fn, std::uint32_t parent) {
    const auto id = static_cast<std::uint32_t>(store_.blocks.size());
    detail::BlkData data;
    data.fn = fn;
    data.parent_op = parent;
    store_.blocks.push_back({std::move(data), 1, true});
    store_.fns[fn].data.blocks.push_back(id);
    return id;
  }

  std::uint32_t add_op(std::uint32_t block, detail::OpData data,
                       std::vector<std::pair<std::string, Ty>> results = {}) {
    const auto id = static_cast<std::uint32_t>(store_.ops.size());
    data.block = block;
    for (std::size_t index = 0; index < results.size(); ++index) {
      detail::ValData value;
      value.name = std::move(results[index].first);
      value.type = std::move(results[index].second);
      value.def = id;
      value.index = index;
      data.outs.push_back(add_val(std::move(value)));
    }
    store_.ops.push_back({std::move(data), 1, true});
    store_.blocks[block].data.ops.push_back(id);
    return id;
  }

  std::uint32_t add_call(std::uint32_t block, std::string callee,
                         std::vector<std::uint32_t> args, Ty type,
                         Loc loc = {}) {
    detail::OpData data;
    data.kind = Op::Kind::call;
    data.callee = std::move(callee);
    data.args = std::move(args);
    data.loc = std::move(loc);
    const auto op = add_op(block, std::move(data), {{"", std::move(type)}});
    return store_.ops[op].data.outs.front();
  }

  std::uint32_t add_const(std::uint32_t block, Attr value, Ty type, Loc loc) {
    detail::OpData data;
    data.kind = Op::Kind::constant;
    data.literal = std::move(value);
    data.loc = std::move(loc);
    const auto op = add_op(block, std::move(data), {{"", std::move(type)}});
    return store_.ops[op].data.outs.front();
  }

  void show(std::uint32_t value, detail::Form form, std::string name = {}) {
    auto& val = store_.vals[value].data;
    if (!name.empty())
      val.name = std::move(name);
    if (val.def != detail::none)
      store_.ops[val.def].data.form = form;
  }

  std::string type_text(std::string_view close) {
    std::string out;
    int angle = 0;
    int square = 0;
    int paren = 0;
    while (!at_end()) {
      const Token& token = peek();
      if (angle == 0 && square == 0 && paren == 0 &&
          (token.text == close || token.text == "," || token.text == "{" ||
           token.text == ";"))
        break;
      if (token.text == "<")
        ++angle;
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

  bool parse_fn(bool host) {
    if (!word("fn"))
      return fail("expected function declaration");
    const Loc loc = peek().loc;
    std::string name = take_name("function name");
    if (name.empty())
      return false;

    detail::FnData data;
    data.name = name;
    data.loc = loc;
    data.host = host;
    if (match("<")) {
      do {
        data.generics.push_back(take_name("generic parameter"));
        if (data.generics.back().empty())
          return false;
      } while (match(","));
      if (!expect(">"))
        return false;
    }
    if (!expect("("))
      return false;
    std::vector<std::pair<std::string, Ty>> params;
    if (!is(")")) {
      do {
        std::string param = take_name("parameter name");
        if (param.empty() || !expect(":"))
          return false;
        const std::string type = type_text(")");
        if (type.empty())
          return fail("expected parameter type");
        params.emplace_back(std::move(param), Ty(type));
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
          data.returns.emplace_back(type);
        } while (match(","));
      }
      if (!expect(")"))
        return false;
    } else {
      const std::string type = type_text("{");
      if (type.empty())
        return fail("expected return type");
      data.returns.emplace_back(type);
    }

    if (store_.symbols.contains(name))
      return fail("duplicate function '" + name + "'", loc);
    const auto fn = static_cast<std::uint32_t>(store_.fns.size());
    store_.fns.push_back({std::move(data), 1, true});
    store_.symbols.emplace(name, fn);

    Scope scope;
    for (const std::string& generic : store_.fns[fn].data.generics) {
      detail::ValData value;
      value.kind = detail::ValKind::generic;
      value.name = generic;
      value.type = Ty("meta");
      const auto id = add_val(std::move(value));
      store_.fns[fn].data.generic_vals.push_back(id);
      scope.emplace(generic, Binding{id, false});
    }
    for (auto& [param, type] : params) {
      detail::ValData value;
      value.kind = detail::ValKind::param;
      value.name = param;
      value.type = std::move(type);
      const auto id = add_val(std::move(value));
      store_.fns[fn].data.params.push_back(id);
      scope.emplace(param, Binding{id, false});
    }
    if (match(";")) {
      store_.fns[fn].data.external = true;
      return true;
    }
    if (!expect("{"))
      return false;
    const auto body = add_block(fn, detail::none);
    if (!parse_block(fn, body, scope, false) || !expect("}"))
      return false;
    semi();
    return true;
  }

  bool parse_block(std::uint32_t fn, std::uint32_t block, Scope& scope,
                   bool nested) {
    while (!at_end() && !is("}")) {
      if (word("let") || word("var")) {
        const bool mut = tokens_[pos_ - 1].text == "var";
        const detail::Form form = mut ? detail::Form::var : detail::Form::let;
        const std::string name = take_name("binding name");
        if (name.empty() || !expect("="))
          return false;
        std::uint32_t value = expression(block, scope);
        if (value == detail::none)
          return false;
        if (store_.vals[value].data.def == detail::none ||
            store_.ops[store_.vals[value].data.def].data.form !=
                detail::Form::hidden)
          value = add_call(block, "base.copy", {value},
                           store_.vals[value].data.type, peek().loc);
        show(value, form, name);
        scope[name] = {value, mut};
        semi();
      } else if (word("for")) {
        if (!parse_for(fn, block, scope))
          return false;
      } else if (word("if")) {
        if (!parse_if(fn, block, scope))
          return false;
      } else if (word("return")) {
        detail::OpData data;
        data.kind = Op::Kind::ret;
        data.loc = tokens_[pos_ - 1].loc;
        if (!is(";") && !is("}")) {
          do {
            const auto value = expression(block, scope);
            if (value == detail::none)
              return false;
            data.args.push_back(value);
          } while (match(","));
        }
        add_op(block, std::move(data));
        semi();
      } else if (!parse_assignment(block, scope))
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

  bool parse_for(std::uint32_t fn, std::uint32_t block, Scope& scope) {
    detail::OpData data;
    data.kind = Op::Kind::loop;
    data.loc = tokens_[pos_ - 1].loc;
    do {
      const std::string iter = take_name("loop variable");
      if (iter.empty() || !word("in"))
        return fail("expected 'in' after loop variable");
      const auto lower = expression(block, scope);
      if (lower == detail::none || !expect(".."))
        return false;
      const auto upper = expression(block, scope);
      if (upper == detail::none)
        return false;
      data.iter_names.push_back(iter);
      data.args.push_back(lower);
      data.args.push_back(upper);
    } while (match(","));
    data.range_count = data.iter_names.size();

    const auto captures = carried(scope);
    data.carried_count = captures.size();
    std::vector<std::pair<std::string, Ty>> results;
    for (const auto& [name, binding] : captures) {
      data.args.push_back(binding.value);
      results.emplace_back(name, store_.vals[binding.value].data.type);
    }
    const auto op = add_op(block, std::move(data), std::move(results));
    const auto body = add_block(fn, op);
    store_.ops[op].data.blocks.push_back(body);

    Scope inner = scope;
    for (const std::string& iter : store_.ops[op].data.iter_names) {
      detail::ValData value;
      value.kind = detail::ValKind::block_arg;
      value.name = iter;
      value.type = Ty("index");
      const auto id = add_val(std::move(value));
      store_.blocks[body].data.args.push_back(id);
      inner[iter] = {id, false};
    }
    for (const auto& [name, binding] : captures) {
      detail::ValData value;
      value.kind = detail::ValKind::block_arg;
      value.name = name;
      value.type = store_.vals[binding.value].data.type;
      const auto id = add_val(std::move(value));
      store_.blocks[body].data.args.push_back(id);
      inner[name] = {id, true};
    }
    if (!expect("{") || !parse_block(fn, body, inner, true) || !expect("}"))
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

  bool parse_if(std::uint32_t fn, std::uint32_t block, Scope& scope) {
    detail::OpData data;
    data.kind = Op::Kind::branch;
    data.loc = tokens_[pos_ - 1].loc;
    const auto condition = expression(block, scope);
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
    const auto op = add_op(block, std::move(data), std::move(results));

    auto arm = [&](bool present) {
      const auto body = add_block(fn, op);
      store_.ops[op].data.blocks.push_back(body);
      Scope inner = scope;
      for (const auto& [name, binding] : captures) {
        detail::ValData value;
        value.kind = detail::ValKind::block_arg;
        value.name = name;
        value.type = store_.vals[binding.value].data.type;
        const auto id = add_val(std::move(value));
        store_.blocks[body].data.args.push_back(id);
        inner[name] = {id, true};
      }
      if (present && !parse_block(fn, body, inner, true))
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

  bool parse_assignment(std::uint32_t block, Scope& scope) {
    const std::size_t start = pos_;
    if (peek().kind == Tk::name &&
        (peek(1).text == "=" || peek(1).text == "+=")) {
      const Token name = take();
      const std::string assignment = take().text;
      auto found = scope.find(name.text);
      if (found == scope.end())
        return fail("unknown name '" + name.text + "'", name.loc);
      if (!found->second.mutable_value)
        return fail("cannot assign to immutable '" + name.text + "'", name.loc);
      const auto rhs = expression(block, scope);
      if (rhs == detail::none)
        return false;
      std::uint32_t value = rhs;
      detail::Form form = detail::Form::assign;
      if (assignment == "+=") {
        value = add_call(block, operator_name("+"), {found->second.value, rhs},
                         store_.vals[found->second.value].data.type, name.loc);
        form = detail::Form::add_assign;
      } else
        value = add_call(block, "base.copy", {rhs},
                         store_.vals[found->second.value].data.type, name.loc);
      show(value, form, name.text);
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
          const auto index = expression(block, scope);
          if (index == detail::none)
            return false;
          args.push_back(index);
        } while (match(","));
      }
      if (!expect("]") || !expect("="))
        return false;
      const auto rhs = expression(block, scope);
      if (rhs == detail::none)
        return false;
      args.push_back(rhs);
      const auto value =
          add_call(block, operator_name("[]="), std::move(args),
                   store_.vals[found->second.value].data.type, base.loc);
      show(value, detail::Form::index_assign, base.text);
      found->second.value = value;
      semi();
      return true;
    }

    pos_ = start;
    const auto value = expression(block, scope);
    if (value == detail::none)
      return false;
    show(value, detail::Form::expr);
    semi();
    return true;
  }

  std::uint32_t expression(std::uint32_t block, Scope& scope, int minimum = 0) {
    std::uint32_t left = unary(block, scope);
    if (left == detail::none)
      return left;
    for (;;) {
      const int level = precedence(peek().text);
      if (level < minimum)
        break;
      const Token op = take();
      const auto right = expression(block, scope, level + 1);
      if (right == detail::none)
        return right;
      Ty type = store_.vals[left].data.type;
      if (op.text == "==" || op.text == "!=" || op.text == "<" ||
          op.text == "<=" || op.text == ">" || op.text == ">=" ||
          op.text == "&&" || op.text == "||")
        type = Ty("bool");
      left =
          add_call(block, operator_name(op.text), {left, right}, type, op.loc);
    }
    return left;
  }

  std::uint32_t unary(std::uint32_t block, Scope& scope) {
    if (is("+") || is("-") || is("!") || is("~")) {
      const Token op = take();
      const auto arg = unary(block, scope);
      if (arg == detail::none)
        return arg;
      return add_call(block, operator_name(op.text), {arg},
                      store_.vals[arg].data.type, op.loc);
    }
    return postfix(block, scope);
  }

  std::uint32_t postfix(std::uint32_t block, Scope& scope) {
    std::uint32_t value = primary(block, scope);
    if (value == detail::none)
      return value;
    while (match("[")) {
      std::vector<std::uint32_t> args{value};
      if (!is("]")) {
        do {
          const auto index = expression(block, scope);
          if (index == detail::none)
            return index;
          args.push_back(index);
        } while (match(","));
      }
      if (!expect("]"))
        return detail::none;
      value = add_call(block, operator_name("[]"), std::move(args), Ty("_"));
    }
    return value;
  }

  std::string template_suffix() {
    if (!match("<"))
      return {};
    std::string out = "<";
    int depth = 1;
    while (!at_end() && depth > 0) {
      const std::string text = take().text;
      if (text == "<")
        ++depth;
      else if (text == ">")
        --depth;
      out += text == "," ? ", " : text;
    }
    if (depth != 0)
      fail("unterminated template argument list");
    return out;
  }

  std::uint32_t primary(std::uint32_t block, Scope& scope) {
    const Token token = take();
    if (token.kind == Tk::number) {
      if (token.text.find('.') != std::string::npos)
        return add_const(block, Attr(std::stod(token.text)), Ty("f64"),
                         token.loc);
      std::int64_t value = 0;
      std::from_chars(token.text.data(), token.text.data() + token.text.size(),
                      value);
      return add_const(block, Attr(value), Ty("int"), token.loc);
    }
    if (token.kind == Tk::string)
      return add_const(block, Attr(token.text), Ty("str"), token.loc);
    if (token.text == "true" || token.text == "false")
      return add_const(block, Attr(token.text == "true"), Ty("bool"),
                       token.loc);
    if (token.text == "(") {
      const auto value = expression(block, scope);
      return expect(")") ? value : detail::none;
    }
    if (token.kind != Tk::name) {
      fail("expected expression", token.loc);
      return detail::none;
    }

    std::string callee = token.text;
    if (is("<")) {
      const std::size_t save = pos_;
      const std::string suffix = template_suffix();
      if (is("("))
        callee += suffix;
      else
        pos_ = save;
    }
    if (match("(")) {
      std::vector<std::uint32_t> args;
      if (!is(")")) {
        do {
          const auto arg = expression(block, scope);
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
      return add_call(block, std::move(callee), std::move(args),
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

std::string render_value(const detail::Store& store, std::uint32_t value);

std::string render_call(const detail::Store& store, const detail::OpData& op) {
  if (op.callee.starts_with("operator ")) {
    const std::string_view symbol(op.callee.data() + 9, op.callee.size() - 9);
    if (symbol == "[]" && !op.args.empty()) {
      std::string out = render_value(store, op.args.front()) + "[";
      for (std::size_t index = 1; index < op.args.size(); ++index) {
        if (index != 1)
          out += ", ";
        out += render_value(store, op.args[index]);
      }
      return out + "]";
    }
    if (op.args.size() == 1)
      return std::string(symbol) + render_value(store, op.args[0]);
    if (op.args.size() == 2)
      return render_value(store, op.args[0]) + " " + std::string(symbol) + " " +
             render_value(store, op.args[1]);
  }
  std::string out = op.callee + "(";
  for (std::size_t index = 0; index < op.args.size(); ++index) {
    if (index)
      out += ", ";
    out += render_value(store, op.args[index]);
  }
  return out + ")";
}

std::string render_value(const detail::Store& store, std::uint32_t value) {
  if (value >= store.vals.size() || !store.vals[value].live)
    return "<invalid>";
  const detail::ValData& data = store.vals[value].data;
  if (data.def == detail::none)
    return data.name;
  const detail::OpData& op = store.ops[data.def].data;
  if (op.kind == Op::Kind::constant)
    return attr_text(op.literal);
  if (op.kind == Op::Kind::call && op.form == detail::Form::hidden)
    return render_call(store, op);
  return data.name.empty() ? "value" : data.name;
}

void indent(std::ostringstream& out, unsigned depth) {
  out << std::string(depth * 2, ' ');
}

void render_block(std::ostringstream& out, const detail::Store& store,
                  std::uint32_t block, unsigned depth) {
  for (const auto id : store.blocks[block].data.ops) {
    if (id >= store.ops.size() || !store.ops[id].live)
      continue;
    const detail::OpData& op = store.ops[id].data;
    if (((op.kind == Op::Kind::call || op.kind == Op::Kind::constant) &&
         op.form == detail::Form::hidden) ||
        op.kind == Op::Kind::yield)
      continue;
    indent(out, depth);
    if (op.kind == Op::Kind::call || op.kind == Op::Kind::constant) {
      const auto result = op.outs.empty() ? detail::none : op.outs[0];
      const std::string name =
          result == detail::none ? "" : store.vals[result].data.name;
      if (op.form == detail::Form::let || op.form == detail::Form::var) {
        out << (op.form == detail::Form::var ? "var " : "let ") << name << " = "
            << (op.kind == Op::Kind::constant ? attr_text(op.literal)
                                              : render_call(store, op));
      } else if (op.form == detail::Form::assign)
        out << name << " = " << render_value(store, op.args.back());
      else if (op.form == detail::Form::add_assign)
        out << name << " += " << render_value(store, op.args.back());
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
      for (std::size_t index = 0; index < op.range_count; ++index) {
        if (index)
          out << ", ";
        out << op.iter_names[index] << " in "
            << render_value(store, op.args[index * 2]) << ".."
            << render_value(store, op.args[index * 2 + 1]);
      }
      out << " {\n";
      render_block(out, store, op.blocks.front(), depth + 1);
      indent(out, depth);
      out << "}\n";
    } else if (op.kind == Op::Kind::branch) {
      out << "if " << render_value(store, op.args.front()) << " {\n";
      render_block(out, store, op.blocks[0], depth + 1);
      indent(out, depth);
      out << "}";
      const auto& else_ops = store.blocks[op.blocks[1]].data.ops;
      const bool empty_else =
          else_ops.size() == 1 &&
          store.ops[else_ops.front()].data.kind == Op::Kind::yield;
      if (!empty_else) {
        out << " else {\n";
        render_block(out, store, op.blocks[1], depth + 1);
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
    if (fn.host)
      out << "[host]\n";
    out << "fn " << fn.name;
    if (!fn.generics.empty()) {
      out << '<';
      for (std::size_t index = 0; index < fn.generics.size(); ++index) {
        if (index)
          out << ", ";
        out << fn.generics[index];
      }
      out << '>';
    }
    out << '(';
    for (std::size_t index = 0; index < fn.params.size(); ++index) {
      if (index)
        out << ", ";
      const detail::ValData& param = store.vals[fn.params[index]].data;
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
    render_block(out, store, fn.blocks.front(), 1);
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

std::uint32_t arg_block(const detail::Store& store, std::uint32_t value) {
  for (std::uint32_t id = 0; id < store.blocks.size(); ++id) {
    const auto& args = store.blocks[id].data.args;
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

std::size_t op_index(const detail::Store& store, std::uint32_t block,
                     std::uint32_t op) {
  const auto& ops = store.blocks[block].data.ops;
  const auto found = std::find(ops.begin(), ops.end(), op);
  return found == ops.end() ? ops.size()
                            : static_cast<std::size_t>(found - ops.begin());
}

bool block_within(const detail::Store& store, std::uint32_t child,
                  std::uint32_t ancestor) {
  while (child != detail::none) {
    if (child == ancestor)
      return true;
    const auto parent = store.blocks[child].data.parent_op;
    child =
        parent == detail::none ? detail::none : store.ops[parent].data.block;
  }
  return false;
}

bool dominates(const detail::Store& store, std::uint32_t value,
               std::uint32_t use) {
  const detail::ValData& val = store.vals[value].data;
  const std::uint32_t use_block = store.ops[use].data.block;
  if (val.kind == detail::ValKind::generic ||
      val.kind == detail::ValKind::param)
    return arg_fn(store, value) == store.blocks[use_block].data.fn;
  if (val.kind == detail::ValKind::block_arg) {
    const std::uint32_t def_block = arg_block(store, value);
    return def_block != detail::none &&
           block_within(store, use_block, def_block);
  }
  if (val.def == detail::none || val.def >= store.ops.size())
    return false;
  const std::uint32_t def_block = store.ops[val.def].data.block;
  if (def_block == use_block)
    return op_index(store, def_block, val.def) <
           op_index(store, use_block, use);

  std::uint32_t child = use_block;
  while (child != detail::none) {
    const std::uint32_t parent = store.blocks[child].data.parent_op;
    if (parent == detail::none)
      return false;
    const std::uint32_t parent_block = store.ops[parent].data.block;
    if (parent_block == def_block)
      return op_index(store, def_block, val.def) <
             op_index(store, parent_block, parent);
    child = parent_block;
  }
  return false;
}

}  // namespace

bool Mod::verify(const Env&) {
  detail::Store& store = impl_->store;
  store.diags.clear();
  detail::rebuild_uses(store);
  if (store.name.empty())
    detail::add_diag(store.diags, "module has no name");
  for (const auto& fn_slot : store.fns) {
    if (!fn_slot.live || fn_slot.data.external)
      continue;
    const detail::FnData& fn = fn_slot.data;
    if (fn.blocks.empty()) {
      detail::add_diag(store.diags, "function '" + fn.name + "' has no body",
                       fn.loc);
      continue;
    }
    const auto& body_ops = store.blocks[fn.blocks.front()].data.ops;
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
  for (const auto& block_slot : store.blocks) {
    if (!block_slot.live || block_slot.data.parent_op == detail::none)
      continue;
    const auto& ops = block_slot.data.ops;
    if (ops.empty() || store.ops[ops.back()].data.kind != Op::Kind::yield) {
      detail::add_diag(store.diags, "nested block must end with yield");
      continue;
    }
    const auto& parent = store.ops[block_slot.data.parent_op].data;
    if (store.ops[ops.back()].data.args.size() != parent.carried_count)
      detail::add_diag(store.diags,
                       "yield arity does not match carried values");
  }
  for (std::uint32_t op_id = 0; op_id < store.ops.size(); ++op_id) {
    const auto& op_slot = store.ops[op_id];
    if (!op_slot.live)
      continue;
    const detail::OpData& op = op_slot.data;
    if (op.block >= store.blocks.size() || !store.blocks[op.block].live)
      detail::add_diag(store.diags, "operation has an invalid parent block",
                       op.loc);
    if (op.kind == Op::Kind::call && op.callee.empty())
      detail::add_diag(store.diags, "call has no callee", op.loc);
    if (op.kind == Op::Kind::loop &&
        (op.blocks.size() != 1 ||
         op.args.size() != op.range_count * 2 + op.carried_count ||
         op.outs.size() != op.carried_count))
      detail::add_diag(store.diags, "loop structure is inconsistent", op.loc);
    if (op.kind == Op::Kind::branch &&
        (op.blocks.size() != 2 || op.args.size() != 1 + op.carried_count ||
         op.outs.size() != op.carried_count))
      detail::add_diag(store.diags, "if structure is inconsistent", op.loc);
    for (const auto arg : op.args) {
      if (arg >= store.vals.size() || !store.vals[arg].live) {
        detail::add_diag(store.diags, "operation uses an invalid value",
                         op.loc);
        continue;
      }
      if (!dominates(store, arg, op_id))
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
