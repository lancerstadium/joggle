#include "ir/mod.h"

#include "joggle/digest.h"
#include "lang/expr.h"
#include "lang/prelude.h"
#include "sema/domain.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <utility>

namespace joggle {
namespace {

std::string_view kind_name(Mod::Symbol::Kind kind) {
  switch (kind) {
  case Mod::Symbol::Kind::Type:
    return "type";
  case Mod::Symbol::Kind::Fn:
    return "fn";
  }
  return "invalid";
}

Version upper_bound(VersionRange range) {
  Version upper = range.base;
  switch (range.kind) {
  case VersionRange::Kind::Exact:
    return upper;
  case VersionRange::Kind::Major:
    ++upper.major;
    upper.minor = 0;
    upper.patch = 0;
    return upper;
  case VersionRange::Kind::Minor:
    ++upper.minor;
    upper.patch = 0;
    return upper;
  case VersionRange::Kind::Caret:
    if (upper.major != 0U) {
      ++upper.major;
      upper.minor = 0;
      upper.patch = 0;
    } else if (upper.minor != 0U) {
      ++upper.minor;
      upper.patch = 0;
    } else {
      ++upper.patch;
    }
    return upper;
  }
  return upper;
}

template <typename Definition>
std::optional<std::size_t>
find_definition(const std::vector<Definition>& values, std::string_view name) {
  const auto found =
      std::find_if(values.begin(), values.end(),
                   [&](const Definition& value) { return value.name == name; });
  if (found == values.end()) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(std::distance(values.begin(), found));
}

}  // namespace

bool VersionRange::contains(Version candidate) const {
  if (kind == VersionRange::Kind::Exact) {
    return candidate == base;
  }
  return candidate >= base && candidate < upper_bound(*this);
}

std::string to_string(Version version) {
  return std::to_string(version.major) + "." + std::to_string(version.minor) +
         "." + std::to_string(version.patch);
}

std::string to_string(VersionRange range) {
  std::string result;
  if (range.kind == VersionRange::Kind::Caret) {
    result += '^';
  }
  result += std::to_string(range.base.major);
  if (range.kind == VersionRange::Kind::Major) {
    return result;
  }
  result += "." + std::to_string(range.base.minor);
  if (range.kind == VersionRange::Kind::Minor) {
    return result;
  }
  return result + "." + std::to_string(range.base.patch);
}

Mod::Symbol::Symbol(std::string mod_name, Version mod_version,
                    std::string declaration_digest, Symbol::Kind kind,
                    std::string local_name, std::string discriminator)
    : mod_name_(std::move(mod_name)), mod_version_(mod_version),
      declaration_digest_(std::move(declaration_digest)), kind_(kind),
      local_name_(std::move(local_name)),
      discriminator_(std::move(discriminator)) {}

std::string Mod::Symbol::qualified_name() const {
  return mod_name_ + "." + local_name_;
}

std::string Mod::Symbol::stable_name() const {
  std::string result = mod_name_ + "@" + to_string(mod_version_) + "/" +
                       std::string(kind_name(kind_)) + "/" + local_name_;
  if (!discriminator_.empty()) {
    result += "/" + discriminator_;
  }
  return result;
}

bool Mod::Symbol::operator==(const Symbol& other) const {
  return mod_name_ == other.mod_name_ && mod_version_ == other.mod_version_ &&
         kind_ == other.kind_ && local_name_ == other.local_name_ &&
         discriminator_ == other.discriminator_;
}

Mod::TypeDecl::TypeDecl(std::shared_ptr<const Storage> storage,
                        std::size_t index)
    : storage_(std::move(storage)), index_(index) {}

std::string_view Mod::TypeDecl::name() const {
  return storage_->types[index_].name;
}

bool Mod::TypeDecl::exported() const {
  return storage_->types[index_].exported;
}

std::span<const Mod::ParamDecl> Mod::TypeDecl::parameters() const {
  return storage_->types[index_].parameters;
}

std::span<const Mod::TypeDecl::DerivedParamDecl>
Mod::TypeDecl::derived_parameters() const {
  return storage_->types[index_].derived_parameters;
}

Mod::Symbol Mod::TypeDecl::symbol() const {
  return {storage_->name, storage_->version, storage_->declaration_digest,
          Symbol::Kind::Type, storage_->types[index_].name};
}

bool Mod::TypeDecl::operator==(const TypeDecl& other) const {
  return symbol() == other.symbol();
}

Mod::FnDecl::FnDecl(std::shared_ptr<const Storage> storage, std::size_t index)
    : storage_(std::move(storage)), index_(index) {}

std::string_view Mod::FnDecl::name() const {
  return storage_->fns[index_].name;
}

bool Mod::FnDecl::exported() const {
  return storage_->fns[index_].declaration->exported;
}

std::span<const Mod::FnDecl::GenericDecl> Mod::FnDecl::generics() const {
  return storage_->fns[index_].declaration->generics;
}

std::span<const Mod::ParamDecl> Mod::FnDecl::inputs() const {
  return storage_->fns[index_].declaration->inputs;
}

std::span<const Mod::ParamDecl> Mod::FnDecl::results() const {
  return storage_->fns[index_].declaration->results;
}

namespace {

std::vector<Mod::ParamDecl>
select_parameters(std::span<const Mod::ParamDecl> parameters,
                  bool select_values) {
  std::vector<Mod::ParamDecl> result;
  for (const Mod::ParamDecl& parameter : parameters) {
    if (detail::is_value_port(parameter) == select_values) {
      result.push_back(parameter);
    }
  }
  return result;
}

}  // namespace

bool detail::is_value_port(const Mod::ParamDecl& parameter) {
  return !compiler_domain(parameter.domain);
}

std::vector<Mod::ParamDecl>
detail::FnTypeAccess::compiler_inputs(const Mod::FnDecl& fn) {
  return select_parameters(fn.inputs(), false);
}

std::vector<Mod::ParamDecl>
detail::FnTypeAccess::value_inputs(const Mod::FnDecl& fn) {
  return select_parameters(fn.inputs(), true);
}

std::vector<Mod::ParamDecl>
detail::FnTypeAccess::compiler_results(const Mod::FnDecl& fn) {
  return select_parameters(fn.results(), false);
}

std::vector<Mod::ParamDecl>
detail::FnTypeAccess::value_results(const Mod::FnDecl& fn) {
  return select_parameters(fn.results(), true);
}

bool detail::has_default_specialization(const Mod::FnDecl& fn) {
  const auto& contract = FnTypeAccess::get(fn);
  std::vector<std::string_view> bound_generics;
  for (std::size_t index = 0; index < fn.inputs().size(); ++index) {
    if (is_value_port(fn.inputs()[index])) {
      continue;
    }
    if (!fn.inputs()[index].default_value) {
      return false;
    }
    if (index < contract.bindings.size() && contract.bindings[index] &&
        contract.bindings[index]->kind == Mod::Expr::Kind::Variable) {
      bound_generics.push_back(contract.bindings[index]->text);
    }
  }
  return std::all_of(
      fn.generics().begin(), fn.generics().end(), [&](const auto& generic) {
        return std::find(bound_generics.begin(), bound_generics.end(),
                         generic.name) != bound_generics.end();
      });
}

std::optional<Mod::FnDecl::Fixity> Mod::FnDecl::operator_fixity() const {
  return storage_->fns[index_].declaration->operator_fixity;
}

Mod::FnDecl::Form Mod::FnDecl::form() const {
  const detail::FnMember& fn = storage_->fns[index_];
  return fn.ir || fn.declaration->body ? Form::Body : Form::External;
}

const Fn* Mod::FnDecl::body() const { return storage_->fns[index_].ir.get(); }

const Mod::Expr* detail::ModAccess::expression(const Mod::FnDecl& fn) {
  if (!FnTypeAccess::value_inputs(fn).empty() ||
      !FnTypeAccess::value_results(fn).empty()) {
    return nullptr;
  }
  return returned_expression(fn);
}

const Mod::Expr* detail::ModAccess::returned_expression(const Mod::FnDecl& fn) {
  const auto& body = fn.storage_->fns[fn.index_].declaration->body;
  if (!body || body->blocks.size() != 1U || body->blocks.front().terminator ||
      body->blocks.front().statements.size() != 1U ||
      body->blocks.front().statements.front().kind !=
          detail::StatementSyntax::Kind::Return ||
      body->blocks.front().statements.front().values.size() != 1U) {
    return nullptr;
  }
  return &body->blocks.front().statements.front().values.front().value;
}

std::string Mod::FnDecl::signature() const {
  const auto text = [](const Mod::Expr& expression) {
    return detail::format_expression(expression);
  };
  std::string result(name());
  if (!generics().empty()) {
    result += '<';
    for (std::size_t index = 0; index < generics().size(); ++index) {
      if (index != 0U) {
        result += ',';
      }
      result += text(generics()[index].domain);
    }
    result += '>';
  }
  result += '(';
  for (std::size_t index = 0; index < inputs().size(); ++index) {
    if (index != 0U) {
      result += ',';
    }
    result += text(inputs()[index].domain);
    if (inputs()[index].variadic) {
      result += "...";
    }
  }
  result += ")->";
  if (results().size() != 1U) {
    result += '(';
  }
  for (std::size_t index = 0; index < results().size(); ++index) {
    if (index != 0U) {
      result += ',';
    }
    result += text(results()[index].domain);
  }
  if (results().size() != 1U) {
    result += ')';
  }
  return result;
}

Mod::Symbol Mod::FnDecl::symbol() const {
  return {storage_->name,
          storage_->version,
          storage_->declaration_digest,
          Symbol::Kind::Fn,
          storage_->fns[index_].name,
          signature()};
}

bool Mod::FnDecl::operator==(const FnDecl& other) const {
  return symbol() == other.symbol();
}

const detail::FnTypeContract& detail::FnTypeAccess::get(const Mod::FnDecl& fn) {
  return fn.storage_->fns[fn.index_].declaration->types;
}

Mod::Mod(std::shared_ptr<const Storage> storage)
    : storage_(std::move(storage)) {}

Mod::Mod(std::string name, Version version) {
  if (name.empty() ||
      (std::isalpha(static_cast<unsigned char>(name.front())) == 0 &&
       name.front() != '_') ||
      !std::all_of(name.begin() + 1, name.end(), [](char character) {
        return std::isalnum(static_cast<unsigned char>(character)) != 0 ||
               character == '_';
      })) {
    throw std::invalid_argument("a Mod needs a valid name");
  }
  auto storage = std::make_shared<Storage>();
  storage->name = std::move(name);
  storage->version = version;
  storage_ = storage;
  storage->digest = compute_digest(storage);
  storage->declaration_digest = compute_declaration_digest(storage);
}

Mod::Mod(const Mod& other) : storage_(other.storage_) {
  if (std::none_of(
          storage_->fns.begin(), storage_->fns.end(),
          [](const detail::FnMember& fn) { return fn.ir != nullptr; })) {
    return;
  }
  auto storage = std::make_shared<Storage>(*storage_);
  for (detail::FnMember& fn : storage->fns) {
    if (fn.ir) {
      fn.ir = std::make_shared<Fn>(*fn.ir);
    }
  }
  storage_ = std::move(storage);
}

Mod& Mod::operator=(const Mod& other) {
  if (this != &other) {
    Mod copy(other);
    storage_.swap(copy.storage_);
  }
  return *this;
}

std::string_view Mod::name() const { return storage_->name; }

Version Mod::version() const { return storage_->version; }

std::string Mod::compute_digest(const std::shared_ptr<const Storage>& storage) {
  std::string canonical = format(Mod(storage));
  for (const auto& [name, payload] : storage->data) {
    static_cast<void>(payload);
    canonical += "\ndata ";
    canonical += name;
    canonical += ';';
  }
  return sha256(canonical);
}

std::string_view
Mod::current_digest(const std::shared_ptr<const Storage>& storage) {
  if (std::any_of(
          storage->fns.begin(), storage->fns.end(),
          [](const detail::FnMember& fn) { return fn.ir != nullptr; })) {
    std::vector<std::pair<std::string, Fn::Revision>> revisions;
    revisions.reserve(storage->fns.size());
    for (const detail::FnMember& fn : storage->fns) {
      if (fn.ir) {
        revisions.emplace_back(fn.name, fn.ir->revision());
      }
    }
    if (revisions != storage->digest_revisions) {
      storage->digest = compute_digest(storage);
      storage->digest_revisions = std::move(revisions);
    }
  }
  return storage->digest;
}

std::string
Mod::compute_declaration_digest(const std::shared_ptr<const Storage>& storage) {
  return std::string(declaration_view(storage).digest());
}

Mod Mod::declaration_view(const std::shared_ptr<const Storage>& storage) {
  auto declarations = std::make_shared<Storage>(*storage);
  const Mod source(storage);
  for (const Dependency& dependency : source.dependencies()) {
    if (dependency.name == detail::prelude_mod_name ||
        dependency.name == source.name()) {
      continue;
    }
    const auto found = std::find_if(
        declarations->imports.begin(), declarations->imports.end(),
        [&](const Import& import) { return import.name == dependency.name; });
    if (found == declarations->imports.end()) {
      declarations->imports.push_back(
          {dependency.name,
           {VersionRange::Kind::Exact, dependency.version},
           {}});
    } else if (found->alias.empty()) {
      found->version = {VersionRange::Kind::Exact, dependency.version};
    }
  }
  for (detail::FnMember& member : declarations->fns) {
    if (member.declaration) {
      member.declaration->body.reset();
    }
    member.ir.reset();
  }
  declarations->data.clear();
  std::vector<std::size_t> order(declarations->fns.size());
  for (std::size_t index = 0; index < order.size(); ++index) {
    order[index] = index;
  }
  std::sort(order.begin(), order.end(),
            [&](std::size_t left, std::size_t right) {
              return FnDecl(declarations, left).signature() <
                     FnDecl(declarations, right).signature();
            });
  std::vector<detail::FnMember> fns;
  fns.reserve(order.size());
  for (const std::size_t index : order) {
    fns.push_back(std::move(declarations->fns[index]));
  }
  declarations->fns = std::move(fns);
  declarations->digest.clear();
  declarations->declaration_digest.clear();
  declarations->digest_revisions.clear();
  Mod result(declarations);
  declarations->digest = compute_digest(declarations);
  declarations->declaration_digest = declarations->digest;
  return result;
}

std::string_view Mod::digest() const { return current_digest(storage_); }

std::string_view Mod::declaration_digest() const {
  return storage_->declaration_digest;
}

std::span<const Mod::Import> Mod::imports() const { return storage_->imports; }

std::string Mod::store(Bytes bytes) {
  const std::string_view raw(reinterpret_cast<const char*>(bytes.data()),
                             bytes.size());
  const std::string name = "sha256:" + sha256(raw);
  const auto existing = storage_->data.find(name);
  if (existing != storage_->data.end()) {
    if (*existing->second != bytes) {
      throw std::logic_error("content-addressed Mod data collision");
    }
    return name;
  }
  auto next = std::make_shared<Storage>(*storage_);
  next->data.emplace(name, std::make_shared<const Bytes>(std::move(bytes)));
  next->digest = compute_digest(next);
  storage_ = std::move(next);
  return name;
}

std::optional<std::span<const std::byte>>
Mod::data(std::string_view name) const {
  const auto found = storage_->data.find(name);
  return found == storage_->data.end()
             ? std::nullopt
             : std::optional<std::span<const std::byte>>{*found->second};
}

std::vector<std::string> Mod::data() const {
  std::vector<std::string> names;
  names.reserve(storage_->data.size());
  for (const auto& [name, payload] : storage_->data) {
    static_cast<void>(payload);
    names.push_back(name);
  }
  return names;
}

std::optional<Mod::TypeDecl> Mod::type(std::string_view name) const {
  const auto index = find_definition(storage_->types, name);
  return index ? std::optional<TypeDecl>{TypeDecl(storage_, *index)}
               : std::nullopt;
}

std::optional<Mod::FnDecl> Mod::fn(std::string_view name) const {
  const auto values = overloads(name);
  return values.size() == 1U ? std::optional<FnDecl>{values.front()}
                             : std::nullopt;
}

std::vector<Mod::FnDecl> Mod::overloads(std::string_view name) const {
  std::vector<FnDecl> result;
  for (std::size_t index = 0; index < storage_->fns.size(); ++index) {
    if (storage_->fns[index].declaration && storage_->fns[index].name == name) {
      result.push_back(FnDecl(storage_, index));
    }
  }
  return result;
}

std::optional<Mod::Symbol> Mod::symbol(Symbol::Kind kind,
                                       std::string_view name) const {
  if (kind == Symbol::Kind::Fn) {
    const auto declarations = overloads(name);
    return declarations.size() == 1U
               ? std::optional<Symbol>{declarations.front().symbol()}
               : std::nullopt;
  }
  const bool exists = kind == Symbol::Kind::Type && type(name).has_value();
  if (!exists) {
    return std::nullopt;
  }
  return Symbol(std::string(storage_->name), storage_->version,
                storage_->declaration_digest, kind, std::string(name));
}

std::vector<Mod::Symbol> Mod::members() const {
  std::vector<Symbol> result;
  result.reserve(storage_->types.size() + storage_->fns.size());
  const auto append = [&](Symbol::Kind kind, const auto& definitions) {
    for (const auto& definition : definitions) {
      result.push_back(Symbol(std::string(storage_->name), storage_->version,
                              storage_->declaration_digest, kind,
                              std::string(definition.name)));
    }
  };
  append(Symbol::Kind::Type, storage_->types);
  for (std::size_t index = 0; index < storage_->fns.size(); ++index) {
    if (storage_->fns[index].declaration) {
      result.push_back(FnDecl(storage_, index).symbol());
    }
  }
  return result;
}

std::vector<Mod::TypeDecl> Mod::types() const {
  std::vector<TypeDecl> result;
  result.reserve(storage_->types.size());
  for (std::size_t index = 0; index < storage_->types.size(); ++index) {
    result.push_back(TypeDecl(storage_, index));
  }
  return result;
}

std::vector<Mod::FnDecl> Mod::fns() const {
  std::vector<FnDecl> result;
  result.reserve(storage_->fns.size());
  for (std::size_t index = 0; index < storage_->fns.size(); ++index) {
    if (storage_->fns[index].declaration) {
      result.push_back(FnDecl(storage_, index));
    }
  }
  return result;
}

bool Mod::operator==(const Mod& other) const {
  return name() == other.name() && version() == other.version() &&
         digest() == other.digest();
}

Mod detail::ModAccess::declaration_view(const Mod& mod) {
  return Mod::declaration_view(mod.storage_);
}

std::shared_ptr<const detail::FnBody>
detail::ModAccess::body(const Mod& mod, const Mod::FnDecl& fn) {
  if (fn.storage_.get() != mod.storage_.get() ||
      fn.index_ >= mod.storage_->fns.size()) {
    return nullptr;
  }
  const auto& declaration = mod.storage_->fns[fn.index_].declaration;
  if (!declaration) {
    return nullptr;
  }
  const auto& body = declaration->body;
  if (!body) {
    return nullptr;
  }
  return std::shared_ptr<const detail::FnBody>(mod.storage_, &*body);
}

std::optional<Loc> detail::ModAccess::import_source(const Mod& mod,
                                                    std::size_t index) {
  if (index >= mod.storage_->import_sources.size()) {
    return std::nullopt;
  }
  return mod.storage_->import_sources[index];
}

std::optional<Loc>
detail::ModAccess::declaration_source(const Mod& mod, Mod::Symbol::Kind kind,
                                      std::string_view name) {
  const auto find_source = [&](const auto& definitions) {
    const auto index = find_definition(definitions, name);
    return index ? definitions[*index].source : std::optional<Loc>{};
  };
  switch (kind) {
  case Mod::Symbol::Kind::Type:
    return find_source(mod.storage_->types);
  case Mod::Symbol::Kind::Fn: {
    const auto found =
        std::find_if(mod.storage_->fns.begin(), mod.storage_->fns.end(),
                     [&](const detail::FnMember& fn) {
                       return fn.declaration && fn.name == name;
                     });
    return found != mod.storage_->fns.end() ? found->declaration->source
                                            : std::optional<Loc>{};
  }
  }
  return std::nullopt;
}

}  // namespace joggle
