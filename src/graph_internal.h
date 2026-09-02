#pragma once

#include <cstdint>
#include <memory>
#include <optional>

#include "joggle/graph.h"

namespace joggle::detail {

struct GraphIdentity;

// Internal access shared by the parser and compiler. Source provenance is
// deliberately not part of the public graph-editing surface.
struct GraphAccess {
  static const std::shared_ptr<GraphIdentity>& graph(const Value& value);
  static const std::shared_ptr<GraphIdentity>&
  graph(const Operation& operation);
  static const std::shared_ptr<GraphIdentity>& graph(const Region& region);

  static std::uint64_t id(const Value& value);
  static std::uint64_t id(const Operation& operation);
  static std::uint64_t id(const Region& region);
  static Region root(const Graph& graph);

  static void locate(Graph::Edit& edit, const Operation& operation,
                     SourceRange source);
  static std::optional<SourceRange> location(const Operation& operation);
  static std::optional<ParameterValue> property(const Operation& operation,
                                                std::string_view name);
  static bool verify_structure(const Graph& graph, Diagnostics& diagnostics);
  static bool verify_contracts(const Graph& graph, Diagnostics& diagnostics);
};

struct PropertyAccess {
  static std::string take_name(Property& property);
  static ParameterValue take_value(Property& property);
};

}  // namespace joggle::detail
