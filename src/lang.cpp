#include "detail.h"
#include "language.h"
#include "syntax.h"

#include <algorithm>
#include <charconv>
#include <exception>
#include <set>
#include <unordered_map>
#include <utility>

namespace joggle {

namespace {

using syntax::Tk;
using syntax::Token;
using syntax::lex;
using detail::GenericInfo;
using detail::generic_info;
using detail::generic_names;
using detail::intrinsic_cast;
using detail::intrinsic_type;
using detail::precedence;

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

bool supported_operator(std::string_view spelling) {
  return precedence(spelling) >= 0 || spelling == "!" || spelling == "~" ||
         spelling == ".." || spelling == "[]" || spelling == "[]=";
}

}  // namespace

class Parser {
public:
  Parser(Env&, std::string_view source, Mod& mod, std::string_view file)
      : store_(mod.impl_->store), tokens_(lex(source, file)) {}

  Parser(Env&, std::span<const Source> sources, Mod& mod)
      : store_(mod.impl_->store), tokens_(lex(sources)) {}

  bool run() {
    store_ = {};
    free_vals_.clear();
    if (!word("module"))
      return fail("expected 'module'");
    if (peek().kind != Tk::name)
      return fail("expected module name");
    const Token module = take();
    if (!detail::valid_qualified_name(module.text))
      return fail("invalid module name '" + module.text + "'", module.loc);
    store_.name = module.text;
    semi();
    while (word("use")) {
      if (peek().kind != Tk::name)
        return fail("expected module name after 'use'");
      const Token dependency = take();
      if (!detail::valid_qualified_name(dependency.text))
        return fail("invalid module name '" + dependency.text + "'",
                    dependency.loc);
      store_.uses.push_back(dependency.text);
      semi();
    }
    while (!at_end()) {
      Attr::Dict meta;
      if (!parse_meta(meta))
        return false;
      const bool local = word("local");
      if (!parse_fn(std::move(meta), local))
        return false;
    }
    compact_values();
    detail::rebuild_uses(store_);
    return store_.diags.empty();
  }

  bool attr(Attr& out) {
    store_ = {};
    out = Attr{};
    Ty ignored;
    auto value = attr_literal(ignored);
    if (!value)
      return false;
    semi();
    if (!at_end())
      return fail("unexpected token after attribute literal");
    out = std::move(*value);
    return true;
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
    if (peek().kind != Tk::symbol || !is(text))
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

  void compact_values() {
    const std::size_t live = std::count_if(
        store_.vals.begin(), store_.vals.end(),
        [](const auto& slot) { return slot.live; });
    if (live == store_.vals.size())
      return;
    std::vector<std::uint32_t> ids(store_.vals.size(), detail::none);
    std::vector<detail::Slot<detail::ValData>> values;
    values.reserve(live);
    for (std::size_t old = 0; old < store_.vals.size(); ++old) {
      if (!store_.vals[old].live)
        continue;
      ids[old] = static_cast<std::uint32_t>(values.size());
      values.push_back(std::move(store_.vals[old]));
    }
    const auto remap = [&](std::vector<std::uint32_t>& refs) {
      for (std::uint32_t& ref : refs)
        ref = ids[ref];
    };
    for (auto& fn : store_.fns) {
      remap(fn.data.generic_vals);
      remap(fn.data.params);
    }
    for (auto& blk : store_.blks)
      remap(blk.data.args);
    for (auto& op : store_.ops) {
      remap(op.data.args);
      remap(op.data.outs);
    }
    store_.vals = std::move(values);
  }
  std::string take_name(std::string_view what) {
    if (peek().kind != Tk::name) {
      fail("expected " + std::string(what));
      return {};
    }
    return take().text;
  }

  std::string take_binding(std::string_view what) {
    const Loc loc = peek().loc;
    std::string name = take_name(what);
    if (!name.empty() && !detail::valid_binding(name)) {
      fail("invalid " + std::string(what) + " '" + name + "'", loc);
      return {};
    }
    return name;
  }

  bool parse_meta(Attr::Dict& out) {
    while (match("[")) {
      if (is("]"))
        return fail("attribute list cannot be empty");
      do {
        const Token key = take();
        if (key.kind != Tk::name)
          return fail("expected metadata name", key.loc);
        if (!detail::valid_qualified_name(key.text))
          return fail("invalid metadata name '" + key.text + "'", key.loc);
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
    if (!free_vals_.empty()) {
      const std::uint32_t id = free_vals_.back();
      free_vals_.pop_back();
      store_.vals[id].data = std::move(data);
      store_.vals[id].live = true;
      return id;
    }
    const auto id = static_cast<std::uint32_t>(store_.vals.size());
    store_.vals.push_back({std::move(data), 1, true});
    return id;
  }

  void drop_val(std::uint32_t id) {
    if (id >= store_.vals.size() || !store_.vals[id].live)
      return;
    store_.vals[id].data = {};
    store_.vals[id].live = false;
    ++store_.vals[id].generation;
    free_vals_.push_back(id);
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

  void show(std::uint32_t value, Op::Form form, std::string name = {}) {
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

  bool parse_fn(Attr::Dict meta, bool local) {
    if (!word("fn"))
      return fail("expected function declaration");
    const Token first = take();
    const Loc loc = first.loc;
    std::string name;
    if (first.kind == Tk::name) {
      if (!detail::valid_qualified_name(first.text))
        return fail("invalid function name '" + first.text + "'", loc);
      name = first.text;
    } else if (first.kind == Tk::symbol) {
      std::string spelling = first.text;
      if (spelling == "[") {
        if (!expect("]"))
          return false;
        spelling = "[]";
        if (match("="))
          spelling += '=';
      }
      if (!supported_operator(spelling))
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
    data.local = local;
    std::vector<Decl> generics;
    if (match("<")) {
      do {
        Attr::Dict generic_meta;
        if (!parse_meta(generic_meta))
          return false;
        std::string generic = take_binding("generic parameter");
        if (generic.empty())
          return false;
        if (intrinsic_type(generic))
          return fail("generic parameter '" + generic +
                      "' conflicts with an intrinsic type");
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
        std::string param = take_binding("parameter name");
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
                 detail::same_type_pattern(
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
    std::set<std::string> declared;
    for (const auto& [name, ignored] : scope) {
      (void)ignored;
      declared.insert(name);
    }
    if (!parse_blk(fn, body, scope, false, std::move(declared)) ||
        !expect("}"))
      return false;
    semi();
    return true;
  }

  bool parse_blk(std::uint32_t fn, std::uint32_t blk, Scope& scope,
                 bool nested,
                 std::set<std::string> declared = {}) {
    while (!at_end() && !is("}")) {
      Attr::Dict meta;
      if (!parse_meta(meta))
        return false;
      if (word("let") || word("var")) {
        const bool mut = tokens_[pos_ - 1].text == "var";
        const Op::Form form = mut ? Op::Form::var : Op::Form::let;
        std::vector<Decl> names;
        do {
          Attr::Dict value_meta;
          if (!parse_meta(value_meta))
            return false;
          std::string name = take_binding("binding name");
          if (name.empty())
            return false;
          if (declared.contains(name))
            return fail("binding '" + name +
                        "' is already declared in this scope");
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
              store_.ops[def].data.form != Op::Form::hidden ||
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
            store_.ops[expression_def].data.form != Op::Form::hidden)
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
          declared.insert(names[index].name);
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
    return !(nested && at_end()) || fail("unterminated Blk");
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

  void replace_in(std::uint32_t blk, std::uint32_t old_value,
                  std::uint32_t new_value) {
    for (const std::uint32_t id : store_.blks[blk].data.ops) {
      detail::OpData& op = store_.ops[id].data;
      for (std::uint32_t& arg : op.args)
        if (arg == old_value)
          arg = new_value;
      for (const std::uint32_t child : op.blks)
        replace_in(child, old_value, new_value);
    }
  }

  void prune_carried(
      std::uint32_t id,
      std::vector<std::pair<std::string, Binding>>& captures) {
    detail::OpData& op = store_.ops[id].data;
    const std::size_t count = captures.size();
    const std::size_t iterators =
        op.kind == Op::Kind::loop ? op.iter_names.size() : 0;
    const std::size_t inputs =
        op.kind == Op::Kind::loop ? iterators : std::size_t{1};
    if (count == 0 || op.args.size() != inputs + count ||
        op.outs.size() != count)
      return;

    std::vector<bool> keep(count, false);
    for (const std::uint32_t blk : op.blks) {
      const detail::BlkData& body = store_.blks[blk].data;
      if (body.args.size() != iterators + count || body.ops.empty())
        return;
      const detail::OpData& yield = store_.ops[body.ops.back()].data;
      if (yield.kind != Op::Kind::yield || yield.args.size() != count)
        return;
      for (std::size_t index = 0; index < count; ++index)
        keep[index] = keep[index] ||
                      yield.args[index] != body.args[iterators + index];
    }
    if (std::all_of(keep.begin(), keep.end(), [](bool item) { return item; }))
      return;

    for (const std::uint32_t blk : op.blks) {
      detail::BlkData& body = store_.blks[blk].data;
      const std::vector<std::uint32_t> old_args = body.args;
      for (std::size_t index = 0; index < count; ++index)
        if (!keep[index])
          replace_in(blk, old_args[iterators + index],
                     op.args[inputs + index]);

      std::vector<std::uint32_t> next_args(old_args.begin(),
                                           old_args.begin() + iterators);
      detail::OpData& yield = store_.ops[body.ops.back()].data;
      const std::vector<std::uint32_t> old_yield = yield.args;
      std::vector<std::uint32_t> next_yield;
      for (std::size_t index = 0; index < count; ++index) {
        const std::uint32_t value = old_args[iterators + index];
        if (keep[index]) {
          next_args.push_back(value);
          next_yield.push_back(old_yield[index]);
        } else {
          drop_val(value);
        }
      }
      body.args = std::move(next_args);
      yield.args = std::move(next_yield);
    }

    const std::vector<std::uint32_t> old_inputs = op.args;
    const std::vector<std::uint32_t> old_outputs = op.outs;
    std::vector<std::uint32_t> next_inputs(old_inputs.begin(),
                                           old_inputs.begin() + inputs);
    std::vector<std::uint32_t> next_outputs;
    std::vector<std::pair<std::string, Binding>> next_captures;
    for (std::size_t index = 0; index < count; ++index) {
      if (keep[index]) {
        next_inputs.push_back(old_inputs[inputs + index]);
        store_.vals[old_outputs[index]].data.index = next_outputs.size();
        next_outputs.push_back(old_outputs[index]);
        next_captures.push_back(captures[index]);
      } else {
        drop_val(old_outputs[index]);
      }
    }
    op.args = std::move(next_inputs);
    op.outs = std::move(next_outputs);
    op.carried_count = next_captures.size();
    captures = std::move(next_captures);
  }

  bool parse_for(std::uint32_t fn, std::uint32_t blk, Scope& scope,
                 Attr::Dict meta) {
    detail::OpData data;
    data.kind = Op::Kind::loop;
    data.meta = std::move(meta);
    data.loc = tokens_[pos_ - 1].loc;
    do {
      const std::string iter = take_binding("loop variable");
      if (iter.empty())
        return false;
      if (!word("in"))
        return fail("expected 'in' after loop variable");
      if (scope.contains(iter) ||
          std::find(data.iter_names.begin(), data.iter_names.end(), iter) !=
              data.iter_names.end())
        return fail("loop variable '" + iter + "' is already visible");
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

    auto captures = carried(scope);
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
    std::set<std::string> declared(
        store_.ops[op].data.iter_names.begin(),
        store_.ops[op].data.iter_names.end());
    if (!expect("{") ||
        !parse_blk(fn, body, inner, true, std::move(declared)) ||
        !expect("}"))
      return false;
    detail::OpData yield;
    yield.kind = Op::Kind::yield;
    for (const auto& [name, ignored] : captures) {
      (void)ignored;
      yield.args.push_back(inner.at(name).value);
    }
    add_op(body, std::move(yield));
    prune_carried(op, captures);
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
    auto captures = carried(scope);
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
    prune_carried(op, captures);
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
      Op::Form form = Op::Form::assign;
      if (assignment != "=") {
        value = add_call(blk,
                         operator_name(assignment.substr(0,
                                                         assignment.size() - 1)),
                         {found->second.value, rhs},
                         store_.vals[found->second.value].data.type, name.loc);
        form = Op::Form::compound;
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
      show(value, Op::Form::index_assign, base.text);
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
    const std::uint32_t def = store_.vals[value].data.def;
    if (def == detail::none ||
        store_.ops[def].data.form != Op::Form::hidden) {
      if (!meta.empty())
        return fail("attributes require a new operation statement");
      semi();
      return true;
    }
    show(value, Op::Form::expr);
    if (!attach(value, std::move(meta)))
      return false;
    semi();
    return true;
  }

  std::uint32_t logical(std::uint32_t blk, Scope& scope, std::uint32_t left,
                        const Token& token, int minimum) {
    detail::OpData data;
    data.kind = Op::Kind::branch;
    data.args = {left, left};
    data.carried_count = 1;
    data.logic = token.text == "&&" ? detail::Logic::and_ : detail::Logic::or_;
    data.loc = token.loc;
    const auto op = add_op(blk, std::move(data), {{"", Ty("bool")}});
    const auto fn = store_.blks[blk].data.fn;

    for (std::size_t arm = 0; arm < 2; ++arm) {
      const auto body = add_blk(fn, op);
      store_.ops[op].data.blks.push_back(body);
      detail::ValData carried;
      carried.kind = detail::ValKind::blk_arg;
      carried.type = Ty("bool");
      const auto carried_id = add_val(std::move(carried));
      store_.blks[body].data.args.push_back(carried_id);

      const bool active =
          (token.text == "&&" && arm == 0) ||
          (token.text == "||" && arm == 1);
      std::uint32_t value = carried_id;
      if (active) {
        value = expression(body, scope, minimum);
        if (value == detail::none)
          return detail::none;
      }
      detail::OpData yield;
      yield.kind = Op::Kind::yield;
      yield.args = {value};
      yield.loc = token.loc;
      add_op(body, std::move(yield));
    }
    return store_.ops[op].data.outs.front();
  }

  std::uint32_t expression(std::uint32_t blk, Scope& scope, int minimum = 0) {
    std::uint32_t left = unary(blk, scope);
    if (left == detail::none)
      return left;
    for (;;) {
      if (peek().kind != Tk::symbol)
        break;
      const int level = precedence(peek().text);
      if (level < minimum)
        break;
      const Token op = take();
      if (op.text == "&&" || op.text == "||") {
        left = logical(blk, scope, left, op, level + 1);
        if (left == detail::none)
          return left;
        continue;
      }
      const auto right = expression(blk, scope, level + 1);
      if (right == detail::none)
        return right;
      Ty type = store_.vals[left].data.type;
      if (op.text == "==" || op.text == "!=" || op.text == "<" ||
          op.text == "<=" || op.text == ">" || op.text == ">=")
        type = Ty("bool");
      left =
          add_call(blk, operator_name(op.text), {left, right}, type, op.loc);
    }
    return left;
  }

  std::uint32_t unary(std::uint32_t blk, Scope& scope) {
    if (peek().kind == Tk::symbol &&
        (is("+") || is("-") || is("!") || is("~"))) {
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
    if (callee.ends_with('.') && peek().kind == Tk::symbol) {
      std::string spelling = take().text;
      if (spelling == "[") {
        if (!expect("]"))
          return detail::none;
        spelling = "[]";
        if (match("="))
          spelling += '=';
      }
      if (!supported_operator(spelling)) {
        fail("unsupported qualified function symbol '" + spelling + "'",
             token.loc);
        return detail::none;
      }
      callee += operator_name(spelling);
    }
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
  std::vector<std::uint32_t> free_vals_;
  std::size_t pos_ = 0;
};


bool parse(Env& env, std::string_view source, Mod& out, std::string_view file) {
  return Parser(env, source, out, file).run();
}

bool parse(Env& env, std::span<const Source> sources, Mod& out) {
  return Parser(env, sources, out).run();
}

bool parse(Env& env, std::string_view source, Attr& out,
           std::string_view file) {
  env.clear_diags();
  Mod scratch;
  if (Parser(env, source, scratch, file).attr(out))
    return true;
  for (const Diag& diag : scratch.diags())
    env.error(diag.message, diag.loc);
  return false;
}


}  // namespace joggle
