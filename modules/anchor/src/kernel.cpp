#include "kernel.h"

#include <algorithm>
#include <cstddef>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace joggle::anchor {
namespace {

struct Contracts {
  Module::InterfaceDecl immutable_data;
  Module::InterfaceDecl placement;
  std::vector<Module::InterfaceDecl> primitives;
};

struct Summary {
  std::size_t roots = 0;
  std::size_t specializations = 0;
  std::size_t primitive_sites = 0;
  std::size_t max_depth = 0;
};

struct Closure {
  Module bundle;
  Summary summary;
};

std::optional<Contracts> contracts(Compiler& compiler,
                                   Diagnostics& diagnostics) {
  const auto arithmetic = compiler.module("arith");
  const auto memory = compiler.module("mem");
  const auto prelude = compiler.module("prelude");
  const auto target = compiler.module("anchor");
  const auto tensor = compiler.module("tensor");
  if (!arithmetic || !memory || !prelude || !target || !tensor) {
    diagnostics.report("anchor kernel analysis requires its Module imports");
    return std::nullopt;
  }

  const auto elementwise = arithmetic->interface("elementwise");
  const auto allocate = memory->interface("allocate");
  const auto read = memory->interface("read");
  const auto write = memory->interface("write");
  const auto alias = memory->interface("alias");
  const auto release = memory->interface("release");
  const auto literal = prelude->interface("literal");
  const auto immutable_data = tensor->interface("immutable_data");
  const auto placement = target->interface("placement");
  if (!elementwise || !allocate || !read || !write || !alias || !release ||
      !literal || !immutable_data || !placement) {
    diagnostics.report("anchor kernel analysis does not match its contracts");
    return std::nullopt;
  }
  return Contracts{
      *immutable_data,
      *placement,
      {*elementwise, *allocate, *read, *write, *alias, *release, *literal}};
}

bool conforms(Compiler& compiler, const Module::FunctionDecl& function,
              const std::vector<Module::InterfaceDecl>& interfaces) {
  return std::any_of(interfaces.begin(), interfaces.end(),
                     [&](const auto& interface) {
                       return compiler.conforms(function, interface);
                     });
}

bool administrative(Compiler& compiler, const Module::FunctionDecl& function,
                    const Contracts& schema) {
  return compiler.conforms(function, schema.immutable_data) ||
         compiler.conforms(function, schema.placement);
}

class Builder {
public:
  Builder(Compiler& compiler, const Module& program, const Contracts& schema,
          Diagnostics& diagnostics)
      : compiler_(compiler), program_(program), schema_(schema),
        diagnostics_(diagnostics), bundle_(program) {}

  std::optional<Closure> build() {
    for (const auto& member : program_.functions()) {
      const Function* source = member.body();
      if (source == nullptr) {
        diagnostics_.report("kernel bundle requires materialized Functions");
        return std::nullopt;
      }
      const auto replacements = plan(*source, true);
      if (!replacements) {
        return std::nullopt;
      }
      Function* entry = bundle_.body(member);
      if (entry == nullptr || !apply(*entry, *replacements)) {
        return std::nullopt;
      }
    }
    return Closure{std::move(bundle_), summary_};
  }

private:
  struct Specialization {
    std::string name;
    std::string signature;
    std::size_t depth = 0;
  };

  std::optional<Module::FunctionDecl>
  declaration(const Specialization& specialization) {
    for (const auto& candidate : bundle_.overloads(specialization.name)) {
      if (candidate.signature() == specialization.signature) {
        return candidate;
      }
    }
    diagnostics_.report("kernel bundle lost specialization '" +
                        specialization.name + "'");
    return std::nullopt;
  }

  std::optional<Specialization> specialize(const Op& call) {
    if (conforms(compiler_, call.callee(), schema_.primitives)) {
      ++summary_.primitive_sites;
      return Specialization{"", "", 0};
    }

    const auto body = compiler_.materialize(call);
    if (!body) {
      diagnostics_.report("kernel call '" +
                          call.callee().symbol().qualified_name() +
                          "' has neither a source body nor an allowed "
                          "primitive contract");
      return std::nullopt;
    }
    std::string key = call.callee().symbol().stable_name();
    key += '\n';
    key += call.callee().signature();
    key += '\n';
    key += format(*body, call.callee().name());
    if (const auto known = specializations_.find(key);
        known != specializations_.end()) {
      return known->second;
    }
    if (!active_.insert(key).second) {
      diagnostics_.report("kernel source expansion is recursive at '" +
                          call.callee().symbol().qualified_name() + "'");
      return std::nullopt;
    }

    std::size_t depth = 1;
    const auto replacements = plan(*body, false, &depth);
    if (!replacements) {
      active_.erase(key);
      return std::nullopt;
    }

    std::string name;
    do {
      name = "kernel_" + std::to_string(next_name_++) + "_" +
             std::string(call.callee().name());
    } while (!bundle_.overloads(name).empty());
    if (!bundle_.insert(name, *body, diagnostics_)) {
      active_.erase(key);
      return std::nullopt;
    }
    const auto inserted = bundle_.function(name);
    Function* target = inserted ? bundle_.body(*inserted) : nullptr;
    if (!inserted || target == nullptr) {
      diagnostics_.report("kernel bundle lost inserted specialization '" +
                          name + "'");
      active_.erase(key);
      return std::nullopt;
    }
    if (!apply(*target, *replacements)) {
      active_.erase(key);
      return std::nullopt;
    }
    const Specialization result{name, inserted->signature(), depth};
    active_.erase(key);
    specializations_.emplace(std::move(key), result);
    ++summary_.specializations;
    summary_.max_depth = std::max(summary_.max_depth, depth);
    return result;
  }

  using Plan = std::vector<std::optional<Specialization>>;

  std::optional<Plan> plan(const Function& function, bool roots,
                           std::size_t* source_depth = nullptr) {
    Plan replacements;
    replacements.reserve(function.ops().size());
    for (const Op& call : function.ops()) {
      if (administrative(compiler_, call.callee(), schema_)) {
        replacements.push_back(std::nullopt);
        continue;
      }
      if (roots) {
        ++summary_.roots;
      }
      const auto nested = specialize(call);
      if (!nested) {
        return std::nullopt;
      }
      if (source_depth != nullptr) {
        *source_depth =
            std::max(*source_depth, std::size_t{1} + nested->depth);
      }
      if (nested->name.empty()) {
        replacements.push_back(std::nullopt);
        continue;
      }
      replacements.push_back(*nested);
    }
    return replacements;
  }

  bool apply(Function& function, const Plan& replacements) {
    const auto calls = function.ops();
    if (calls.size() != replacements.size()) {
      diagnostics_.report("kernel bundle rewrite plan does not match its "
                          "Function");
      return false;
    }
    if (std::none_of(replacements.begin(), replacements.end(),
                     [](const auto& replacement) {
                       return replacement.has_value();
                     })) {
      return true;
    }
    auto edit = function.edit();
    for (std::size_t index = 0; index < replacements.size(); ++index) {
      if (!replacements[index]) {
        continue;
      }
      const auto callee = declaration(*replacements[index]);
      if (!callee) {
        return false;
      }
      std::vector<Type> results;
      for (const Value& result : calls[index].results()) {
        results.push_back(result.type());
      }
      const Op linked = edit.insert(calls[index], *callee,
                                    calls[index].operands(), results);
      edit.replace(calls[index], linked.results());
    }
    return edit.commit(diagnostics_);
  }

  Compiler& compiler_;
  const Module& program_;
  const Contracts& schema_;
  Diagnostics& diagnostics_;
  Module bundle_;
  Summary summary_;
  std::set<std::string> active_;
  std::unordered_map<std::string, Specialization> specializations_;
  std::size_t next_name_ = 0;
};

std::optional<Closure> build(Compiler& compiler, const Module& program,
                             Diagnostics& diagnostics) {
  const auto schema = contracts(compiler, diagnostics);
  return schema ? Builder(compiler, program, *schema, diagnostics).build()
                : std::nullopt;
}

Bytes encode(std::string_view text) {
  Bytes result;
  result.reserve(text.size());
  for (const char value : text) {
    result.push_back(static_cast<std::byte>(static_cast<unsigned char>(value)));
  }
  return result;
}

}  // namespace

std::optional<Module> kernel_bundle(Compiler& compiler, const Module& program,
                                    Diagnostics& diagnostics) {
  auto closure = build(compiler, program, diagnostics);
  return closure ? std::optional<Module>{std::move(closure->bundle)}
                 : std::nullopt;
}

std::optional<Bytes> kernel_report(Compiler& compiler, const Module& program,
                                   Diagnostics& diagnostics) {
  const auto closure = build(compiler, program, diagnostics);
  if (!closure) {
    return std::nullopt;
  }

  const Summary& summary = closure->summary;
  std::string report = "anchor kernel closure 1\nmodule ";
  report += program.name();
  report += '#';
  report += program.digest();
  report += "\nroot-calls ";
  report += std::to_string(summary.roots);
  report += "\nsource-specializations ";
  report += std::to_string(summary.specializations);
  report += "\nprimitive-sites ";
  report += std::to_string(summary.primitive_sites);
  report += "\nmax-source-depth ";
  report += std::to_string(summary.max_depth);
  report += '\n';
  return encode(report);
}

}  // namespace joggle::anchor
