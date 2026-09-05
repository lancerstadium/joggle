#include "import.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <locale>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "onnx.pb.h"

namespace joggle_onnx {
namespace {

using Shape = std::vector<std::int64_t>;

struct Tensor {
  joggle::Val value;
  std::int32_t element = 0;
  Shape shape;
};

struct Initializer {
  std::int32_t element = 0;
  Shape shape;
  joggle::Bytes bytes;
  std::vector<std::int64_t> integers;
};


bool fail(joggle::Diag& diagnostics, std::string message) {
  diagnostics.report("onnx.read: " + std::move(message));
  return false;
}

bool valid_identifier(std::string_view name) {
  if (name.empty()) {
    return false;
  }
  const auto first = static_cast<unsigned char>(name.front());
  if (std::isalpha(first) == 0 && name.front() != '_') {
    return false;
  }
  return std::all_of(name.begin() + 1, name.end(), [](char value) {
    const auto character = static_cast<unsigned char>(value);
    return std::isalnum(character) != 0 || value == '_';
  });
}

std::optional<std::size_t> element_count(std::span<const std::int64_t> shape) {
  std::size_t count = 1;
  for (const auto dimension : shape) {
    if (dimension < 0) {
      return std::nullopt;
    }
    const auto size = static_cast<std::uint64_t>(dimension);
    if (size > std::numeric_limits<std::size_t>::max() ||
        (size != 0 && count > std::numeric_limits<std::size_t>::max() /
                                  static_cast<std::size_t>(size))) {
      return std::nullopt;
    }
    count *= static_cast<std::size_t>(size);
  }
  return count;
}

void append_u32(joggle::Bytes& output, std::uint32_t value) {
  for (unsigned shift = 0; shift < 32; shift += 8) {
    output.push_back(
        static_cast<std::byte>((value >> shift) & std::uint32_t{0xff}));
  }
}

void append_u64(joggle::Bytes& output, std::uint64_t value) {
  for (unsigned shift = 0; shift < 64; shift += 8) {
    output.push_back(
        static_cast<std::byte>((value >> shift) & std::uint64_t{0xff}));
  }
}

std::optional<Initializer> decode_initializer(const onnx::TensorProto& tensor,
                                              joggle::Diag& diagnostics) {
  Initializer result;
  result.element = tensor.data_type();
  result.shape.assign(tensor.dims().begin(), tensor.dims().end());
  const auto count = element_count(result.shape);
  if (!count) {
    fail(diagnostics, "initializer '" + tensor.name() +
                          "' has an invalid or overflowing shape");
    return std::nullopt;
  }
  if (tensor.has_segment() ||
      tensor.data_location() == onnx::TensorProto_DataLocation_EXTERNAL ||
      tensor.external_data_size() != 0) {
    fail(diagnostics, "initializer '" + tensor.name() +
                          "' uses segmented or external storage");
    return std::nullopt;
  }

  const bool has_raw = !tensor.raw_data().empty();
  const auto byte_count = [&](std::size_t width) -> std::optional<std::size_t> {
    if (*count > std::numeric_limits<std::size_t>::max() / width) {
      fail(diagnostics,
           "initializer '" + tensor.name() + "' byte count overflows");
      return std::nullopt;
    }
    return *count * width;
  };
  const auto copy_raw = [&](std::size_t width) {
    const auto expected = byte_count(width);
    if (!expected || tensor.raw_data().size() != *expected) {
      if (!expected) {
        return false;
      }
      fail(diagnostics, "initializer '" + tensor.name() +
                            "' raw byte count does not match its shape");
      return false;
    }
    result.bytes.reserve(tensor.raw_data().size());
    for (const char value : tensor.raw_data()) {
      result.bytes.push_back(
          static_cast<std::byte>(static_cast<unsigned char>(value)));
    }
    return true;
  };
  if (result.element == onnx::TensorProto_DataType_FLOAT) {
    if (tensor.int32_data_size() != 0 || tensor.int64_data_size() != 0 ||
        tensor.double_data_size() != 0 || tensor.uint64_data_size() != 0 ||
        tensor.string_data_size() != 0 ||
        (has_raw && tensor.float_data_size() != 0)) {
      fail(diagnostics, "initializer '" + tensor.name() +
                            "' has conflicting FLOAT storage fields");
      return std::nullopt;
    }
    if (has_raw) {
      if (!copy_raw(sizeof(float))) {
        return std::nullopt;
      }
    } else {
      if (static_cast<std::size_t>(tensor.float_data_size()) != *count) {
        fail(diagnostics, "initializer '" + tensor.name() +
                              "' FLOAT value count does not match shape");
        return std::nullopt;
      }
      const auto expected = byte_count(sizeof(float));
      if (!expected) {
        return std::nullopt;
      }
      result.bytes.reserve(*expected);
      for (const float value : tensor.float_data()) {
        append_u32(result.bytes, std::bit_cast<std::uint32_t>(value));
      }
    }
    return result;
  }

  if (result.element == onnx::TensorProto_DataType_UINT8 ||
      result.element == onnx::TensorProto_DataType_INT8 ||
      result.element == onnx::TensorProto_DataType_INT32) {
    if (tensor.float_data_size() != 0 || tensor.int64_data_size() != 0 ||
        tensor.double_data_size() != 0 || tensor.uint64_data_size() != 0 ||
        tensor.string_data_size() != 0 ||
        (has_raw && tensor.int32_data_size() != 0)) {
      fail(diagnostics, "initializer '" + tensor.name() +
                            "' has conflicting integer storage fields");
      return std::nullopt;
    }
    const std::size_t width =
        result.element == onnx::TensorProto_DataType_INT32 ? 4U : 1U;
    if (has_raw) {
      if (!copy_raw(width)) {
        return std::nullopt;
      }
      return result;
    }
    if (static_cast<std::size_t>(tensor.int32_data_size()) != *count) {
      fail(diagnostics, "initializer '" + tensor.name() +
                            "' integer value count does not match shape");
      return std::nullopt;
    }
    const auto expected = byte_count(width);
    if (!expected) {
      return std::nullopt;
    }
    result.bytes.reserve(*expected);
    for (const auto value : tensor.int32_data()) {
      if ((result.element == onnx::TensorProto_DataType_UINT8 &&
           (value < 0 || value > 255)) ||
          (result.element == onnx::TensorProto_DataType_INT8 &&
           (value < -128 || value > 127))) {
        fail(diagnostics, "initializer '" + tensor.name() +
                              "' has an out-of-range 8-bit value");
        return std::nullopt;
      }
      const auto bits = static_cast<std::uint32_t>(value);
      for (std::size_t byte = 0; byte < width; ++byte) {
        result.bytes.push_back(
            static_cast<std::byte>((bits >> (byte * 8U)) & 0xffU));
      }
    }
    return result;
  }

  if (result.element == onnx::TensorProto_DataType_INT64) {
    if (tensor.int32_data_size() != 0 || tensor.float_data_size() != 0 ||
        tensor.double_data_size() != 0 || tensor.uint64_data_size() != 0 ||
        tensor.string_data_size() != 0 ||
        (has_raw && tensor.int64_data_size() != 0)) {
      fail(diagnostics, "initializer '" + tensor.name() +
                            "' has conflicting INT64 storage fields");
      return std::nullopt;
    }
    if (has_raw) {
      if (!copy_raw(sizeof(std::int64_t))) {
        return std::nullopt;
      }
      result.integers.reserve(*count);
      for (std::size_t index = 0; index < *count; ++index) {
        std::uint64_t bits = 0;
        for (unsigned byte = 0; byte < 8; ++byte) {
          bits |= static_cast<std::uint64_t>(std::to_integer<unsigned char>(
                      result.bytes[index * 8 + byte]))
                  << (byte * 8);
        }
        result.integers.push_back(std::bit_cast<std::int64_t>(bits));
      }
    } else {
      if (static_cast<std::size_t>(tensor.int64_data_size()) != *count) {
        fail(diagnostics, "initializer '" + tensor.name() +
                              "' INT64 value count does not match shape");
        return std::nullopt;
      }
      result.integers.assign(tensor.int64_data().begin(),
                             tensor.int64_data().end());
      const auto expected = byte_count(sizeof(std::int64_t));
      if (!expected) {
        return std::nullopt;
      }
      result.bytes.reserve(*expected);
      for (const auto value : result.integers) {
        append_u64(result.bytes, static_cast<std::uint64_t>(value));
      }
    }
    return result;
  }

  fail(diagnostics, "initializer '" + tensor.name() +
                        "' has unsupported element type " +
                        std::to_string(result.element));
  return std::nullopt;
}

std::optional<std::pair<std::int32_t, Shape>>
value_type(const onnx::ValueInfoProto& value, joggle::Diag& diagnostics) {
  if (!value.has_type() || !value.type().has_tensor_type()) {
    fail(diagnostics, "value '" + value.name() + "' is not a typed tensor");
    return std::nullopt;
  }
  const auto& tensor = value.type().tensor_type();
  if (!tensor.has_shape()) {
    fail(diagnostics, "value '" + value.name() + "' has no static shape");
    return std::nullopt;
  }
  Shape shape;
  shape.reserve(static_cast<std::size_t>(tensor.shape().dim_size()));
  for (const auto& dimension : tensor.shape().dim()) {
    if (!dimension.has_dim_value() || dimension.dim_value() < 0) {
      fail(diagnostics, "value '" + value.name() +
                            "' has an absent, symbolic, or negative dimension");
      return std::nullopt;
    }
    shape.push_back(dimension.dim_value());
  }
  return std::pair{tensor.elem_type(), std::move(shape)};
}

struct Types {
  joggle::Mod::TypeDecl tensor;
  joggle::Mod::FnDecl constant;
  std::map<std::int32_t, joggle::Type> elements;
};

enum class Scalar { Integer, Real, Boolean, String, Type, Fn, Bytes };

struct Domain {
  Scalar element = Scalar::Integer;
  bool list = false;
};

std::optional<Domain> domain(const joggle::Mod::Expr& expression) {
  const joggle::Mod::Expr* element = &expression;
  bool list = false;
  if (expression.kind == joggle::Mod::Expr::Kind::Reference &&
      expression.text == "list" && expression.arguments.size() == 1U) {
    list = true;
    element = &expression.arguments.front();
  }
  if (element->kind != joggle::Mod::Expr::Kind::Reference ||
      !element->arguments.empty()) {
    return std::nullopt;
  }
  const std::map<std::string_view, Scalar> names{
      {"int", Scalar::Integer}, {"real", Scalar::Real},
      {"bool", Scalar::Boolean}, {"string", Scalar::String},
      {"type", Scalar::Type},    {"fn", Scalar::Fn},
      {"bytes", Scalar::Bytes},
  };
  const auto found = names.find(element->text);
  return found == names.end() ? std::nullopt
                              : std::optional<Domain>{{found->second, list}};
}

std::string_view scalar_name(Scalar scalar) {
  switch (scalar) {
  case Scalar::Integer:
    return "int";
  case Scalar::Real:
    return "real";
  case Scalar::Boolean:
    return "bool";
  case Scalar::String:
    return "string";
  case Scalar::Type:
    return "type";
  case Scalar::Fn:
    return "fn";
  case Scalar::Bytes:
    return "bytes";
  }
  return {};
}

std::optional<joggle::Type> domain_type(joggle::Compiler& compiler,
                                        Domain value) {
  const auto element = compiler.make(scalar_name(value.element));
  if (!element || !value.list) {
    return element;
  }
  const auto prelude = compiler.mod("prelude");
  const auto list = prelude ? prelude->type("list") : std::nullopt;
  return list ? compiler.make(*list, *element) : std::nullopt;
}

std::optional<std::int64_t> integer_literal(const joggle::Mod::Expr& value) {
  if (value.kind != joggle::Mod::Expr::Kind::Number) {
    return std::nullopt;
  }
  std::int64_t result = 0;
  const auto* begin = value.text.data();
  const auto* end = begin + value.text.size();
  const auto parsed = std::from_chars(begin, end, result);
  return parsed.ec == std::errc{} && parsed.ptr == end
             ? std::optional<std::int64_t>{result}
             : std::nullopt;
}

std::optional<double> real_literal(const joggle::Mod::Expr& value) {
  if (value.kind != joggle::Mod::Expr::Kind::Number) {
    return std::nullopt;
  }
  std::istringstream stream(value.text);
  stream.imbue(std::locale::classic());
  double result = 0.0;
  stream >> result;
  return stream && stream.eof() && std::isfinite(result)
             ? std::optional<double>{result}
             : std::nullopt;
}

template <typename T, typename Decode>
std::optional<std::vector<T>> literal_list(const joggle::Mod::Expr& expression,
                                           Decode decode) {
  if (expression.kind != joggle::Mod::Expr::Kind::List) {
    return std::nullopt;
  }
  std::vector<T> result;
  result.reserve(expression.arguments.size());
  for (const auto& element : expression.arguments) {
    auto value = decode(element);
    if (!value) {
      return std::nullopt;
    }
    result.push_back(std::move(*value));
  }
  return result;
}

std::optional<joggle::Val> literal(joggle::Compiler& compiler, Domain expected,
                                   const joggle::Mod::Expr& expression) {
  const auto type = domain_type(compiler, expected);
  if (!type) {
    return std::nullopt;
  }
  if (expected.list) {
    switch (expected.element) {
    case Scalar::Integer: {
      auto values = literal_list<std::int64_t>(expression, integer_literal);
      return values ? compiler.known(*type, std::move(*values)) : std::nullopt;
    }
    case Scalar::Real: {
      auto values = literal_list<double>(expression, real_literal);
      return values ? compiler.known(*type, std::move(*values)) : std::nullopt;
    }
    case Scalar::Boolean: {
      auto values = literal_list<bool>(
          expression, [](const joggle::Mod::Expr& value) -> std::optional<bool> {
            return value.kind == joggle::Mod::Expr::Kind::Boolean
                       ? std::optional<bool>{value.text == "true"}
                       : std::nullopt;
          });
      return values ? compiler.known(*type, std::move(*values)) : std::nullopt;
    }
    case Scalar::String: {
      auto values = literal_list<std::string>(
          expression,
          [](const joggle::Mod::Expr& value) -> std::optional<std::string> {
            return value.kind == joggle::Mod::Expr::Kind::String
                       ? std::optional<std::string>{value.text}
                       : std::nullopt;
          });
      return values ? compiler.known(*type, std::move(*values)) : std::nullopt;
    }
    case Scalar::Type:
    case Scalar::Fn:
    case Scalar::Bytes:
      return std::nullopt;
    }
  }

  switch (expected.element) {
  case Scalar::Integer: {
    const auto value = integer_literal(expression);
    return value ? compiler.known(*type, *value) : std::nullopt;
  }
  case Scalar::Real: {
    const auto value = real_literal(expression);
    return value ? compiler.known(*type, *value) : std::nullopt;
  }
  case Scalar::Boolean:
    return expression.kind == joggle::Mod::Expr::Kind::Boolean
               ? compiler.known(*type, expression.text == "true")
               : std::nullopt;
  case Scalar::String:
    return expression.kind == joggle::Mod::Expr::Kind::String
               ? compiler.known(*type, expression.text)
               : std::nullopt;
  case Scalar::Type:
  case Scalar::Fn:
  case Scalar::Bytes:
    return std::nullopt;
  }
  return std::nullopt;
}

std::optional<joggle::Val> attribute(joggle::Compiler& compiler,
                                     Domain expected,
                                     const onnx::AttributeProto& source) {
  const auto type = domain_type(compiler, expected);
  if (!type) {
    return std::nullopt;
  }
  if (expected.list) {
    switch (expected.element) {
    case Scalar::Integer:
      if (source.type() == onnx::AttributeProto_AttributeType_INTS) {
        return compiler.known(
            *type, Shape(source.ints().begin(), source.ints().end()));
      }
      return std::nullopt;
    case Scalar::Real:
      if (source.type() == onnx::AttributeProto_AttributeType_FLOATS) {
        return compiler.known(
            *type, std::vector<double>(source.floats().begin(),
                                       source.floats().end()));
      }
      return std::nullopt;
    case Scalar::Boolean:
      if (source.type() == onnx::AttributeProto_AttributeType_INTS) {
        std::vector<bool> values;
        values.reserve(static_cast<std::size_t>(source.ints_size()));
        for (const auto value : source.ints()) {
          if (value != 0 && value != 1) {
            return std::nullopt;
          }
          values.push_back(value != 0);
        }
        return compiler.known(*type, std::move(values));
      }
      return std::nullopt;
    case Scalar::String:
      if (source.type() == onnx::AttributeProto_AttributeType_STRINGS) {
        return compiler.known(
            *type, std::vector<std::string>(source.strings().begin(),
                                            source.strings().end()));
      }
      return std::nullopt;
    case Scalar::Type:
    case Scalar::Fn:
    case Scalar::Bytes:
      return std::nullopt;
    }
  }

  switch (expected.element) {
  case Scalar::Integer:
    return source.type() == onnx::AttributeProto_AttributeType_INT &&
                   source.has_i()
               ? compiler.known(*type, source.i())
               : std::nullopt;
  case Scalar::Real:
    return source.type() == onnx::AttributeProto_AttributeType_FLOAT &&
                   source.has_f() && std::isfinite(source.f())
               ? compiler.known(*type, static_cast<double>(source.f()))
               : std::nullopt;
  case Scalar::Boolean:
    return source.type() == onnx::AttributeProto_AttributeType_INT &&
                   source.has_i() && (source.i() == 0 || source.i() == 1)
               ? compiler.known(*type, source.i() != 0)
               : std::nullopt;
  case Scalar::String:
    return source.type() == onnx::AttributeProto_AttributeType_STRING &&
                   source.has_s()
               ? compiler.known(*type, source.s())
               : std::nullopt;
  case Scalar::Type:
  case Scalar::Fn:
  case Scalar::Bytes:
    return std::nullopt;
  }
  return std::nullopt;
}

std::optional<joggle::Val> lift(joggle::Compiler& compiler, Domain expected,
                                const Initializer& initializer) {
  if (initializer.element != onnx::TensorProto_DataType_INT64) {
    return std::nullopt;
  }
  const auto type = domain_type(compiler, expected);
  if (!type || expected.element != Scalar::Integer) {
    return std::nullopt;
  }
  if (expected.list) {
    return compiler.known(*type, initializer.integers);
  }
  return initializer.integers.size() == 1U
             ? compiler.known(*type, initializer.integers.front())
             : std::nullopt;
}

std::optional<Types> load_types(joggle::Compiler& compiler,
                                joggle::Diag& diagnostics) {
  const auto tensor_mod = compiler.mod("tensor");
  const auto onnx_mod = compiler.mod("onnx");
  const auto tensor = tensor_mod ? tensor_mod->type("tensor") : std::nullopt;
  const auto constants =
      onnx_mod ? onnx_mod->overloads("Constant")
               : std::vector<joggle::Mod::FnDecl>{};
  const auto constant = std::find_if(
      constants.begin(), constants.end(),
      [](const auto& fn) { return fn.inputs().size() == 1U; });
  if (!tensor || constant == constants.end()) {
    fail(diagnostics, "linked ONNX and tensor declarations are unavailable");
    return std::nullopt;
  }

  std::map<std::int32_t, joggle::Type> elements;
  for (const auto& [code, name] :
       std::vector<std::pair<std::int32_t, std::string_view>>{
           {onnx::TensorProto_DataType_FLOAT, "f32"},
           {onnx::TensorProto_DataType_UINT8, "u8"},
           {onnx::TensorProto_DataType_INT8, "i8"},
           {onnx::TensorProto_DataType_INT32, "i32"},
           {onnx::TensorProto_DataType_INT64, "i64"}}) {
    const auto type = compiler.make(name);
    if (!type) {
      fail(diagnostics, "Prelude tensor element Types are unavailable");
      return std::nullopt;
    }
    elements.emplace(code, *type);
  }
  return Types{*tensor, *constant, std::move(elements)};
}

std::optional<joggle::Type> tensor_type(joggle::Compiler& compiler,
                                        const Types& types,
                                        std::int32_t element,
                                        const Shape& shape) {
  const auto found = types.elements.find(element);
  return found == types.elements.end()
             ? std::nullopt
             : compiler.make(types.tensor, found->second, shape);
}

std::optional<std::pair<std::int32_t, Shape>>
tensor_info(const Types& types, const joggle::Type& type) {
  if (type.schema() != types.tensor) {
    return std::nullopt;
  }
  const auto element = type.get<joggle::Type>("element");
  const auto shape = type.get<Shape>("shape");
  if (!element || !shape) {
    return std::nullopt;
  }
  const auto found = std::find_if(
      types.elements.begin(), types.elements.end(),
      [&](const auto& item) { return item.second == *element; });
  return found == types.elements.end()
             ? std::nullopt
             : std::optional<std::pair<std::int32_t, Shape>>{
                   {found->first, *shape}};
}

using Attributes = std::map<std::string, const onnx::AttributeProto*>;

std::optional<Attributes> attributes(const onnx::NodeProto& node) {
  Attributes result;
  for (const auto& attribute : node.attribute()) {
    if (attribute.name().empty() ||
        !result.emplace(attribute.name(), &attribute).second) {
      return std::nullopt;
    }
  }
  return result;
}

std::optional<std::vector<joggle::Val>> bind_node(
    joggle::Compiler& compiler, const joggle::Mod::FnDecl& fn,
    const onnx::NodeProto& node, const Attributes& attrs,
    const std::map<std::string, Tensor>& values,
    const std::map<std::string, Initializer>& initializers) {
  std::vector<joggle::Val> result;
  std::set<std::string> consumed_attributes;
  std::size_t input = 0;

  for (const auto& parameter : fn.inputs()) {
    const auto compile_domain = domain(parameter.domain);
    if (!compile_domain) {
      if (parameter.variadic) {
        while (input < static_cast<std::size_t>(node.input_size())) {
          if (node.input(static_cast<int>(input)).empty()) {
            return std::nullopt;
          }
          const auto found = values.find(node.input(static_cast<int>(input++)));
          if (found == values.end()) {
            return std::nullopt;
          }
          result.push_back(found->second.value);
        }
        continue;
      }
      if (input >= static_cast<std::size_t>(node.input_size()) ||
          node.input(static_cast<int>(input)).empty()) {
        return std::nullopt;
      }
      const auto found = values.find(node.input(static_cast<int>(input++)));
      if (found == values.end()) {
        return std::nullopt;
      }
      result.push_back(found->second.value);
      continue;
    }

    const auto found_attribute = attrs.find(parameter.name);
    if (found_attribute != attrs.end()) {
      auto value = attribute(compiler, *compile_domain,
                             *found_attribute->second);
      if (!value) {
        return std::nullopt;
      }
      result.push_back(std::move(*value));
      consumed_attributes.insert(found_attribute->first);
      continue;
    }

    if (input < static_cast<std::size_t>(node.input_size())) {
      const std::string& name = node.input(static_cast<int>(input));
      const auto initializer = initializers.find(name);
      if (!name.empty() && initializer != initializers.end()) {
        auto value = lift(compiler, *compile_domain, initializer->second);
        if (value) {
          result.push_back(std::move(*value));
          ++input;
          continue;
        }
      }
      if (name.empty()) {
        ++input;
      }
    }

    if (!parameter.default_value) {
      return std::nullopt;
    }
    auto value = literal(compiler, *compile_domain, *parameter.default_value);
    if (!value) {
      return std::nullopt;
    }
    result.push_back(std::move(*value));
  }

  while (input < static_cast<std::size_t>(node.input_size()) &&
         node.input(static_cast<int>(input)).empty()) {
    ++input;
  }
  return input == static_cast<std::size_t>(node.input_size()) &&
                 consumed_attributes.size() == attrs.size()
             ? std::optional<std::vector<joggle::Val>>{std::move(result)}
             : std::nullopt;
}

joggle::Loc location(const onnx::GraphProto& graph, std::string_view kind,
                     std::string_view name, std::size_t ordinal) {
  const auto line = ordinal + 1;
  return {"onnx:" + graph.name() + "/" + std::string(kind) + "/" +
              std::string(name),
          {line, 1},
          {line, 2}};
}

enum class Profile { Opset7, Opset13 };

std::optional<Profile> profile(const onnx::ModelProto& source,
                               joggle::Diag& diagnostics) {
  std::optional<std::int64_t> standard;
  for (const auto& imported : source.opset_import()) {
    if (imported.domain().empty() || imported.domain() == "ai.onnx") {
      if (standard) {
        fail(diagnostics, "the ai.onnx opset is imported more than once");
        return std::nullopt;
      }
      standard = imported.version();
    }
  }
  if (source.ir_version() == 3 && standard == 7) {
    return Profile::Opset7;
  }
  if (source.ir_version() == 7 && standard == 13) {
    return Profile::Opset13;
  }
  fail(diagnostics, "supported profiles are IR 3/opset 7 and IR 7/opset 13");
  return std::nullopt;
}

}  // namespace

std::optional<joggle::Mod> read(joggle::Compiler& compiler,
                                const joggle::Bytes& input, std::string name,
                                joggle::Diag& diagnostics) {
  if (!valid_identifier(name)) {
    fail(diagnostics, "mod name '" + name + "' is not an identifier");
    return std::nullopt;
  }
  if (input.size() >
      static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    fail(diagnostics, "input exceeds Protobuf parser size");
    return std::nullopt;
  }

  onnx::ModelProto source;
  if (!source.ParseFromArray(input.data(), static_cast<int>(input.size()))) {
    fail(diagnostics, "input is not a valid ModelProto");
    return std::nullopt;
  }
  const auto selected = profile(source, diagnostics);
  if (!selected) {
    return std::nullopt;
  }
  if (!source.has_graph() || source.functions_size() != 0 ||
      source.training_info_size() != 0) {
    fail(diagnostics,
         "a single inference graph without fns or training data is required");
    return std::nullopt;
  }

  const auto& graph = source.graph();
  if (graph.output_size() == 0 || graph.sparse_initializer_size() != 0 ||
      graph.quantization_annotation_size() != 0) {
    fail(diagnostics,
         "at least one graph output and no sparse initializers or "
         "quantization annotations are required");
    return std::nullopt;
  }

  const auto types = load_types(compiler, diagnostics);
  const auto onnx_mod = compiler.mod("onnx");
  if (!types || !onnx_mod) {
    return std::nullopt;
  }

  using Declared = std::pair<std::int32_t, Shape>;
  std::map<std::string, Declared> declared_values;
  std::set<std::string> graph_inputs;
  const auto declare = [&](const onnx::ValueInfoProto& value) {
    if (value.name().empty() || declared_values.contains(value.name())) {
      fail(diagnostics, "value metadata names must be non-empty and unique");
      return false;
    }
    const auto declared = value_type(value, diagnostics);
    return declared && declared_values.emplace(value.name(), *declared).second;
  };
  for (const auto& value : graph.input()) {
    if (!graph_inputs.insert(value.name()).second || !declare(value)) {
      return std::nullopt;
    }
  }
  for (const auto& value : graph.value_info()) {
    if (!declare(value)) {
      return std::nullopt;
    }
  }
  for (const auto& value : graph.output()) {
    if (!declare(value)) {
      return std::nullopt;
    }
  }

  std::map<std::string, Initializer> initializers;
  for (const auto& tensor : graph.initializer()) {
    if (tensor.name().empty() || initializers.contains(tensor.name())) {
      fail(diagnostics, "initializer names must be non-empty and unique");
      return std::nullopt;
    }
    auto decoded = decode_initializer(tensor, diagnostics);
    if (!decoded) {
      return std::nullopt;
    }
    initializers.emplace(tensor.name(), std::move(*decoded));
  }

  for (const auto& [initializer_name, initializer] : initializers) {
    const auto found = declared_values.find(initializer_name);
    const bool legacy_match = graph_inputs.contains(initializer_name) &&
                              found != declared_values.end() &&
                              found->second.first == initializer.element &&
                              found->second.second == initializer.shape;
    if ((*selected == Profile::Opset7 && !legacy_match) ||
        (*selected == Profile::Opset13 &&
         graph_inputs.contains(initializer_name))) {
      fail(diagnostics, "initializer '" + initializer_name +
                            "' violates its ONNX IR profile's input rule");
      return std::nullopt;
    }
  }

  joggle::Mod model(std::move(name), {1, 0, 0});
  static_cast<void>(model.store(joggle::Bytes(input)));
  for (const auto& [initializer_name, initializer] : initializers) {
    static_cast<void>(initializer_name);
    static_cast<void>(model.store(initializer.bytes));
  }

  auto fn = compiler.create_fn();
  if (!fn) {
    fail(diagnostics, "could not create a Fn in the linked compiler");
    return std::nullopt;
  }
  auto edit = fn->edit();
  std::map<std::string, Tensor> values;

  for (const auto& input_value : graph.input()) {
    if (initializers.contains(input_value.name())) {
      continue;
    }
    const auto& declared = declared_values.at(input_value.name());
    const auto type =
        tensor_type(compiler, *types, declared.first, declared.second);
    if (!type) {
      fail(diagnostics, "unsupported graph input tensor Type");
      return std::nullopt;
    }
    values.emplace(input_value.name(), Tensor{edit.argument(*type),
                                              declared.first, declared.second});
  }

  std::size_t initializer_ordinal = 0;
  for (const auto& tensor : graph.initializer()) {
    const auto& initializer = initializers.at(tensor.name());
    if (initializer.element == onnx::TensorProto_DataType_INT64) {
      ++initializer_ordinal;
      continue;
    }
    const auto type =
        tensor_type(compiler, *types, initializer.element, initializer.shape);
    const auto digest = model.store(initializer.bytes);
    const auto string = compiler.make("string");
    const auto content =
        string ? compiler.known(*string, digest) : std::nullopt;
    if (!type || !content) {
      fail(diagnostics,
           "could not materialize initializer '" + tensor.name() + "'");
      return std::nullopt;
    }
    const auto constant = edit.call(types->constant, {*content}, {*type});
    edit.locate(constant, location(graph, "initializer", tensor.name(),
                                   initializer_ordinal));
    values.emplace(tensor.name(), Tensor{constant.value(), initializer.element,
                                         initializer.shape});
    ++initializer_ordinal;
  }

  const auto publish = [&](std::string_view value_name, Tensor tensor) {
    const auto declared = declared_values.find(std::string(value_name));
    if (declared != declared_values.end() &&
        (declared->second.first != tensor.element ||
         declared->second.second != tensor.shape)) {
      return fail(diagnostics, "inferred value '" + std::string(value_name) +
                                   "' disagrees with ONNX metadata");
    }
    return values.emplace(std::string(value_name), std::move(tensor)).second;
  };

  for (std::size_t node_index = 0;
       node_index < static_cast<std::size_t>(graph.node_size()); ++node_index) {
    const auto& node = graph.node(static_cast<int>(node_index));
    if (!node.domain().empty() && node.domain() != "ai.onnx") {
      fail(diagnostics,
           "node '" + node.name() + "' uses an unsupported domain");
      return std::nullopt;
    }
    if (node.output_size() == 0) {
      fail(diagnostics, "a node must define at least one output");
      return std::nullopt;
    }
    for (const auto& output : node.output()) {
      if (!output.empty() && values.contains(output)) {
        fail(diagnostics, "node output '" + output + "' is not unique");
        return std::nullopt;
      }
    }

    const auto attrs = attributes(node);
    if (!attrs) {
      fail(diagnostics, "node '" + node.name() +
                            "' has an empty or duplicate attribute name");
      return std::nullopt;
    }

    struct Candidate {
      joggle::Mod::FnDecl fn;
      std::vector<joggle::Val> arguments;
    };
    std::vector<Candidate> candidates;
    for (const auto& declaration : onnx_mod->overloads(node.op_type())) {
      auto arguments =
          bind_node(compiler, declaration, node, *attrs, values, initializers);
      if (arguments) {
        candidates.push_back({declaration, std::move(*arguments)});
      }
    }
    if (candidates.empty()) {
      fail(diagnostics, "no declaration of '" + node.op_type() +
                            "' accepts this node's inputs and attributes");
      return std::nullopt;
    }
    if (candidates.size() != 1U) {
      fail(diagnostics, "node '" + node.name() + "' ambiguously matches " +
                            std::to_string(candidates.size()) +
                            " declarations of '" + node.op_type() + "'");
      return std::nullopt;
    }

    const auto where =
        location(graph, "node",
                 node.name().empty() ? node.output(0) : node.name(), node_index);
    auto call = compiler.call(edit, candidates.front().fn,
                              std::move(candidates.front().arguments), where);
    if (!call) {
      fail(diagnostics, "could not infer the result Type of node '" +
                            node.name() + "' (" + node.op_type() + ")");
      return std::nullopt;
    }
    const auto results = call->results();
    if (results.size() != static_cast<std::size_t>(node.output_size())) {
      fail(diagnostics, "node '" + node.name() + "' has " +
                            std::to_string(node.output_size()) +
                            " outputs but its fn returns " +
                            std::to_string(results.size()));
      return std::nullopt;
    }
    for (std::size_t index = 0; index < results.size(); ++index) {
      const std::string& output = node.output(static_cast<int>(index));
      if (output.empty()) {
        continue;
      }
      const auto info = tensor_info(*types, results[index].type());
      if (!info ||
          !publish(output, Tensor{results[index], info->first, info->second})) {
        if (diagnostics.ok()) {
          fail(diagnostics, "node '" + node.name() +
                                "' returned a non-tensor result");
        }
        return std::nullopt;
      }
    }
  }

  std::vector<joggle::Val> returned;
  returned.reserve(static_cast<std::size_t>(graph.output_size()));
  for (const auto& output : graph.output()) {
    const auto value = values.find(output.name());
    const auto declared = value_type(output, diagnostics);
    if (value == values.end() || !declared ||
        value->second.element != declared->first ||
        value->second.shape != declared->second) {
      if (diagnostics.ok()) {
        fail(diagnostics,
             "inferred graph output '" + output.name() +
                 "' does not match its declaration");
      }
      return std::nullopt;
    }
    returned.push_back(value->second.value);
  }

  edit.ret(fn->entry(), std::move(returned));
  if (!edit.commit(compiler, diagnostics) ||
      !model.insert("main", std::move(*fn), diagnostics)) {
    return std::nullopt;
  }
  return model;
}

}  // namespace joggle_onnx
