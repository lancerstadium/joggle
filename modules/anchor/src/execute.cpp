#include "execute.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace joggle::anchor {
namespace {

std::optional<std::uint64_t> elements(const joggle::Type& type,
                                      joggle::Diagnostics& diagnostics) {
  const auto shape = type.get<std::vector<std::int64_t>>("shape");
  if (!shape) {
    diagnostics.report("anchor operation has no shaped reference result");
    return std::nullopt;
  }
  std::uint64_t count = 1;
  for (const std::int64_t dimension : *shape) {
    if (dimension <= 0 ||
        static_cast<std::uint64_t>(dimension) >
            std::numeric_limits<std::uint64_t>::max() / count) {
      diagnostics.report("anchor element count overflows");
      return std::nullopt;
    }
    count *= static_cast<std::uint64_t>(dimension);
  }
  return count;
}

struct Tensor {
  joggle::Type type;
  std::shared_ptr<std::vector<float>> values;
};

struct TensorBinding {
  joggle::Value value;
  Tensor tensor;
};

bool f32_reference(joggle::Compiler& compiler, const joggle::Type& type,
                   const ExecutionSchema& schema) {
  if (!compiler.conforms(type.schema(), schema.memory_reference)) {
    return false;
  }
  const auto element = type.get<joggle::Type>("element_type");
  return element &&
         element->schema().symbol().qualified_name() == "prelude.f32";
}

std::optional<std::size_t> element_count(const joggle::Type& type,
                                         joggle::Diagnostics& diagnostics) {
  const auto count = elements(type, diagnostics);
  if (!count || *count > static_cast<std::uint64_t>(
                             std::numeric_limits<std::size_t>::max())) {
    if (count) {
      diagnostics.report("anchor tensor is too large for the host executor");
    }
    return std::nullopt;
  }
  return static_cast<std::size_t>(*count);
}

std::optional<std::vector<std::size_t>>
dimensions(const joggle::Type& type, std::size_t rank,
           joggle::Diagnostics& diagnostics) {
  const auto shape = type.get<std::vector<std::int64_t>>("shape");
  if (!shape || shape->size() != rank) {
    diagnostics.report("anchor executor encountered an unexpected tensor rank");
    return std::nullopt;
  }
  std::vector<std::size_t> result;
  result.reserve(shape->size());
  for (const std::int64_t dimension : *shape) {
    if (dimension <= 0 || static_cast<std::uint64_t>(dimension) >
                              static_cast<std::uint64_t>(
                                  std::numeric_limits<std::size_t>::max())) {
      diagnostics.report("anchor executor encountered an invalid tensor shape");
      return std::nullopt;
    }
    result.push_back(static_cast<std::size_t>(dimension));
  }
  return result;
}

std::optional<std::vector<float>> decode_f32(std::span<const std::byte> bytes,
                                             std::size_t count,
                                             joggle::Diagnostics& diagnostics,
                                             std::string_view description) {
  static_assert(sizeof(float) == sizeof(std::uint32_t));
  if (count > std::numeric_limits<std::size_t>::max() / sizeof(float) ||
      bytes.size() != count * sizeof(float)) {
    diagnostics.report(std::string(description) +
                       " does not match its f32 tensor shape");
    return std::nullopt;
  }
  std::vector<float> result;
  result.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    const std::size_t offset = index * sizeof(float);
    const std::uint32_t word =
        std::to_integer<std::uint32_t>(bytes[offset]) |
        (std::to_integer<std::uint32_t>(bytes[offset + 1U]) << 8U) |
        (std::to_integer<std::uint32_t>(bytes[offset + 2U]) << 16U) |
        (std::to_integer<std::uint32_t>(bytes[offset + 3U]) << 24U);
    result.push_back(std::bit_cast<float>(word));
  }
  return result;
}

joggle::Bytes encode_f32(const std::vector<float>& values) {
  joggle::Bytes result;
  result.reserve(values.size() * sizeof(float));
  for (const float value : values) {
    const std::uint32_t word = std::bit_cast<std::uint32_t>(value);
    result.push_back(static_cast<std::byte>(word & 0xffU));
    result.push_back(static_cast<std::byte>((word >> 8U) & 0xffU));
    result.push_back(static_cast<std::byte>((word >> 16U) & 0xffU));
    result.push_back(static_cast<std::byte>((word >> 24U) & 0xffU));
  }
  return result;
}

std::optional<Tensor> lookup_tensor(const std::vector<TensorBinding>& bindings,
                                    const joggle::Value& value,
                                    joggle::Diagnostics& diagnostics) {
  for (auto iterator = bindings.rbegin(); iterator != bindings.rend();
       ++iterator) {
    if (iterator->value == value) {
      return iterator->tensor;
    }
  }
  diagnostics.report("anchor executor encountered an unbound SSA value");
  return std::nullopt;
}

std::optional<Tensor> operand_tensor(const joggle::Op& op,
                                     std::string_view name,
                                     const std::vector<TensorBinding>& bindings,
                                     joggle::Diagnostics& diagnostics) {
  const auto value = op.operand(name);
  if (!value) {
    diagnostics.report("anchor executor is missing operand '" +
                       std::string(name) + "'");
    return std::nullopt;
  }
  return lookup_tensor(bindings, *value, diagnostics);
}

std::optional<std::size_t>
nonnegative_property(const joggle::Op& op, std::string_view name,
                     joggle::Diagnostics& diagnostics, bool positive = false) {
  const auto value = op.property<std::int64_t>(name);
  if (!value || *value < 0 || (positive && *value == 0) ||
      static_cast<std::uint64_t>(*value) >
          static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
    diagnostics.report("anchor executor has invalid property '" +
                       std::string(name) + "'");
    return std::nullopt;
  }
  return static_cast<std::size_t>(*value);
}

std::optional<Tensor> result_tensor(joggle::Compiler& compiler,
                                    const joggle::Op& op,
                                    const ExecutionSchema& schema,
                                    joggle::Diagnostics& diagnostics) {
  const auto results = op.results();
  if (results.size() != 1U ||
      !f32_reference(compiler, results.front().type(), schema)) {
    diagnostics.report(
        "anchor execute_f32 requires one f32 reference result per call");
    return std::nullopt;
  }
  const auto count = element_count(results.front().type(), diagnostics);
  if (!count) {
    return std::nullopt;
  }
  return Tensor{results.front().type(),
                std::make_shared<std::vector<float>>(*count)};
}

bool same_size(const Tensor& left, const Tensor& right,
               joggle::Diagnostics& diagnostics) {
  if (left.values->size() != right.values->size()) {
    diagnostics.report("anchor executor encountered incompatible tensors");
    return false;
  }
  return true;
}

std::optional<Tensor> evaluate_relu(joggle::Compiler& compiler,
                                    const joggle::Op& op,
                                    const std::vector<TensorBinding>& bindings,
                                    const ExecutionSchema& schema,
                                    joggle::Diagnostics& diagnostics) {
  const auto input = operand_tensor(op, "input", bindings, diagnostics);
  auto output = result_tensor(compiler, op, schema, diagnostics);
  if (!input || !output || !same_size(*input, *output, diagnostics)) {
    return std::nullopt;
  }
  for (std::size_t index = 0; index < output->values->size(); ++index) {
    (*output->values)[index] = std::max((*input->values)[index], 0.0F);
  }
  return output;
}

std::optional<Tensor> evaluate_add(joggle::Compiler& compiler,
                                   const joggle::Op& op,
                                   const std::vector<TensorBinding>& bindings,
                                   const ExecutionSchema& schema,
                                   joggle::Diagnostics& diagnostics) {
  const auto left = operand_tensor(op, "lhs", bindings, diagnostics);
  const auto right = operand_tensor(op, "rhs", bindings, diagnostics);
  auto output = result_tensor(compiler, op, schema, diagnostics);
  if (!left || !right || !output || !same_size(*left, *right, diagnostics) ||
      !same_size(*left, *output, diagnostics)) {
    return std::nullopt;
  }
  for (std::size_t index = 0; index < output->values->size(); ++index) {
    (*output->values)[index] = (*left->values)[index] + (*right->values)[index];
  }
  return output;
}

std::optional<Tensor>
evaluate_convolution(joggle::Compiler& compiler, const joggle::Op& op,
                     const std::vector<TensorBinding>& bindings,
                     const ExecutionSchema& schema, bool fused_relu,
                     joggle::Diagnostics& diagnostics) {
  const auto input = operand_tensor(op, "input", bindings, diagnostics);
  const auto weight = operand_tensor(op, "weight", bindings, diagnostics);
  const auto bias_value = op.operand("bias");
  const auto bias = bias_value
                        ? lookup_tensor(bindings, *bias_value, diagnostics)
                        : std::optional<Tensor>{};
  auto output = result_tensor(compiler, op, schema, diagnostics);
  if (!input || !weight || (bias_value && !bias) || !output) {
    return std::nullopt;
  }
  const auto input_shape = dimensions(input->type, 4U, diagnostics);
  const auto weight_shape = dimensions(weight->type, 4U, diagnostics);
  const auto output_shape = dimensions(output->type, 4U, diagnostics);
  const auto stride_h = nonnegative_property(op, "stride_h", diagnostics, true);
  const auto stride_w = nonnegative_property(op, "stride_w", diagnostics, true);
  const auto dilation_h =
      nonnegative_property(op, "dilation_h", diagnostics, true);
  const auto dilation_w =
      nonnegative_property(op, "dilation_w", diagnostics, true);
  const auto pad_top = nonnegative_property(op, "pad_top", diagnostics);
  const auto pad_left = nonnegative_property(op, "pad_left", diagnostics);
  if (!input_shape || !weight_shape || !output_shape || !stride_h ||
      !stride_w || !dilation_h || !dilation_w || !pad_top || !pad_left) {
    return std::nullopt;
  }
  const auto [batches, input_channels, input_height, input_width] =
      std::tuple{(*input_shape)[0], (*input_shape)[1], (*input_shape)[2],
                 (*input_shape)[3]};
  const auto [output_channels, weight_channels, kernel_height, kernel_width] =
      std::tuple{(*weight_shape)[0], (*weight_shape)[1], (*weight_shape)[2],
                 (*weight_shape)[3]};
  const auto [output_batches, result_channels, output_height, output_width] =
      std::tuple{(*output_shape)[0], (*output_shape)[1], (*output_shape)[2],
                 (*output_shape)[3]};
  if (input_channels != weight_channels || batches != output_batches ||
      output_channels != result_channels ||
      (bias && bias->values->size() != output_channels)) {
    diagnostics.report("anchor convolution tensor shapes are inconsistent");
    return std::nullopt;
  }

  const float* input_data = input->values->data();
  const float* weight_data = weight->values->data();
  float* output_data = output->values->data();
  for (std::size_t batch = 0; batch < batches; ++batch) {
    for (std::size_t output_channel = 0; output_channel < output_channels;
         ++output_channel) {
      for (std::size_t output_y = 0; output_y < output_height; ++output_y) {
        for (std::size_t output_x = 0; output_x < output_width; ++output_x) {
          float accumulator = bias ? (*bias->values)[output_channel] : 0.0F;
          for (std::size_t input_channel = 0; input_channel < input_channels;
               ++input_channel) {
            for (std::size_t kernel_y = 0; kernel_y < kernel_height;
                 ++kernel_y) {
              const std::size_t padded_y =
                  output_y * *stride_h + kernel_y * *dilation_h;
              if (padded_y < *pad_top) {
                continue;
              }
              const std::size_t input_y = padded_y - *pad_top;
              if (input_y >= input_height) {
                continue;
              }
              const std::size_t input_row =
                  ((batch * input_channels + input_channel) * input_height +
                   input_y) *
                  input_width;
              const std::size_t weight_row =
                  ((output_channel * input_channels + input_channel) *
                       kernel_height +
                   kernel_y) *
                  kernel_width;
              for (std::size_t kernel_x = 0; kernel_x < kernel_width;
                   ++kernel_x) {
                const std::size_t padded_x =
                    output_x * *stride_w + kernel_x * *dilation_w;
                if (padded_x < *pad_left) {
                  continue;
                }
                const std::size_t input_x = padded_x - *pad_left;
                if (input_x < input_width) {
                  accumulator += input_data[input_row + input_x] *
                                 weight_data[weight_row + kernel_x];
                }
              }
            }
          }
          const std::size_t output_offset =
              ((batch * output_channels + output_channel) * output_height +
               output_y) *
                  output_width +
              output_x;
          output_data[output_offset] =
              fused_relu ? std::max(accumulator, 0.0F) : accumulator;
        }
      }
    }
  }
  return output;
}

std::optional<Tensor>
evaluate_batch_norm(joggle::Compiler& compiler, const joggle::Op& op,
                    const std::vector<TensorBinding>& bindings,
                    const ExecutionSchema& schema, bool fused_relu,
                    joggle::Diagnostics& diagnostics) {
  const auto input = operand_tensor(op, "input", bindings, diagnostics);
  const auto scale = operand_tensor(op, "scale", bindings, diagnostics);
  const auto bias = operand_tensor(op, "bias", bindings, diagnostics);
  const auto mean = operand_tensor(op, "mean", bindings, diagnostics);
  const auto variance = operand_tensor(op, "variance", bindings, diagnostics);
  const auto epsilon = op.property<double>("epsilon");
  auto output = result_tensor(compiler, op, schema, diagnostics);
  if (!input || !scale || !bias || !mean || !variance || !epsilon || !output ||
      *epsilon < 0.0) {
    diagnostics.report("anchor batch normalization is incomplete");
    return std::nullopt;
  }
  const auto shape = dimensions(input->type, 4U, diagnostics);
  if (!shape || !same_size(*input, *output, diagnostics)) {
    return std::nullopt;
  }
  const std::size_t batches = (*shape)[0];
  const std::size_t channels = (*shape)[1];
  const std::size_t height = (*shape)[2];
  const std::size_t width = (*shape)[3];
  if (scale->values->size() != channels || bias->values->size() != channels ||
      mean->values->size() != channels ||
      variance->values->size() != channels) {
    diagnostics.report(
        "anchor batch normalization parameters are inconsistent");
    return std::nullopt;
  }
  for (std::size_t batch = 0; batch < batches; ++batch) {
    for (std::size_t channel = 0; channel < channels; ++channel) {
      const float denominator = std::sqrt((*variance->values)[channel] +
                                          static_cast<float>(*epsilon));
      for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
          const std::size_t offset =
              ((batch * channels + channel) * height + y) * width + x;
          float value =
              (*scale->values)[channel] *
                  (((*input->values)[offset] - (*mean->values)[channel]) /
                   denominator) +
              (*bias->values)[channel];
          if (fused_relu) {
            value = std::max(value, 0.0F);
          }
          (*output->values)[offset] = value;
        }
      }
    }
  }
  return output;
}

std::optional<Tensor>
evaluate_max_pool(joggle::Compiler& compiler, const joggle::Op& op,
                  const std::vector<TensorBinding>& bindings,
                  const ExecutionSchema& schema,
                  joggle::Diagnostics& diagnostics) {
  const auto input = operand_tensor(op, "input", bindings, diagnostics);
  auto output = result_tensor(compiler, op, schema, diagnostics);
  if (!input || !output) {
    return std::nullopt;
  }
  const auto input_shape = dimensions(input->type, 4U, diagnostics);
  const auto output_shape = dimensions(output->type, 4U, diagnostics);
  const auto kernel_h = nonnegative_property(op, "kernel_h", diagnostics, true);
  const auto kernel_w = nonnegative_property(op, "kernel_w", diagnostics, true);
  const auto stride_h = nonnegative_property(op, "stride_h", diagnostics, true);
  const auto stride_w = nonnegative_property(op, "stride_w", diagnostics, true);
  const auto dilation_h =
      nonnegative_property(op, "dilation_h", diagnostics, true);
  const auto dilation_w =
      nonnegative_property(op, "dilation_w", diagnostics, true);
  const auto pad_top = nonnegative_property(op, "pad_top", diagnostics);
  const auto pad_left = nonnegative_property(op, "pad_left", diagnostics);
  if (!input_shape || !output_shape || !kernel_h || !kernel_w || !stride_h ||
      !stride_w || !dilation_h || !dilation_w || !pad_top || !pad_left) {
    return std::nullopt;
  }
  const std::size_t batches = (*input_shape)[0];
  const std::size_t channels = (*input_shape)[1];
  const std::size_t input_height = (*input_shape)[2];
  const std::size_t input_width = (*input_shape)[3];
  const std::size_t output_height = (*output_shape)[2];
  const std::size_t output_width = (*output_shape)[3];
  if ((*output_shape)[0] != batches || (*output_shape)[1] != channels) {
    diagnostics.report("anchor pooling tensor shapes are inconsistent");
    return std::nullopt;
  }
  for (std::size_t batch = 0; batch < batches; ++batch) {
    for (std::size_t channel = 0; channel < channels; ++channel) {
      for (std::size_t output_y = 0; output_y < output_height; ++output_y) {
        for (std::size_t output_x = 0; output_x < output_width; ++output_x) {
          float accumulator = 0.0F;
          bool initialized = false;
          for (std::size_t kernel_y = 0; kernel_y < *kernel_h; ++kernel_y) {
            const std::size_t padded_y =
                output_y * *stride_h + kernel_y * *dilation_h;
            if (padded_y < *pad_top) {
              continue;
            }
            const std::size_t input_y = padded_y - *pad_top;
            if (input_y >= input_height) {
              continue;
            }
            for (std::size_t kernel_x = 0; kernel_x < *kernel_w; ++kernel_x) {
              const std::size_t padded_x =
                  output_x * *stride_w + kernel_x * *dilation_w;
              if (padded_x < *pad_left) {
                continue;
              }
              const std::size_t input_x = padded_x - *pad_left;
              if (input_x >= input_width) {
                continue;
              }
              const std::size_t input_offset =
                  ((batch * channels + channel) * input_height + input_y) *
                      input_width +
                  input_x;
              const float value = (*input->values)[input_offset];
              accumulator = initialized ? std::max(accumulator, value) : value;
              initialized = true;
            }
          }
          if (!initialized) {
            diagnostics.report("anchor pooling window has no input element");
            return std::nullopt;
          }
          const std::size_t output_offset =
              ((batch * channels + channel) * output_height + output_y) *
                  output_width +
              output_x;
          (*output->values)[output_offset] = accumulator;
        }
      }
    }
  }
  return output;
}

std::optional<Tensor>
evaluate_global_average(joggle::Compiler& compiler, const joggle::Op& op,
                        const std::vector<TensorBinding>& bindings,
                        const ExecutionSchema& schema,
                        joggle::Diagnostics& diagnostics) {
  const auto input = operand_tensor(op, "input", bindings, diagnostics);
  auto output = result_tensor(compiler, op, schema, diagnostics);
  if (!input || !output) {
    return std::nullopt;
  }
  const auto shape = dimensions(input->type, 4U, diagnostics);
  if (!shape) {
    return std::nullopt;
  }
  const std::size_t batches = (*shape)[0];
  const std::size_t channels = (*shape)[1];
  const std::size_t height = (*shape)[2];
  const std::size_t width = (*shape)[3];
  if (output->values->size() != batches * channels) {
    diagnostics.report("anchor global pooling result shape is inconsistent");
    return std::nullopt;
  }
  const float area = static_cast<float>(height * width);
  for (std::size_t batch = 0; batch < batches; ++batch) {
    for (std::size_t channel = 0; channel < channels; ++channel) {
      float accumulator = 0.0F;
      const std::size_t base = (batch * channels + channel) * height * width;
      for (std::size_t index = 0; index < height * width; ++index) {
        accumulator += (*input->values)[base + index];
      }
      (*output->values)[batch * channels + channel] = accumulator / area;
    }
  }
  return output;
}

std::optional<Tensor>
evaluate_flatten(joggle::Compiler& compiler, const joggle::Op& op,
                 const std::vector<TensorBinding>& bindings,
                 const ExecutionSchema& schema,
                 joggle::Diagnostics& diagnostics) {
  const auto input = operand_tensor(op, "input", bindings, diagnostics);
  auto output = result_tensor(compiler, op, schema, diagnostics);
  if (!input || !output || !same_size(*input, *output, diagnostics)) {
    return std::nullopt;
  }
  *output->values = *input->values;
  return output;
}

std::optional<Tensor>
evaluate_linear(joggle::Compiler& compiler, const joggle::Op& op,
                const std::vector<TensorBinding>& bindings,
                const ExecutionSchema& schema,
                joggle::Diagnostics& diagnostics) {
  const auto input = operand_tensor(op, "input", bindings, diagnostics);
  const auto weight = operand_tensor(op, "weight", bindings, diagnostics);
  const auto bias = operand_tensor(op, "bias", bindings, diagnostics);
  auto output = result_tensor(compiler, op, schema, diagnostics);
  if (!input || !weight || !bias || !output) {
    return std::nullopt;
  }
  const auto input_shape = dimensions(input->type, 2U, diagnostics);
  const auto weight_shape = dimensions(weight->type, 2U, diagnostics);
  const auto output_shape = dimensions(output->type, 2U, diagnostics);
  if (!input_shape || !weight_shape || !output_shape) {
    return std::nullopt;
  }
  const std::size_t rows = (*input_shape)[0];
  const std::size_t reduction = (*input_shape)[1];
  const std::size_t columns = (*weight_shape)[0];
  if ((*weight_shape)[1] != reduction || (*output_shape)[0] != rows ||
      (*output_shape)[1] != columns || bias->values->size() != columns) {
    diagnostics.report("anchor linear tensor shapes are inconsistent");
    return std::nullopt;
  }
  for (std::size_t row = 0; row < rows; ++row) {
    for (std::size_t column = 0; column < columns; ++column) {
      float accumulator = (*bias->values)[column];
      for (std::size_t index = 0; index < reduction; ++index) {
        accumulator += (*input->values)[row * reduction + index] *
                       (*weight->values)[column * reduction + index];
      }
      (*output->values)[row * columns + column] = accumulator;
    }
  }
  return output;
}

std::optional<Tensor> evaluate_call(joggle::Compiler& compiler,
                                    const joggle::Op& op,
                                    const std::vector<TensorBinding>& bindings,
                                    const ExecutionSchema& schema,
                                    joggle::Diagnostics& diagnostics) {
  if (op.callee().symbol().module_name() != schema.target.name()) {
    diagnostics.report("anchor executor encountered foreign call '" +
                       op.callee().symbol().qualified_name() + "'");
    return std::nullopt;
  }
  const std::string_view name = op.callee().name();
  if (name == "relu") {
    return evaluate_relu(compiler, op, bindings, schema, diagnostics);
  }
  if (name == "add") {
    return evaluate_add(compiler, op, bindings, schema, diagnostics);
  }
  if (name == "conv2d_nchw" || name == "conv_relu_nchw") {
    return evaluate_convolution(compiler, op, bindings, schema,
                                name == "conv_relu_nchw", diagnostics);
  }
  if (name == "batch_norm_nchw" || name == "batch_norm_relu_nchw") {
    return evaluate_batch_norm(compiler, op, bindings, schema,
                               name == "batch_norm_relu_nchw", diagnostics);
  }
  if (name == "max_pool2d_nchw") {
    return evaluate_max_pool(compiler, op, bindings, schema, diagnostics);
  }
  if (name == "global_average_pool_nchw") {
    return evaluate_global_average(compiler, op, bindings, schema, diagnostics);
  }
  if (name == "flatten_nchw") {
    return evaluate_flatten(compiler, op, bindings, schema, diagnostics);
  }
  if (name == "linear") {
    return evaluate_linear(compiler, op, bindings, schema, diagnostics);
  }
  diagnostics.report("anchor execute_f32 has no implementation for '" +
                     std::string(name) + "'");
  return std::nullopt;
}

}  // namespace

std::optional<joggle::Bytes> execute_f32(joggle::Compiler& compiler,
                                         const joggle::Module& program,
                                         const joggle::Bytes& input,
                                         const ExecutionSchema& schema,
                                         joggle::Diagnostics& diagnostics) {
  const auto members = program.functions();
  if (members.size() != 1U || members.front().body() == nullptr) {
    diagnostics.report(
        "anchor execute_f32 requires one materialized entry Function");
    return std::nullopt;
  }
  const joggle::Function& function_body = *members.front().body();
  const auto blocks = function_body.blocks();
  const auto arguments = function_body.arguments();
  if (blocks.size() != 1U || !blocks.front().arguments().empty() ||
      blocks.front().terminator().kind() != joggle::Terminator::Kind::Return ||
      arguments.size() != 1U ||
      !f32_reference(compiler, arguments.front().type(), schema)) {
    diagnostics.report(
        "anchor execute_f32 requires a straight-line single-input f32 graph");
    return std::nullopt;
  }
  const auto input_count = element_count(arguments.front().type(), diagnostics);
  const auto input_values =
      input_count
          ? decode_f32(std::span<const std::byte>(input.data(), input.size()),
                       *input_count, diagnostics, "anchor executor input")
          : std::optional<std::vector<float>>{};
  if (!input_values) {
    return std::nullopt;
  }

  std::vector<TensorBinding> bindings;
  bindings.push_back(TensorBinding{
      arguments.front(),
      Tensor{arguments.front().type(),
             std::make_shared<std::vector<float>>(*input_values)}});
  for (const auto& op : function_body.ops()) {
    const auto results = op.results();
    if (results.size() != 1U) {
      diagnostics.report(
          "anchor execute_f32 requires single-result graph operations");
      return std::nullopt;
    }
    if (compiler.conforms(op.callee(), schema.placement)) {
      const auto source = operand_tensor(op, "input", bindings, diagnostics);
      if (!source || source->type != results.front().type()) {
        diagnostics.report("anchor placement changed the represented tensor");
        return std::nullopt;
      }
      bindings.push_back(TensorBinding{results.front(), *source});
      continue;
    }
    if (op.callee().symbol().module_name() == schema.target.name() &&
        op.callee().name() == "constant") {
      const auto resource = op.property<std::string>("resource");
      const auto count = element_count(results.front().type(), diagnostics);
      const auto payload = resource
                               ? program.data(*resource)
                               : std::optional<std::span<const std::byte>>{};
      const auto values =
          payload && count ? decode_f32(*payload, *count, diagnostics,
                                        "anchor constant '" + *resource + "'")
                           : std::optional<std::vector<float>>{};
      if (!resource || !count || !payload || !values ||
          !f32_reference(compiler, results.front().type(), schema)) {
        if (!resource || !payload) {
          diagnostics.report("anchor executor cannot resolve constant data");
        }
        return std::nullopt;
      }
      bindings.push_back(
          TensorBinding{results.front(),
                        Tensor{results.front().type(),
                               std::make_shared<std::vector<float>>(*values)}});
      continue;
    }
    const auto result =
        evaluate_call(compiler, op, bindings, schema, diagnostics);
    if (!result) {
      return std::nullopt;
    }
    bindings.push_back(TensorBinding{results.front(), *result});
  }

  const auto returned = blocks.front().terminator().returned();
  if (returned.size() != 1U) {
    diagnostics.report("anchor execute_f32 requires one returned tensor");
    return std::nullopt;
  }
  const auto output = lookup_tensor(bindings, returned.front(), diagnostics);
  return output ? std::optional<joggle::Bytes>{encode_f32(*output->values)}
                : std::nullopt;
}

}  // namespace joggle::anchor
