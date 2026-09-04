#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <joggle/joggle.h>

namespace {

using Shape = std::vector<std::int64_t>;

bool named(const joggle::Type& type, std::string_view module,
           std::string_view name) {
  return type.schema().symbol().module_name() == module &&
         type.schema().symbol().local_name() == name;
}

std::optional<std::int64_t> bits(const joggle::Type& type) {
  return type.get<std::int64_t>("storage_bits");
}

bool verify_integer(const joggle::Type& type,
                    joggle::Diagnostics& diagnostics) {
  const auto width = type.get<std::int64_t>("bits");
  if (!width || *width <= 0 || *width > 64) {
    diagnostics.report("bitpack.integer bits must be between 1 and 64");
    return false;
  }
  return true;
}

bool verify_packed(const joggle::Type& type,
                   joggle::Diagnostics& diagnostics) {
  const auto logical = type.get<joggle::Type>("logical");
  const auto storage = type.get<joggle::Type>("storage");
  const auto axis = type.get<std::int64_t>("axis");
  const auto lanes = type.get<std::int64_t>("lanes");
  const auto order = type.get<std::string>("order");
  if (!logical || !storage || !axis || !lanes || !order ||
      !named(*logical, "tensor", "tensor") ||
      !named(*storage, "tensor", "tensor")) {
    diagnostics.report(
        "bitpack.packed needs logical and storage tensor Types");
    return false;
  }
  const auto logical_element = logical->get<joggle::Type>("element");
  const auto storage_element = storage->get<joggle::Type>("element");
  const auto logical_shape = logical->get<Shape>("shape");
  const auto storage_shape = storage->get<Shape>("shape");
  const auto logical_bits = logical_element ? bits(*logical_element)
                                            : std::nullopt;
  const auto storage_bits = storage_element ? bits(*storage_element)
                                            : std::nullopt;
  if (!logical_shape || !storage_shape || !logical_bits || !storage_bits ||
      logical_shape->size() != storage_shape->size() || *lanes <= 0 ||
      *axis < 0 ||
      static_cast<std::size_t>(*axis) >= logical_shape->size() ||
      (*order != "lsb" && *order != "msb")) {
    diagnostics.report("bitpack.packed has an invalid packing description");
    return false;
  }
  if (*logical_bits > std::numeric_limits<std::int64_t>::max() / *lanes ||
      *logical_bits * *lanes != *storage_bits) {
    diagnostics.report(
        "bitpack.packed lanes must exactly fill one storage element");
    return false;
  }
  for (std::size_t index = 0; index < logical_shape->size(); ++index) {
    if ((*logical_shape)[index] < 0 || (*storage_shape)[index] < 0) {
      diagnostics.report("bitpack.packed shapes must be static");
      return false;
    }
    if (index == static_cast<std::size_t>(*axis)) {
      if ((*storage_shape)[index] >
              std::numeric_limits<std::int64_t>::max() / *lanes ||
          (*storage_shape)[index] * *lanes != (*logical_shape)[index]) {
        diagnostics.report(
            "bitpack.packed storage shape does not pack the logical axis");
        return false;
      }
    } else if ((*logical_shape)[index] != (*storage_shape)[index]) {
      diagnostics.report(
          "bitpack.packed may change only its declared packing axis");
      return false;
    }
  }
  return true;
}

std::optional<joggle::Module::FunctionDecl>
counterpart(const joggle::Module& module, const joggle::Op& op) {
  const auto overloads = module.overloads(op.callee().name());
  const auto found = std::find_if(
      overloads.begin(), overloads.end(), [&](const auto& declaration) {
        return declaration.inputs().size() == op.arguments().size() &&
               declaration.results().size() == op.results().size();
      });
  return found == overloads.end()
             ? std::optional<joggle::Module::FunctionDecl>{}
             : std::optional<joggle::Module::FunctionDecl>{*found};
}

std::optional<joggle::Function>
run(joggle::Compiler& compiler, joggle::Function input,
    const joggle::Type& word, std::int64_t axis, std::string order,
    const joggle::Module& module, joggle::Diagnostics& diagnostics) {
  const auto tensor_module = compiler.module("tensor");
  const auto tensor = tensor_module ? tensor_module->type("tensor")
                                    : std::nullopt;
  const auto packed = module.type("packed");
  const auto word_bits = bits(word);
  if (!tensor || !packed || !word_bits || *word_bits <= 0 ||
      (order != "lsb" && order != "msb")) {
    diagnostics.report("bitpack.run received an invalid format");
    return std::nullopt;
  }

  const auto map_type = [&](const joggle::Type& type)
      -> std::optional<joggle::Type> {
    if (!named(type, "tensor", "tensor")) {
      return type;
    }
    const auto element = type.get<joggle::Type>("element");
    const auto shape = type.get<Shape>("shape");
    const auto element_bits = element ? bits(*element) : std::nullopt;
    if (!element || !shape || !element_bits || *element_bits <= 0 ||
        *word_bits % *element_bits != 0 || axis < 0 ||
        static_cast<std::size_t>(axis) >= shape->size()) {
      return std::nullopt;
    }
    const std::int64_t lanes = *word_bits / *element_bits;
    if (lanes <= 0 || (*shape)[static_cast<std::size_t>(axis)] < 0 ||
        (*shape)[static_cast<std::size_t>(axis)] % lanes != 0) {
      return std::nullopt;
    }
    Shape physical = *shape;
    physical[static_cast<std::size_t>(axis)] /= lanes;
    auto storage = compiler.make(*tensor, word, physical);
    return storage ? compiler.make(*packed, type, *storage, axis, lanes, order)
                   : std::nullopt;
  };

  auto transformed = joggle::clone(
      compiler, input,
      [&](const joggle::Value& value) { return map_type(value.type()); },
      [&](const joggle::Op& op)
          -> std::optional<joggle::Module::FunctionDecl> {
        if (op.callee().symbol().module_name() != "tensor") {
          return op.callee();
        }
        return counterpart(module, op);
      },
      diagnostics);
  if (!transformed) {
    diagnostics.report(
        "bitpack.run cannot represent every type and call in the Function");
    return std::nullopt;
  }
  const joggle::TypeProjection logical = [](const joggle::Type& type) {
    if (named(type, "bitpack", "packed")) {
      return type.get<joggle::Type>("logical");
    }
    return std::optional<joggle::Type>{type};
  };
  if (!joggle::equivalent(compiler, input, *transformed, logical,
                          diagnostics)) {
    return std::nullopt;
  }
  return transformed;
}

}  // namespace

void joggle_module(joggle::Compiler& compiler, const joggle::Module& module,
                   joggle::Diagnostics& diagnostics) {
  const auto integer = module.type("integer");
  const auto packed = module.type("packed");
  if (!integer || !packed) {
    diagnostics.report("bitpack native does not match its Module source");
    return;
  }
  compiler.verify(*integer, verify_integer);
  compiler.verify(*packed, verify_packed);
  compiler.bind(
      module, "run",
      [module](joggle::Compiler& active, joggle::Function input,
               const joggle::Type& storage, std::int64_t axis,
               std::string order, joggle::Diagnostics& run_diagnostics) {
        return run(active, std::move(input), storage, axis, std::move(order),
                   module, run_diagnostics);
      });
}
