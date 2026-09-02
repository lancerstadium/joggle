#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>

#include <joggle/joggle.h>

namespace {

bool lane_operation(const joggle::Operation& operation,
                    joggle::Diagnostics& diagnostics) {
  const auto lanes = operation.get<std::int64_t>("lanes");
  if (!lanes || *lanes <= 0) {
    diagnostics.report("edgevec lane count must be positive");
    return false;
  }
  return true;
}

bool bind(joggle::Compiler& compiler, const joggle::Module& module,
          joggle::Diagnostics& diagnostics) {
  const auto dot = module.operation("dot");
  const auto clamp = module.operation("clamp");
  const auto miniai = compiler.module("miniai");
  const auto dense = miniai ? miniai->operation("dense") : std::nullopt;
  const auto relu = miniai ? miniai->operation("relu") : std::nullopt;
  const auto lower = module.pass("lower");
  if (!dot || !clamp || !dense || !relu || !lower) {
    diagnostics.report("edgevec behavior does not match its linked schema");
    return false;
  }

  compiler.bind(*dot,
                [](const joggle::Operation& operation,
                   joggle::Diagnostics& operation_diagnostics) {
                  return lane_operation(operation, operation_diagnostics);
                });
  compiler.bind(*clamp,
                [](const joggle::Operation& operation,
                   joggle::Diagnostics& operation_diagnostics) {
                  return lane_operation(operation, operation_diagnostics);
                });
  const auto read_lanes =
      [](const joggle::Operation& operation) -> std::optional<std::int64_t> {
    return operation.get<std::int64_t>("lanes");
  };
  compiler.bind(*dot, "lanes", read_lanes);
  compiler.bind(*clamp, "lanes", read_lanes);
  compiler.bind(*dot, "cycles",
                [](const joggle::Operation&) { return std::int64_t{4}; });
  compiler.bind(*clamp, "cycles",
                [](const joggle::Operation&) { return std::int64_t{1}; });

  compiler.bind(
      *lower,
      [dot = *dot, clamp = *clamp, dense = *dense, relu = *relu](
          joggle::Graph& graph, joggle::Diagnostics& pass_diagnostics) {
        const auto operations = graph.all_operations();
        const bool needed = std::any_of(
            operations.begin(), operations.end(), [&](const auto& operation) {
              return operation.schema() == dense || operation.schema() == relu;
            });
        if (!needed) {
          return true;
        }

        auto edit = graph.edit();
        for (const joggle::Operation& operation : operations) {
          const joggle::Module::OperationDecl* target = nullptr;
          if (operation.schema() == dense) {
            target = &dot;
          } else if (operation.schema() == relu) {
            target = &clamp;
          } else {
            continue;
          }

          edit.replace(operation, *target);
        }
        return edit.commit(pass_diagnostics);
      });
  return true;
}

}  // namespace

JOGGLE_EXPORT_BEHAVIOR(bind)
