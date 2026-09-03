#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <joggle/joggle.h>

namespace {

std::uint16_t half(std::uint32_t bits) {
  const std::uint16_t sign =
      static_cast<std::uint16_t>((bits >> 16U) & 0x8000U);
  const std::uint32_t exponent = (bits >> 23U) & 0xffU;
  const std::uint32_t fraction = bits & 0x7fffffU;
  if (exponent == 0xffU) {
    if (fraction == 0U) {
      return static_cast<std::uint16_t>(sign | 0x7c00U);
    }
    std::uint16_t payload = static_cast<std::uint16_t>(fraction >> 13U);
    payload = static_cast<std::uint16_t>(payload | 0x0200U);
    return static_cast<std::uint16_t>(sign | 0x7c00U | payload);
  }

  const std::int32_t target_exponent =
      static_cast<std::int32_t>(exponent) - 127 + 15;
  if (target_exponent >= 31) {
    return static_cast<std::uint16_t>(sign | 0x7c00U);
  }
  if (target_exponent <= 0) {
    if (target_exponent < -10) {
      return sign;
    }
    const std::uint32_t significand = fraction | 0x800000U;
    const std::uint32_t shift =
        static_cast<std::uint32_t>(14 - target_exponent);
    std::uint32_t rounded = significand >> shift;
    const std::uint32_t remainder =
        significand & ((std::uint32_t{1} << shift) - 1U);
    const std::uint32_t halfway = std::uint32_t{1} << (shift - 1U);
    if (remainder > halfway || (remainder == halfway && (rounded & 1U) != 0U)) {
      ++rounded;
    }
    return static_cast<std::uint16_t>(sign | rounded);
  }

  std::uint32_t rounded =
      (static_cast<std::uint32_t>(target_exponent) << 10U) |
      (fraction >> 13U);
  const std::uint32_t remainder = fraction & 0x1fffU;
  if (remainder > 0x1000U ||
      (remainder == 0x1000U && (rounded & 1U) != 0U)) {
    ++rounded;
  }
  return static_cast<std::uint16_t>(sign | rounded);
}

std::optional<joggle::Bytes>
convert_payload(const joggle::Bytes& input, std::string_view resource,
                joggle::Diagnostics& diagnostics) {
  if (input.size() % 4U != 0U) {
    diagnostics.report("f32 resource '" + std::string(resource) +
                       "' does not contain whole 32-bit elements");
    return std::nullopt;
  }
  joggle::Bytes output;
  output.reserve(input.size() / 2U);
  for (std::size_t offset = 0; offset < input.size(); offset += 4U) {
    std::uint32_t bits = 0;
    for (std::uint32_t byte = 0; byte < 4U; ++byte) {
      bits |= std::to_integer<std::uint32_t>(input[offset + byte])
              << (byte * 8U);
    }
    const std::uint16_t value = half(bits);
    output.push_back(static_cast<std::byte>(value & 0xffU));
    output.push_back(static_cast<std::byte>(value >> 8U));
  }
  return output;
}

bool is_f32_tensor(const joggle::Type& type,
                   const joggle::Module::TypeDecl& ranked,
                   const joggle::Module::TypeDecl& f32) {
  const auto element = type.get<joggle::Type>("element");
  return type.schema() == ranked && element && element->schema() == f32;
}

std::optional<joggle::Type>
convert_type(joggle::Compiler& compiler, const joggle::Type& input,
             const joggle::Module::TypeDecl& ranked,
             const joggle::Module::TypeDecl& f32,
             const joggle::Type& f16, joggle::Diagnostics& diagnostics) {
  if (input.schema() != ranked) {
    return input;
  }
  const auto element = input.get<joggle::Type>("element");
  const auto shape = input.get<std::vector<std::int64_t>>("shape");
  if (!element || !shape) {
    diagnostics.report("ranked tensor type is missing element or shape data");
    return std::nullopt;
  }
  if (element->schema() != f32) {
    return input;
  }
  return compiler.make(ranked, f16, *shape);
}

std::optional<joggle::Function>
convert_function(joggle::Compiler& compiler, const joggle::Function& input,
                 const joggle::Module::TypeDecl& ranked,
                 const joggle::Module::TypeDecl& f32,
                 const joggle::Type& f16,
                 const joggle::Module::FunctionDecl& constant,
                 joggle::ResourceSet& resources,
                 std::set<std::string, std::less<>>& converted_resources,
                 joggle::Diagnostics& diagnostics) {
  const auto blocks = input.blocks();
  if (blocks.size() != 1U ||
      blocks.front().terminator().kind() != joggle::Terminator::Kind::Return) {
    diagnostics.report("f32_to_f16 currently requires straight-line Functions");
    return std::nullopt;
  }
  auto output = compiler.create_function();
  if (!output) {
    return std::nullopt;
  }
  auto edit = output->edit();
  std::vector<std::pair<joggle::Value, joggle::Value>> values;
  for (const auto& argument : input.arguments()) {
    const auto type =
        convert_type(compiler, argument.type(), ranked, f32, f16, diagnostics);
    if (!type) {
      return std::nullopt;
    }
    values.emplace_back(argument, edit.argument(*type));
  }
  const auto mapped = [&](const joggle::Value& value)
      -> std::optional<joggle::Value> {
    if (value.known()) {
      return value;
    }
    const auto found = std::find_if(
        values.begin(), values.end(),
        [&](const auto& item) { return item.first == value; });
    return found == values.end() ? std::nullopt
                                 : std::optional<joggle::Value>{found->second};
  };

  for (const auto& instruction : input.instructions()) {
    std::vector<joggle::Value> arguments;
    arguments.reserve(instruction.arguments().size());
    for (const auto& argument : instruction.arguments()) {
      const auto value = mapped(argument);
      if (!value) {
        diagnostics.report("f32_to_f16 encountered an unmapped operand");
        return std::nullopt;
      }
      arguments.push_back(*value);
    }
    std::vector<joggle::Type> result_types;
    result_types.reserve(instruction.results().size());
    for (const auto& result : instruction.results()) {
      const auto type =
          convert_type(compiler, result.type(), ranked, f32, f16, diagnostics);
      if (!type) {
        return std::nullopt;
      }
      result_types.push_back(*type);
    }

    if (instruction.callee() == constant && instruction.results().size() == 1U &&
        is_f32_tensor(instruction.value().type(), ranked, f32)) {
      const auto resource = instruction.get<std::string>("resource");
      const auto payload = resource ? resources.find(*resource) : resources.end();
      if (!resource || payload == resources.end()) {
        diagnostics.report("f32 tensor constant references a missing resource");
        return std::nullopt;
      }
      const auto shape = instruction.value().type().get<
          std::vector<std::int64_t>>("shape");
      std::size_t expected = 4U;
      if (!shape) {
        diagnostics.report("f32 tensor constant has no ranked shape");
        return std::nullopt;
      }
      for (const std::int64_t dimension : *shape) {
        if (dimension <= 0 ||
            static_cast<std::uint64_t>(dimension) >
                std::numeric_limits<std::size_t>::max() / expected) {
          diagnostics.report("f32 tensor constant has an invalid byte size");
          return std::nullopt;
        }
        expected *= static_cast<std::size_t>(dimension);
      }
      if (payload->second.size() != expected) {
        diagnostics.report("f32 tensor constant resource size disagrees with "
                           "its type");
        return std::nullopt;
      }
      const auto converted = convert_payload(payload->second, *resource, diagnostics);
      if (!converted) {
        return std::nullopt;
      }
      const std::string raw(reinterpret_cast<const char*>(converted->data()),
                            converted->size());
      const std::string name = "sha256:" + joggle::sha256(raw);
      const auto existing = resources.find(name);
      if (existing != resources.end() && existing->second != *converted) {
        diagnostics.report("resource name collision while encoding f16 data");
        return std::nullopt;
      }
      resources.try_emplace(name, *converted);
      converted_resources.insert(*resource);
      const auto known = compiler.make("string");
      const auto resource_value = known ? compiler.known(*known, name) : std::nullopt;
      if (!resource_value) {
        return std::nullopt;
      }
      arguments = {*resource_value};
    }

    std::optional<joggle::Instruction> created;
    try {
      created = edit.append(instruction.callee(), std::move(arguments),
                            std::move(result_types));
    } catch (const std::exception& error) {
      diagnostics.report("f32_to_f16 cannot rebuild call '" +
                         std::string(instruction.callee().symbol().qualified_name()) +
                         "': " + error.what());
      return std::nullopt;
    }
    const auto old_results = instruction.results();
    const auto new_results = created->results();
    for (std::size_t index = 0; index < old_results.size(); ++index) {
      values.emplace_back(old_results[index], new_results[index]);
    }
  }

  std::vector<joggle::Value> returned;
  for (const auto& value : blocks.front().terminator().returned()) {
    const auto result = mapped(value);
    if (!result) {
      diagnostics.report("f32_to_f16 encountered an unmapped return value");
      return std::nullopt;
    }
    returned.push_back(*result);
  }
  edit.ret(output->entry(), std::move(returned));
  return edit.commit(diagnostics) ? std::move(output) : std::nullopt;
}

std::optional<std::tuple<joggle::Module, joggle::ResourceSet>>
f32_to_f16(joggle::Compiler& compiler, joggle::Module input,
           joggle::ResourceSet resources, joggle::Diagnostics& diagnostics) {
  const auto tensor = compiler.module("tensor");
  const auto ranked = tensor ? tensor->type("ranked") : std::nullopt;
  const auto constant = tensor ? tensor->function("constant") : std::nullopt;
  const auto f32 = compiler.make("f32");
  const auto f16 = compiler.make("f16");
  if (!ranked || !constant || !f32 || !f16) {
    diagnostics.report("precision behavior requires tensor@2");
    return std::nullopt;
  }

  joggle::Module output(std::string(input.name()), input.version());
  std::set<std::string, std::less<>> converted_resources;
  for (const auto& declaration : input.functions()) {
    if (declaration.body() == nullptr) {
      diagnostics.report("f32_to_f16 requires every input Function to have a body");
      return std::nullopt;
    }
    const auto instructions = declaration.body()->instructions();
    const auto internal_call = std::find_if(
        instructions.begin(), instructions.end(),
        [&](const joggle::Instruction& instruction) {
          return instruction.callee().symbol().module_name() == input.name();
        });
    if (internal_call != instructions.end()) {
      diagnostics.report(
          "f32_to_f16 currently rejects calls between input Module Functions");
      return std::nullopt;
    }
    auto function = convert_function(compiler, *declaration.body(), *ranked,
                                     f32->schema(), *f16, *constant, resources,
                                     converted_resources, diagnostics);
    if (!function ||
        !output.insert(std::string(declaration.name()), std::move(*function),
                       diagnostics)) {
      return std::nullopt;
    }
  }

  std::set<std::string, std::less<>> referenced;
  for (const auto& declaration : output.functions()) {
    for (const auto& instruction : declaration.body()->instructions()) {
      if (instruction.callee() == *constant) {
        const auto resource = instruction.get<std::string>("resource");
        if (resource) {
          referenced.insert(*resource);
        }
      }
    }
  }
  for (const auto& resource : converted_resources) {
    if (!referenced.contains(resource)) {
      resources.erase(resource);
    }
  }
  if (!compiler.verify(output)) {
    return std::nullopt;
  }
  return std::tuple{std::move(output), std::move(resources)};
}

void bind(joggle::Compiler& compiler, const joggle::Module& module,
          joggle::Diagnostics& diagnostics) {
  const auto resource = compiler.module("resource");
  const auto set = resource ? resource->type("set") : std::nullopt;
  if (!set || !compiler.represent<joggle::ResourceSet>(*set)) {
    diagnostics.report("precision behavior requires resource@1");
    return;
  }
  compiler.bind(module, "f32_to_f16", f32_to_f16);
}

}  // namespace

JOGGLE_EXPORT_BEHAVIOR(bind)
