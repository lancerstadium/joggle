#include "artifact.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace joggle::anchor {
namespace {

constexpr std::string_view magic = "JOGGLE_DATA_1";

void append(Bytes& output, std::string_view text) {
  output.reserve(output.size() + text.size());
  for (const char value : text) {
    output.push_back(
        static_cast<std::byte>(static_cast<unsigned char>(value)));
  }
}

void append_u64(Bytes& output, std::uint64_t value) {
  for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
    output.push_back(static_cast<std::byte>((value >> (byte * 8U)) & 0xffU));
  }
}

std::optional<std::uint64_t> size(std::size_t value,
                                  Diagnostics& diagnostics) {
  if constexpr (sizeof(std::size_t) > sizeof(std::uint64_t)) {
    if (value > static_cast<std::size_t>(
                    std::numeric_limits<std::uint64_t>::max())) {
      diagnostics.report("anchor artifact exceeds its 64-bit size domain");
      return std::nullopt;
    }
  }
  return static_cast<std::uint64_t>(value);
}

class Reader {
public:
  Reader(const Bytes& input, std::size_t offset)
      : input_(input), offset_(offset) {}

  std::optional<std::uint64_t> u64() {
    if (remaining() < sizeof(std::uint64_t)) {
      return std::nullopt;
    }
    std::uint64_t result = 0;
    for (std::size_t byte = 0; byte < sizeof(result); ++byte) {
      result |= std::to_integer<std::uint64_t>(input_[offset_ + byte])
                << (byte * 8U);
    }
    offset_ += sizeof(result);
    return result;
  }

  std::optional<std::string> text(std::uint64_t count) {
    const auto length = fit(count);
    if (!length || *length > remaining()) {
      return std::nullopt;
    }
    std::string result;
    result.reserve(*length);
    for (std::size_t index = 0; index < *length; ++index) {
      result.push_back(static_cast<char>(
          std::to_integer<unsigned char>(input_[offset_ + index])));
    }
    offset_ += *length;
    return result;
  }

  std::optional<Bytes> bytes(std::uint64_t count) {
    const auto length = fit(count);
    if (!length || *length > remaining()) {
      return std::nullopt;
    }
    Bytes result(input_.begin() + static_cast<std::ptrdiff_t>(offset_),
                 input_.begin() +
                     static_cast<std::ptrdiff_t>(offset_ + *length));
    offset_ += *length;
    return result;
  }

  bool done() const { return offset_ == input_.size(); }

private:
  std::optional<std::size_t> fit(std::uint64_t value) const {
    if (value > static_cast<std::uint64_t>(
                    std::numeric_limits<std::size_t>::max())) {
      return std::nullopt;
    }
    return static_cast<std::size_t>(value);
  }

  std::size_t remaining() const { return input_.size() - offset_; }

  const Bytes& input_;
  std::size_t offset_ = 0;
};

struct Identity {
  std::string name;
  std::string digest;
};

std::optional<Identity> identity(std::string_view manifest,
                                 Diagnostics& diagnostics) {
  if (!manifest.starts_with("anchor 3\n")) {
    diagnostics.report("anchor artifact has an unsupported manifest version");
    return std::nullopt;
  }
  constexpr std::string_view prefix = "\nbundle ";
  const std::size_t begin = manifest.find(prefix);
  const std::size_t end =
      begin == std::string_view::npos
          ? std::string_view::npos
          : manifest.find('\n', begin + prefix.size());
  if (begin == std::string_view::npos || end == std::string_view::npos) {
    diagnostics.report("anchor artifact has no bundle identity");
    return std::nullopt;
  }
  const std::string_view value =
      manifest.substr(begin + prefix.size(), end - begin - prefix.size());
  const std::size_t hash = value.rfind('#');
  if (hash == std::string_view::npos || hash == 0U ||
      hash + 1U == value.size()) {
    diagnostics.report("anchor artifact has a malformed bundle identity");
    return std::nullopt;
  }
  return Identity{std::string(value.substr(0, hash)),
                  std::string(value.substr(hash + 1U))};
}

void forward(const Diagnostics& source, Diagnostics& destination) {
  for (const Diagnostic& diagnostic : source.entries()) {
    destination.report(diagnostic);
  }
}

}  // namespace

std::optional<Bytes> pack_artifact(std::string_view manifest,
                                   const Module& bundle,
                                   Diagnostics& diagnostics) {
  if (manifest.find('\0') != std::string_view::npos) {
    diagnostics.report("anchor artifact manifest contains a zero byte");
    return std::nullopt;
  }
  const auto manifest_size = size(manifest.size(), diagnostics);
  const auto resource_count = size(bundle.data().size(), diagnostics);
  if (!manifest_size || !resource_count) {
    return std::nullopt;
  }

  Bytes output;
  append(output, manifest);
  output.push_back(std::byte{0});
  append(output, magic);
  append_u64(output, *resource_count);
  for (const std::string& name : bundle.data()) {
    const auto payload = bundle.data(name);
    const auto name_size = size(name.size(), diagnostics);
    const auto payload_size =
        payload ? size(payload->size(), diagnostics) : std::nullopt;
    if (!payload || !name_size || !payload_size) {
      if (!payload) {
        diagnostics.report("anchor artifact lost resource '" + name + "'");
      }
      return std::nullopt;
    }
    append_u64(output, *name_size);
    append_u64(output, *payload_size);
    append(output, name);
    output.insert(output.end(), payload->begin(), payload->end());
  }
  return output;
}

std::optional<Module> unpack_artifact(Compiler& compiler,
                                      const Bytes& artifact,
                                      Diagnostics& diagnostics) {
  const auto separator =
      std::find(artifact.begin(), artifact.end(), std::byte{0});
  if (separator == artifact.end()) {
    diagnostics.report("anchor artifact has no payload separator");
    return std::nullopt;
  }
  const std::size_t manifest_size =
      static_cast<std::size_t>(separator - artifact.begin());
  std::string manifest;
  manifest.reserve(manifest_size);
  for (std::size_t index = 0; index < manifest_size; ++index) {
    manifest.push_back(static_cast<char>(
        std::to_integer<unsigned char>(artifact[index])));
  }
  const auto expected = identity(manifest, diagnostics);
  constexpr std::string_view module_separator = "\n---\n";
  const std::size_t module_begin = manifest.find(module_separator);
  if (!expected || module_begin == std::string::npos) {
    if (expected) {
      diagnostics.report("anchor artifact has no bundled Module");
    }
    return std::nullopt;
  }
  const std::string module_text =
      manifest.substr(module_begin + module_separator.size());
  auto parsed = parse_module(module_text, diagnostics, "<anchor-artifact>");
  if (!parsed || parsed->name() != expected->name || !parsed->data().empty()) {
    if (parsed && parsed->name() != expected->name) {
      diagnostics.report("anchor artifact bundle name does not match its "
                         "manifest");
    } else if (parsed && !parsed->data().empty()) {
      diagnostics.report("anchor artifact text unexpectedly contains data");
    }
    return std::nullopt;
  }

  const std::size_t payload_begin = manifest_size + 1U;
  if (artifact.size() - payload_begin < magic.size() ||
      !std::equal(magic.begin(), magic.end(),
                  artifact.begin() +
                      static_cast<std::ptrdiff_t>(payload_begin),
                  [](char left, std::byte right) {
                    return static_cast<unsigned char>(left) ==
                           std::to_integer<unsigned char>(right);
                  })) {
    diagnostics.report("anchor artifact has an invalid payload magic");
    return std::nullopt;
  }
  Reader reader(artifact, payload_begin + magic.size());
  const auto count = reader.u64();
  if (!count || *count > static_cast<std::uint64_t>(artifact.size())) {
    diagnostics.report("anchor artifact has an invalid resource count");
    return std::nullopt;
  }
  std::set<std::string> names;
  for (std::uint64_t index = 0; index < *count; ++index) {
    const auto name_size = reader.u64();
    const auto payload_size = reader.u64();
    auto name = name_size ? reader.text(*name_size) : std::nullopt;
    auto payload = payload_size ? reader.bytes(*payload_size) : std::nullopt;
    if (!name_size || !payload_size || !name || !payload ||
        !names.insert(*name).second) {
      diagnostics.report("anchor artifact has a malformed resource record");
      return std::nullopt;
    }
    if (parsed->store(std::move(*payload)) != *name) {
      diagnostics.report("anchor artifact resource '" + *name +
                         "' fails its content identity");
      return std::nullopt;
    }
  }
  if (!reader.done() || parsed->data().size() != names.size() ||
      parsed->digest() != expected->digest) {
    diagnostics.report("anchor artifact does not match its bundle digest");
    return std::nullopt;
  }

  Compiler loader(compiler.evaluation_limits());
  for (const Module& module : compiler.modules()) {
    if (module.name() != "prelude" && module.name() != parsed->name()) {
      loader.add(module);
    }
  }
  loader.add(*parsed);
  if (!loader.link()) {
    forward(loader.diagnostics(), diagnostics);
    return std::nullopt;
  }
  const auto linked = loader.module(parsed->name());
  auto materialized = linked ? loader.materialize(*linked) : std::nullopt;
  if (!materialized) {
    forward(loader.diagnostics(), diagnostics);
    return std::nullopt;
  }
  if (materialized->digest() != expected->digest) {
    diagnostics.report("anchor artifact changed while materializing its "
                       "bundle");
    return std::nullopt;
  }
  return materialized;
}

}  // namespace joggle::anchor
