#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <span>
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
                 const joggle::Module::InterfaceDecl& immutable_data,
                 const joggle::Module& source, joggle::Module& destination,
                 joggle::Diagnostics& diagnostics) {
  std::map<std::string, std::string, std::less<>> resource_names;
  for (const auto& op : input.ops()) {
    if (!compiler.conforms(op.callee(), immutable_data)) {
      continue;
    }
    const auto resource = op.property<std::string>("resource");
    const auto payload = resource ? source.data(*resource) : std::nullopt;
    if (!resource || !payload) {
      diagnostics.report("constant references missing Module data");
      return std::nullopt;
    }
    if (op.results().size() != 1U ||
        !is_f32_tensor(op.value().type(), ranked, f32)) {
      const std::string copied =
          destination.store(joggle::Bytes(payload->begin(), payload->end()));
      if (copied != *resource) {
        diagnostics.report("content-addressed Module data changed while copying");
        return std::nullopt;
      }
      resource_names.insert_or_assign(*resource, copied);
      continue;
    }
    const auto shape =
        op.value().type().get<std::vector<std::int64_t>>("shape");
    std::size_t expected = 4U;
    if (!shape) {
      diagnostics.report("f32 tensor constant has no ranked shape");
      return std::nullopt;
    }
    const joggle::Bytes source_bytes(payload->begin(), payload->end());
    const auto converted =
        convert_payload(source_bytes, *resource, diagnostics);
    if (!converted) {
      return std::nullopt;
    }
    resource_names.insert_or_assign(*resource,
                                    destination.store(*converted));
    for (const std::int64_t dimension : *shape) {
      if (dimension <= 0 ||
          static_cast<std::uint64_t>(dimension) >
              std::numeric_limits<std::size_t>::max() / expected) {
        diagnostics.report("f32 tensor constant has an invalid byte size");
        return std::nullopt;
      }
      expected *= static_cast<std::size_t>(dimension);
    }
    if (payload->size() != expected) {
      diagnostics.report(
          "f32 tensor constant data size disagrees with its type");
      return std::nullopt;
    }
  }

  auto output = joggle::clone(
      compiler, input,
      [&](const joggle::Type& type) {
        return convert_type(compiler, type, ranked, f32, f16, diagnostics);
      },
      diagnostics);
  if (!output) {
    return std::nullopt;
  }

  const auto string_type = compiler.make("string");
  if (!string_type) {
    return std::nullopt;
  }
  const auto changed = joggle::rewrite(
      *output,
      [&](const joggle::Op& op, joggle::Function::Edit& edit,
          joggle::Diagnostics& reported) {
        if (!compiler.conforms(op.callee(), immutable_data) ||
            op.results().size() != 1U) {
          return false;
        }
        const auto old_name = op.property<std::string>("resource");
        const auto mapped_name = old_name ? resource_names.find(*old_name)
                                          : resource_names.end();
        if (!old_name || mapped_name == resource_names.end()) {
          reported.report("constant has no converted Module data");
          return false;
        }
        if (mapped_name->second == *old_name) {
          return false;
        }
        const auto name = compiler.known(*string_type, mapped_name->second);
        if (!name) {
          return false;
        }
        std::vector<joggle::Value> arguments = op.arguments();
        const auto inputs = op.callee().inputs();
        const auto resource = std::find_if(
            inputs.begin(), inputs.end(),
            [](const auto& input) { return input.name == "resource"; });
        if (resource == inputs.end()) {
          reported.report("immutable data function has no resource property");
          return false;
        }
        arguments[static_cast<std::size_t>(resource - inputs.begin())] = *name;
        const auto replacement =
            edit.insert(op, op.callee(), std::move(arguments),
                        {op.value().type()});
        edit.replace(op, replacement.results());
        return true;
      },
      diagnostics);
  return changed ? output : std::nullopt;
}

std::optional<joggle::Module>
f32_to_f16(joggle::Compiler& compiler, joggle::Module input,
           joggle::Diagnostics& diagnostics) {
  const auto tensor = compiler.module("tensor");
  const auto ranked = tensor ? tensor->type("ranked") : std::nullopt;
  const auto immutable_data =
      tensor ? tensor->interface("immutable_data") : std::nullopt;
  const auto f32 = compiler.make("f32");
  const auto f16 = compiler.make("f16");
  if (!ranked || !immutable_data || !f32 || !f16) {
    diagnostics.report("precision behavior requires tensor@2");
    return std::nullopt;
  }

  joggle::Module output(std::string(input.name()), input.version());
  for (const auto& declaration : input.functions()) {
    if (declaration.body() == nullptr) {
      diagnostics.report("f32_to_f16 requires every input Function to have a body");
      return std::nullopt;
    }
    const auto ops = declaration.body()->ops();
    const auto internal_call = std::find_if(
        ops.begin(), ops.end(),
        [&](const joggle::Op& op) {
          return op.callee().symbol().module_name() == input.name();
        });
    if (internal_call != ops.end()) {
      diagnostics.report(
          "f32_to_f16 currently rejects calls between input Module Functions");
      return std::nullopt;
    }
    auto function = convert_function(compiler, *declaration.body(), *ranked,
                                     f32->schema(), *f16, *immutable_data,
                                     input, output, diagnostics);
    if (!function ||
        !output.insert(std::string(declaration.name()), std::move(*function),
                       diagnostics)) {
      return std::nullopt;
    }
  }

  if (!compiler.verify(output)) {
    return std::nullopt;
  }
  return output;
}

void bind(joggle::Compiler& compiler, const joggle::Module& module,
          joggle::Diagnostics&) {
  compiler.bind(module, "f32_to_f16", f32_to_f16);
}

}  // namespace

JOGGLE_EXPORT_BEHAVIOR(bind)
