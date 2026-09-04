#include <algorithm>
#include <bit>
#include <cstddef>
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

struct Encoding {
  std::uint32_t element_bits = 0;
  std::uint32_t word_bits = 0;
  std::uint32_t lanes = 0;
  bool is_signed = false;
  bool most_significant_first = false;
};

std::optional<Encoding>
encoding(const joggle::Type& element, const joggle::Type& storage,
         std::string_view order, joggle::Diagnostics& diagnostics) {
  const auto element_bits = bits(element);
  const auto word_bits = bits(storage);
  const auto is_signed = element.get<bool>("signed");
  if (!named(element, "bitpack", "integer") || !element_bits || !word_bits ||
      !is_signed || *element_bits <= 0 || *element_bits > 64 ||
      *word_bits <= 0 || *word_bits > 64 || *word_bits % 8 != 0 ||
      *word_bits % *element_bits != 0 ||
      (!*is_signed && *element_bits == 64) ||
      (order != "lsb" && order != "msb")) {
    diagnostics.report(
        "bitpack encoding needs a 1..64-bit signed or 1..63-bit unsigned "
        "integer, byte-aligned storage, exact divisibility, and lsb or msb "
        "lane order");
    return std::nullopt;
  }
  return Encoding{static_cast<std::uint32_t>(*element_bits),
                  static_cast<std::uint32_t>(*word_bits),
                  static_cast<std::uint32_t>(*word_bits / *element_bits),
                  *is_signed, order == "msb"};
}

std::uint64_t mask(std::uint32_t width) {
  return width == 64U ? std::numeric_limits<std::uint64_t>::max()
                      : (std::uint64_t{1} << width) - 1U;
}

std::optional<joggle::Bytes>
encode(const std::vector<std::int64_t>& values,
       const joggle::Type& element, const joggle::Type& storage,
       std::string order, joggle::Diagnostics& diagnostics) {
  const auto format = encoding(element, storage, order, diagnostics);
  if (!format || values.size() % format->lanes != 0U) {
    if (format) {
      diagnostics.report(
          "bitpack.encode needs a whole number of storage words");
    }
    return std::nullopt;
  }
  const std::uint64_t lane_mask = mask(format->element_bits);
  const std::int64_t signed_min =
      format->element_bits == 64U
          ? std::numeric_limits<std::int64_t>::min()
          : -(std::int64_t{1} << (format->element_bits - 1U));
  const std::int64_t signed_max =
      format->element_bits == 64U
          ? std::numeric_limits<std::int64_t>::max()
          : (std::int64_t{1} << (format->element_bits - 1U)) - 1;
  joggle::Bytes result;
  result.reserve(values.size() / format->lanes * (format->word_bits / 8U));
  for (std::size_t first = 0; first < values.size();
       first += format->lanes) {
    std::uint64_t word = 0;
    for (std::uint32_t lane = 0; lane < format->lanes; ++lane) {
      const std::int64_t value = values[first + lane];
      const bool in_range = format->is_signed
                                ? value >= signed_min && value <= signed_max
                                : value >= 0 &&
                                      static_cast<std::uint64_t>(value) <=
                                          lane_mask;
      if (!in_range) {
        diagnostics.report("bitpack.encode value is outside its element "
                           "format");
        return std::nullopt;
      }
      const std::uint32_t physical =
          format->most_significant_first ? format->lanes - 1U - lane : lane;
      word |= (static_cast<std::uint64_t>(value) & lane_mask)
              << (physical * format->element_bits);
    }
    for (std::uint32_t byte = 0; byte < format->word_bits / 8U; ++byte) {
      result.push_back(static_cast<std::byte>((word >> (byte * 8U)) & 0xffU));
    }
  }
  return result;
}

std::optional<std::vector<std::int64_t>>
decode(const joggle::Bytes& input, const joggle::Type& element,
       const joggle::Type& storage, std::string order,
       joggle::Diagnostics& diagnostics) {
  const auto format = encoding(element, storage, order, diagnostics);
  const std::size_t word_bytes =
      format ? static_cast<std::size_t>(format->word_bits / 8U) : 0U;
  if (!format || input.size() % word_bytes != 0U) {
    if (format) {
      diagnostics.report("bitpack.decode input ends inside a storage word");
    }
    return std::nullopt;
  }
  const std::uint64_t lane_mask = mask(format->element_bits);
  const std::uint64_t sign =
      std::uint64_t{1} << (format->element_bits - 1U);
  std::vector<std::int64_t> result;
  result.reserve(input.size() / word_bytes * format->lanes);
  for (std::size_t first = 0; first < input.size(); first += word_bytes) {
    std::uint64_t word = 0;
    for (std::size_t byte = 0; byte < word_bytes; ++byte) {
      word |= static_cast<std::uint64_t>(
                  std::to_integer<unsigned char>(input[first + byte]))
              << (byte * 8U);
    }
    for (std::uint32_t lane = 0; lane < format->lanes; ++lane) {
      const std::uint32_t physical =
          format->most_significant_first ? format->lanes - 1U - lane : lane;
      std::uint64_t value =
          (word >> (physical * format->element_bits)) & lane_mask;
      if (format->is_signed && format->element_bits < 64U &&
          (value & sign) != 0U) {
        value |= ~lane_mask;
      }
      result.push_back(std::bit_cast<std::int64_t>(value));
    }
  }
  return result;
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
  compiler.bind(module, "encode", encode);
  compiler.bind(module, "decode", decode);
  compiler.bind(
      module, "run",
      [module](joggle::Compiler& active, joggle::Function input,
               const joggle::Type& storage, std::int64_t axis,
               std::string order, joggle::Diagnostics& run_diagnostics) {
        return run(active, std::move(input), storage, axis, std::move(order),
                   module, run_diagnostics);
      });
}
