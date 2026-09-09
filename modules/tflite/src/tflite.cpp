#include "joggle/joggle.h"

#include "flatbuffers/minireflect.h"
#include "flatbuffers/verifier.h"
#include "schema_generated.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::string quote(std::string_view text) {
  std::string out = "\"";
  for (const unsigned char ch : text) {
    switch (ch) {
    case '\\':
    case '"':
      out.push_back('\\');
      out.push_back(static_cast<char>(ch));
      break;
    case '\n':
      out += "\\n";
      break;
    default:
      if (ch < 0x20)
        out.push_back(' ');
      else
        out.push_back(static_cast<char>(ch));
    }
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
      "else", "false", "fn", "for", "hex", "if", "in",
      "let",  "module", "nil", "return", "true", "use", "var"};
  if (keywords.contains(out))
    out.insert(out.begin(), '_');
  return out;
}

std::string hex(const flatbuffers::Vector<std::uint8_t>* data) {
  static constexpr char digits[] = "0123456789abcdef";
  std::string out = "hex\"";
  if (data) {
    out.reserve(5 + data->size() * 2);
    for (const std::uint8_t byte : *data) {
      out.push_back(digits[byte >> 4]);
      out.push_back(digits[byte & 0xf]);
    }
  }
  return out + '"';
}

template <class T>
std::string ints(const flatbuffers::Vector<T>* values, bool shape = false) {
  std::string out = "[";
  if (values) {
    for (flatbuffers::uoffset_t index = 0; index < values->size(); ++index) {
      if (index)
        out += ", ";
      const auto value = static_cast<std::int64_t>(values->Get(index));
      out += shape && value < 0 ? "_" : std::to_string(value);
    }
  }
  return out + "]";
}

std::string element(tflite::TensorType type) {
  switch (type) {
  case tflite::TensorType_FLOAT32:
    return "f32";
  case tflite::TensorType_FLOAT16:
    return "f16";
  case tflite::TensorType_INT32:
    return "i32";
  case tflite::TensorType_UINT8:
    return "u8";
  case tflite::TensorType_INT64:
    return "i64";
  case tflite::TensorType_STRING:
    return "str";
  case tflite::TensorType_BOOL:
    return "bool";
  case tflite::TensorType_INT16:
    return "i16";
  case tflite::TensorType_COMPLEX64:
    return "c64";
  case tflite::TensorType_INT8:
    return "i8";
  case tflite::TensorType_FLOAT64:
    return "f64";
  case tflite::TensorType_COMPLEX128:
    return "c128";
  case tflite::TensorType_UINT64:
    return "u64";
  case tflite::TensorType_UINT32:
    return "u32";
  default:
    return "tflite_type_" + std::to_string(static_cast<int>(type));
  }
}

std::string type(const tflite::Tensor& tensor) {
  const auto* dims = tensor.shape_signature();
  if (!dims || dims->empty())
    dims = tensor.shape();
  return "tensor<" + element(tensor.type()) + ", " + ints(dims, true) + ">";
}

std::string describe(const void* object, const flatbuffers::TypeTable* type) {
  if (!object || !type)
    return "{}";
  flatbuffers::ToStringVisitor visitor("", true, "");
  flatbuffers::IterateObject(static_cast<const std::uint8_t*>(object), type,
                             &visitor);
  return visitor.s;
}

std::string options(const tflite::Operator& op) {
  const auto* union_type = tflite::BuiltinOptionsTypeTable();
  const auto index = flatbuffers::LookupEnum(
      static_cast<std::int64_t>(op.builtin_options_type()), union_type->values,
      union_type->num_elems);
  if (index < 0 || static_cast<std::size_t>(index) >= union_type->num_elems)
    return "{}";
  const auto code = union_type->type_codes[index];
  if (code.sequence_ref < 0 || !op.builtin_options())
    return "{}";
  return describe(op.builtin_options(),
                  union_type->type_refs[code.sequence_ref]());
}

std::string model_info(const tflite::Model& model) {
  std::string out = "{\"description\": " +
                    quote(model.description() ? model.description()->string_view()
                                              : std::string_view{}) +
                    ", \"metadata\": [";
  if (model.metadata()) {
    for (flatbuffers::uoffset_t index = 0; index < model.metadata()->size();
         ++index) {
      const auto* item = model.metadata()->Get(index);
      if (item->buffer() >= model.buffers()->size())
        throw std::runtime_error("metadata has an invalid buffer index");
      if (index)
        out += ", ";
      out += "{\"data\": " + hex(model.buffers()->Get(item->buffer())->data()) +
             ", \"name\": " +
             quote(item->name() ? item->name()->string_view()
                                : std::string_view{}) +
             "}";
    }
  }
  out += "], \"metadata_buffer\": [";
  if (model.metadata_buffer()) {
    for (flatbuffers::uoffset_t index = 0;
         index < model.metadata_buffer()->size(); ++index) {
      const std::int32_t buffer = model.metadata_buffer()->Get(index);
      if (buffer < 0 ||
          static_cast<flatbuffers::uoffset_t>(buffer) >= model.buffers()->size())
        throw std::runtime_error("legacy metadata has an invalid buffer index");
      if (index)
        out += ", ";
      out += hex(model.buffers()->Get(buffer)->data());
    }
  }
  out += "], \"signatures\": [";
  if (model.signature_defs()) {
    for (flatbuffers::uoffset_t index = 0;
         index < model.signature_defs()->size(); ++index) {
      if (index)
        out += ", ";
      out += describe(model.signature_defs()->Get(index),
                      tflite::SignatureDefTypeTable());
    }
  }
  out += "], \"subgraphs\": " + std::to_string(model.subgraphs()->size()) +
         ", \"version\": " + std::to_string(model.version()) + "}";
  return out;
}

std::string opcode(const tflite::Model& model, const tflite::Operator& op,
                   int& version) {
  if (!model.operator_codes() ||
      op.opcode_index() >= model.operator_codes()->size())
    throw std::runtime_error("operator has an invalid opcode index");
  const auto* code = model.operator_codes()->Get(op.opcode_index());
  version = code->version();
  const int number =
      std::max(static_cast<int>(code->builtin_code()),
               static_cast<int>(code->deprecated_builtin_code()));
  if (number == tflite::BuiltinOperator_CUSTOM) {
    if (!code->custom_code() || code->custom_code()->empty())
      throw std::runtime_error("custom operator has no name");
    return code->custom_code()->str();
  }
  const char* name = tflite::EnumNameBuiltinOperator(
      static_cast<tflite::BuiltinOperator>(number));
  if (!name || !*name)
    return "op_" + std::to_string(number);
  return name;
}

std::vector<std::string> names(const tflite::SubGraph& graph) {
  std::vector<std::string> out;
  std::set<std::string, std::less<>> used;
  if (!graph.tensors())
    return out;
  out.reserve(graph.tensors()->size());
  for (flatbuffers::uoffset_t index = 0; index < graph.tensors()->size();
       ++index) {
    const auto* tensor = graph.tensors()->Get(index);
    std::string base = tensor->name() ? atom(tensor->name()->string_view())
                                      : "tensor_" + std::to_string(index);
    if (base.empty())
      base = "tensor_" + std::to_string(index);
    std::string name = base;
    for (std::size_t suffix = 1; used.contains(name); ++suffix)
      name = base + "_" + std::to_string(suffix);
    used.insert(name);
    out.push_back(std::move(name));
  }
  return out;
}

void require_tensor(const tflite::SubGraph& graph, std::int32_t index) {
  if (index < 0 || !graph.tensors() ||
      static_cast<flatbuffers::uoffset_t>(index) >= graph.tensors()->size())
    throw std::runtime_error("operator refers to an invalid tensor index");
}

std::string emit(const tflite::Model& model) {
  if (!model.subgraphs() || model.subgraphs()->empty())
    throw std::runtime_error("TFLite model has no subgraph");
  if (!model.buffers())
    throw std::runtime_error("TFLite model has no buffer table");

  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << "module model\nuse tflite\n\n";

  for (flatbuffers::uoffset_t graph_index = 0;
       graph_index < model.subgraphs()->size(); ++graph_index) {
    const auto* graph = model.subgraphs()->Get(graph_index);
    if (!graph->tensors() || !graph->operators())
      throw std::runtime_error("TFLite subgraph is incomplete");
    const auto binding = names(*graph);
    std::set<std::int32_t> inputs;
    std::set<std::int32_t> produced;
    if (graph->inputs())
      for (const std::int32_t index : *graph->inputs()) {
        require_tensor(*graph, index);
        inputs.insert(index);
      }
    for (const auto* op : *graph->operators()) {
      if (!op->outputs())
        continue;
      for (const std::int32_t index : *op->outputs()) {
        require_tensor(*graph, index);
        if (!produced.insert(index).second)
          throw std::runtime_error("tensor is produced more than once");
      }
    }

    out << "fn " << (graph_index == 0 ? "main" : "subgraph_" +
                                                    std::to_string(graph_index))
        << '(';
    bool first = true;
    if (graph->inputs()) {
      for (const std::int32_t index : *graph->inputs()) {
        if (!first)
          out << ", ";
        first = false;
        out << binding[index] << ": " << type(*graph->tensors()->Get(index));
      }
    }
    out << ')';
    if (graph->outputs() && !graph->outputs()->empty()) {
      out << " -> ";
      if (graph->outputs()->size() > 1)
        out << '(';
      for (flatbuffers::uoffset_t index = 0; index < graph->outputs()->size();
           ++index) {
        const std::int32_t tensor_index = graph->outputs()->Get(index);
        require_tensor(*graph, tensor_index);
        if (index)
          out << ", ";
        out << type(*graph->tensors()->Get(tensor_index));
      }
      if (graph->outputs()->size() > 1)
        out << ')';
    }
    out << " {\n";

    if (graph_index == 0) {
      out << "  tflite.model(" << model_info(model) << ")\n";
    }

    for (flatbuffers::uoffset_t index = 0; index < graph->tensors()->size();
         ++index) {
      if (inputs.contains(static_cast<std::int32_t>(index)) ||
          produced.contains(static_cast<std::int32_t>(index)))
        continue;
      const auto* tensor = graph->tensors()->Get(index);
      if (tensor->buffer() >= model.buffers()->size())
        throw std::runtime_error("tensor has an invalid buffer index");
      const auto* buffer = model.buffers()->Get(tensor->buffer());
      out << "  [tflite: {\"index\": " << index
          << ", \"tensor\": "
          << describe(tensor, tflite::TensorTypeTable()) << "}]\n"
          << "  let " << binding[index] << ": " << type(*tensor)
          << " = tflite.tensor(" << hex(buffer->data()) << ")\n";
    }

    for (flatbuffers::uoffset_t op_index = 0;
         op_index < graph->operators()->size(); ++op_index) {
      const auto* op = graph->operators()->Get(op_index);
      int version = 0;
      const std::string name = opcode(model, *op, version);
      const char* option_name =
          tflite::EnumNameBuiltinOptions(op->builtin_options_type());
      out << "  [tflite: {\"index\": " << op_index << ", \"options\": "
          << options(*op) << ", \"options_type\": "
          << quote(option_name ? option_name : "NONE") << ", \"version\": "
          << version;
      if (op->custom_options() && !op->custom_options()->empty())
        out << ", \"custom\": " << hex(op->custom_options());
      out << "}]\n  ";

      if (op->outputs() && !op->outputs()->empty()) {
        out << "let ";
        for (flatbuffers::uoffset_t index = 0; index < op->outputs()->size();
             ++index) {
          const std::int32_t tensor_index = op->outputs()->Get(index);
          require_tensor(*graph, tensor_index);
          if (index)
            out << ", ";
          out << binding[tensor_index] << ": "
              << type(*graph->tensors()->Get(tensor_index));
        }
        out << " = ";
      }
      out << "tflite." << atom(name) << '(';
      if (op->inputs()) {
        for (flatbuffers::uoffset_t index = 0; index < op->inputs()->size();
             ++index) {
          if (index)
            out << ", ";
          const std::int32_t tensor_index = op->inputs()->Get(index);
          if (tensor_index < 0)
            out << "nil";
          else {
            require_tensor(*graph, tensor_index);
            out << binding[tensor_index];
          }
        }
      }
      out << ")\n";
    }

    out << "  return";
    if (graph->outputs()) {
      bool first_output = true;
      for (const std::int32_t index : *graph->outputs()) {
        require_tensor(*graph, index);
        out << (first_output ? " " : ", ");
        first_output = false;
        out << binding[index];
      }
    }
    out << "\n}\n";
    if (graph_index + 1 != model.subgraphs()->size())
      out << '\n';
  }
  return out.str();
}

bool read(jog_call* call, void*) {
  jog_value input{};
  if (call->api->arg_count(call) != 1 || !call->api->arg(call, 0, &input) ||
      input.kind != JOG_BYTES)
    return call->api->fail(call, "expected serialized TFLite bytes");
  try {
    flatbuffers::Verifier verifier(
        reinterpret_cast<const std::uint8_t*>(input.data.bytes.data),
        input.data.bytes.size);
    if (!tflite::VerifyModelBuffer(verifier))
      return call->api->fail(call, "invalid TFLite model");
    const auto* model = tflite::GetModel(input.data.bytes.data);
    const std::string source = emit(*model);
    jog_value output{};
    output.kind = JOG_STR;
    output.data.string = {source.data(), source.size()};
    return call->api->ret(call, 0, &output);
  } catch (const std::exception& error) {
    return call->api->fail(call, error.what());
  }
}

}  // namespace

JOGGLE_MODULE_EXPORT bool joggle_module(const jog_api* api,
                                        jog_module* module) {
  return joggle::compatible(api) &&
         api->bind(module, "tflite.read", read, nullptr);
}
