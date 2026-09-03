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

class Inspector {
public:
  Inspector(Compiler& compiler, const Contracts& schema,
            Diagnostics& diagnostics)
      : compiler_(compiler), schema_(schema), diagnostics_(diagnostics) {}

  std::optional<std::size_t> inspect(const Op& call) {
    if (conforms(compiler_, call.callee(), schema_.primitives)) {
      ++summary_.primitive_sites;
      return std::size_t{0};
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
    if (const auto known = depths_.find(key); known != depths_.end()) {
      return known->second;
    }
    if (!active_.insert(key).second) {
      diagnostics_.report("kernel source expansion is recursive at '" +
                          call.callee().symbol().qualified_name() + "'");
      return std::nullopt;
    }

    ++summary_.specializations;
    std::size_t depth = 1;
    for (const auto& nested : body->ops()) {
      const auto nested_depth = inspect(nested);
      if (!nested_depth) {
        active_.erase(key);
        return std::nullopt;
      }
      depth = std::max(depth, std::size_t{1} + *nested_depth);
    }
    active_.erase(key);
    depths_.emplace(key, depth);
    summary_.max_depth = std::max(summary_.max_depth, depth);
    return depth;
  }

  Summary& summary() { return summary_; }

private:
  Compiler& compiler_;
  const Contracts& schema_;
  Diagnostics& diagnostics_;
  Summary summary_;
  std::set<std::string> active_;
  std::unordered_map<std::string, std::size_t> depths_;
};

Bytes encode(std::string_view text) {
  Bytes result;
  result.reserve(text.size());
  for (const char value : text) {
    result.push_back(static_cast<std::byte>(static_cast<unsigned char>(value)));
  }
  return result;
}

}  // namespace

std::optional<Bytes> kernel_report(Compiler& compiler, const Module& program,
                                   Diagnostics& diagnostics) {
  const auto schema = contracts(compiler, diagnostics);
  if (!schema) {
    return std::nullopt;
  }
  Inspector inspector(compiler, *schema, diagnostics);
  for (const auto& member : program.functions()) {
    const Function* body = member.body();
    if (body == nullptr) {
      diagnostics.report("kernel analysis requires materialized Functions");
      return std::nullopt;
    }
    for (const auto& call : body->ops()) {
      if (administrative(compiler, call.callee(), *schema)) {
        continue;
      }
      ++inspector.summary().roots;
      if (!inspector.inspect(call)) {
        return std::nullopt;
      }
    }
  }

  const Summary& summary = inspector.summary();
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
