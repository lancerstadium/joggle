#include "joggle/joggle.h"
#include "onnx.pb.h"

#include <bit>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

std::string quote(std::string_view text) {
  std::string out = "\"";
  for (const char ch : text) {
    if (ch == '"' || ch == '\\')
      out.push_back('\\');
    if (ch == '\n')
      out += "\\n";
    else
      out.push_back(ch);
  }
  return out + '"';
}

std::string atom(std::string_view text) {
  std::string out;
  for (const unsigned char ch : text)
    out.push_back(std::isalnum(ch) || ch == '_' ? static_cast<char>(ch) : '_');
  if (out.empty())
    out = "value";
  if (std::isdigit(static_cast<unsigned char>(out.front())))
    out.insert(out.begin(), '_');
  static const std::set<std::string, std::less<>> keywords{
      "else", "false",  "fn",  "for",    "hex",  "host", "if", "in",
      "let",  "module", "nil", "return", "true", "use",  "var"};
  if (keywords.contains(out))
    out.insert(out.begin(), '_');
  return out;
}

class Names {
public:
  std::string get(std::string_view original) {
    const auto found = names_.find(original);
    if (found != names_.end())
      return found->second;
    std::string base = atom(original);
    std::string name = base;
    for (std::size_t suffix = 1; used_.contains(name); ++suffix)
      name = base + "_" + std::to_string(suffix);
    used_.insert(name);
    names_.emplace(original, name);
    return name;
  }

private:
  std::map<std::string, std::string, std::less<>> names_;
  std::set<std::string, std::less<>> used_;
};

std::string number(double value) {
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
  return out.str();
}

std::string element(int type) {
  static constexpr std::string_view names[] = {
      "_",    "f32",  "u8",    "i8",   "u16",   "i16", "i32", "i64",
      "str",  "bool", "f16",   "f64",  "u32",   "u64", "c64", "c128",
      "bf16", "f8e4", "f8e4z", "f8e5", "f8e5z", "u4",  "i4",  "f4"};
  return type >= 0 && static_cast<std::size_t>(type) < std::size(names)
             ? std::string(names[type])
             : "onnx_type_" + std::to_string(type);
}

std::string shape(const google::protobuf::RepeatedField<std::int64_t>& dims) {
  std::string out = "[";
  for (int index = 0; index < dims.size(); ++index) {
    if (index)
      out += ", ";
    out += std::to_string(dims[index]);
  }
  return out + "]";
}

std::string type(const jogonnx::ValueInfoProto& value) {
  if (!value.has_type() || !value.type().has_tensor_type())
    return "_";
  const auto& tensor = value.type().tensor_type();
  std::string dims = "[";
  if (tensor.has_shape()) {
    for (int index = 0; index < tensor.shape().dim_size(); ++index) {
      if (index)
        dims += ", ";
      const auto& dim = tensor.shape().dim(index);
      if (dim.has_dim_value())
        dims += std::to_string(dim.dim_value());
      else if (dim.has_dim_param())
        dims += atom(dim.dim_param());
      else
        dims += "_";
    }
  }
  return "tensor<" + element(tensor.elem_type()) + ", " + dims + "]>";
}

template <class UInt> void append_le(std::string& out, UInt value) {
  for (std::size_t index = 0; index < sizeof(UInt); ++index)
    out.push_back(static_cast<char>((value >> (index * 8)) & 0xff));
}

std::size_t width(int type) {
  switch (type) {
  case 2:
  case 3:
  case 9:
  case 17:
  case 18:
  case 19:
  case 20:
    return 1;
  case 4:
  case 5:
  case 10:
  case 16:
    return 2;
  default:
    return 4;
  }
}

std::size_t elements(const jogonnx::TensorProto& tensor) {
  std::size_t count = 1;
  for (const std::int64_t dim : tensor.dims()) {
    if (dim < 0)
      throw std::runtime_error("initializer has a negative dimension");
    if (dim == 0)
      return 0;
    const auto size = static_cast<std::size_t>(dim);
    if (count > std::numeric_limits<std::size_t>::max() / size)
      throw std::runtime_error("initializer shape is too large");
    count *= size;
  }
  return count;
}

std::size_t expected_bytes(const jogonnx::TensorProto& tensor) {
  const std::size_t count = elements(tensor);
  if (tensor.data_type() >= 21 && tensor.data_type() <= 23)
    return count / 2 + count % 2;
  std::size_t bytes = 0;
  switch (tensor.data_type()) {
  case 1:
  case 6:
  case 12:
    bytes = 4;
    break;
  case 2:
  case 3:
  case 9:
  case 17:
  case 18:
  case 19:
  case 20:
    bytes = 1;
    break;
  case 4:
  case 5:
  case 10:
  case 16:
    bytes = 2;
    break;
  case 7:
  case 11:
  case 13:
  case 14:
    bytes = 8;
    break;
  case 15:
    bytes = 16;
    break;
  case 8:
    throw std::runtime_error("string tensors are not supported yet");
  default:
    throw std::runtime_error("unsupported ONNX tensor element type");
  }
  if (count && bytes > std::numeric_limits<std::size_t>::max() / count)
    throw std::runtime_error("initializer payload is too large");
  return count * bytes;
}

bool has_typed_data(const jogonnx::TensorProto& tensor) {
  return tensor.float_data_size() || tensor.int32_data_size() ||
         tensor.string_data_size() || tensor.int64_data_size() ||
         tensor.double_data_size() || tensor.uint64_data_size();
}

std::string data(const jogonnx::TensorProto& tensor) {
  if (tensor.has_segment())
    throw std::runtime_error("segmented tensor data is not supported yet");
  if (tensor.external_data_size() ||
      (tensor.has_data_location() && tensor.data_location() != 0))
    throw std::runtime_error("external tensor data is not supported yet");
  const std::size_t expected = expected_bytes(tensor);
  if (tensor.has_raw_data()) {
    if (has_typed_data(tensor))
      throw std::runtime_error("initializer has both raw and typed data");
    if (tensor.raw_data().size() != expected)
      throw std::runtime_error("initializer payload size does not match shape");
    return tensor.raw_data();
  }
  std::string out;
  switch (tensor.data_type()) {
  case 1:
  case 14:
    if (tensor.int32_data_size() || tensor.int64_data_size() ||
        tensor.double_data_size() || tensor.uint64_data_size())
      throw std::runtime_error("initializer uses the wrong typed data field");
    for (const float value : tensor.float_data())
      append_le(out, std::bit_cast<std::uint32_t>(value));
    break;
  case 2:
  case 3:
  case 4:
  case 5:
  case 6:
  case 9:
  case 10:
  case 16:
  case 17:
  case 18:
  case 19:
  case 20:
    if (tensor.float_data_size() || tensor.int64_data_size() ||
        tensor.double_data_size() || tensor.uint64_data_size())
      throw std::runtime_error("initializer uses the wrong typed data field");
    for (const std::int32_t value : tensor.int32_data()) {
      const auto bits = static_cast<std::uint32_t>(value);
      for (std::size_t index = 0; index < width(tensor.data_type()); ++index)
        out.push_back(static_cast<char>((bits >> (index * 8)) & 0xff));
    }
    break;
  case 21:
  case 22:
  case 23:
    if (tensor.float_data_size() || tensor.int64_data_size() ||
        tensor.double_data_size() || tensor.uint64_data_size())
      throw std::runtime_error("initializer uses the wrong typed data field");
    for (int index = 0; index < tensor.int32_data_size(); index += 2) {
      unsigned byte = static_cast<unsigned>(tensor.int32_data(index)) & 0xf;
      if (index + 1 < tensor.int32_data_size())
        byte |= (static_cast<unsigned>(tensor.int32_data(index + 1)) & 0xf)
                << 4;
      out.push_back(static_cast<char>(byte));
    }
    break;
  case 7:
    if (tensor.float_data_size() || tensor.int32_data_size() ||
        tensor.double_data_size() || tensor.uint64_data_size())
      throw std::runtime_error("initializer uses the wrong typed data field");
    for (const std::int64_t value : tensor.int64_data())
      append_le(out, static_cast<std::uint64_t>(value));
    break;
  case 11:
  case 15:
    if (tensor.float_data_size() || tensor.int32_data_size() ||
        tensor.int64_data_size() || tensor.uint64_data_size())
      throw std::runtime_error("initializer uses the wrong typed data field");
    for (const double value : tensor.double_data())
      append_le(out, std::bit_cast<std::uint64_t>(value));
    break;
  case 12:
  case 13:
    if (tensor.float_data_size() || tensor.int32_data_size() ||
        tensor.int64_data_size() || tensor.double_data_size())
      throw std::runtime_error("initializer uses the wrong typed data field");
    for (const std::uint64_t value : tensor.uint64_data()) {
      if (tensor.data_type() == 12)
        append_le(out, static_cast<std::uint32_t>(value));
      else
        append_le(out, value);
    }
    break;
  case 8:
    throw std::runtime_error("string tensors are not supported yet");
  default:
    throw std::runtime_error("unsupported ONNX tensor element type");
  }
  if (tensor.string_data_size())
    throw std::runtime_error("string tensors are not supported yet");
  if (out.size() != expected)
    throw std::runtime_error("initializer payload size does not match shape");
  return out;
}

std::string hex(std::string_view bytes) {
  static constexpr char digits[] = "0123456789abcdef";
  std::string out = "hex\"";
  out.reserve(bytes.size() * 2 + 5);
  for (const unsigned char byte : bytes) {
    out.push_back(digits[byte >> 4]);
    out.push_back(digits[byte & 15]);
  }
  return out + '"';
}

std::string tensor(const jogonnx::TensorProto& value) {
  return "{type: " + std::to_string(value.data_type()) +
         ", shape: " + shape(value.dims()) + ", data: " + hex(data(value)) +
         "}";
}

template <class Range, class Render>
std::string list(const Range& values, Render render) {
  std::string out = "[";
  bool first = true;
  for (const auto& value : values) {
    if (!first)
      out += ", ";
    first = false;
    out += render(value);
  }
  return out + "]";
}

std::string attr(const jogonnx::AttributeProto& value) {
  switch (value.type()) {
  case jogonnx::AttributeProto::FLOAT:
    return number(value.f());
  case jogonnx::AttributeProto::INT:
    return std::to_string(value.i());
  case jogonnx::AttributeProto::STRING:
    return quote(value.s());
  case jogonnx::AttributeProto::TENSOR:
    return tensor(value.t());
  case jogonnx::AttributeProto::FLOATS:
    return list(value.floats(), [](float item) { return number(item); });
  case jogonnx::AttributeProto::INTS:
    return list(value.ints(),
                [](std::int64_t item) { return std::to_string(item); });
  case jogonnx::AttributeProto::STRINGS:
    return list(value.strings(),
                [](const std::string& item) { return quote(item); });
  default:
    throw std::runtime_error("unsupported ONNX attribute kind");
  }
}

std::string attrs(const jogonnx::NodeProto& node) {
  std::string out = "{";
  bool first = true;
  if (node.has_name() && !node.name().empty()) {
    out += quote("$node") + ": " + quote(node.name());
    first = false;
  }
  for (const auto& value : node.attribute()) {
    if (!first)
      out += ", ";
    first = false;
    out += quote(value.name()) + ": " + attr(value);
  }
  return out + "}";
}

std::string emit(const jogonnx::ModelProto& model) {
  if (!model.has_graph())
    throw std::runtime_error("ONNX model has no graph");
  const auto& graph = model.graph();
  if (graph.sparse_initializer_size())
    throw std::runtime_error("sparse initializers are not supported yet");
  Names names;
  std::set<std::string, std::less<>> initialized;
  for (const auto& value : graph.initializer())
    initialized.insert(value.name());

  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << "module model\nuse onnx\n\nfn main(";
  bool first = true;
  for (const auto& input : graph.input()) {
    if (initialized.contains(input.name()))
      continue;
    if (!first)
      out << ", ";
    first = false;
    out << names.get(input.name()) << ": " << type(input);
  }
  out << ") -> ";
  if (graph.output_size() != 1)
    throw std::runtime_error("only one graph output is supported yet");
  out << type(graph.output(0)) << " {\n";

  out << "  onnx.model({ir: " << model.ir_version()
      << ", producer: " << quote(model.producer_name()) << ", opsets: [";
  for (int index = 0; index < model.opset_import_size(); ++index) {
    if (index)
      out << ", ";
    const auto& opset = model.opset_import(index);
    out << "{domain: " << quote(opset.domain())
        << ", version: " << opset.version() << "}";
  }
  out << "]})\n";

  for (const auto& value : graph.initializer())
    out << "  let " << names.get(value.name()) << " = onnx.tensor("
        << value.data_type() << ", " << shape(value.dims()) << ", "
        << hex(data(value)) << ")\n";

  for (const auto& node : graph.node()) {
    if (node.output_size() != 1 || node.output(0).empty())
      throw std::runtime_error("only named single-output nodes are supported");
    const std::string domain =
        node.domain().empty() ? "onnx" : atom(node.domain());
    out << "  let " << names.get(node.output(0)) << " = " << domain << '.'
        << atom(node.op_type()) << '(';
    for (int index = 0; index < node.input_size(); ++index) {
      if (index)
        out << ", ";
      out << (node.input(index).empty() ? "nil" : names.get(node.input(index)));
    }
    if (node.attribute_size() || (node.has_name() && !node.name().empty())) {
      if (node.input_size())
        out << ", ";
      out << attrs(node);
    }
    out << ")\n";
  }
  out << "  return " << names.get(graph.output(0).name()) << "\n}\n";
  return out.str();
}

bool read(jog_call_v1* call, void*) {
  jog_value_v1 input{};
  if (call->api->arg_count(call) != 1 || !call->api->arg(call, 0, &input) ||
      input.kind != JOG_BYTES_V1)
    return call->api->fail(call, "expected serialized ONNX bytes");
  try {
    jogonnx::ModelProto model;
    if (input.data.bytes.size >
            static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        !model.ParseFromArray(input.data.bytes.data,
                              static_cast<int>(input.data.bytes.size)))
      return call->api->fail(call, "invalid ONNX ModelProto");
    const std::string source = emit(model);
    jog_value_v1 output{};
    output.kind = JOG_STR_V1;
    output.data.string = {source.data(), source.size()};
    return call->api->ret(call, 0, &output);
  } catch (const std::exception& error) {
    return call->api->fail(call, error.what());
  }
}

}  // namespace

JOGGLE_MODULE_EXPORT bool joggle_module_v1(const jog_api_v1* api,
                                           jog_module_v1* module) {
  return api && api->abi_version == joggle::module_abi_version &&
         api->bind(module, "onnx.read", read, nullptr);
}
