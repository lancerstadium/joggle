#ifndef JOGGLE_RESEARCH_RESIDUAL_ARTIFACT_ENCODING_H
#define JOGGLE_RESEARCH_RESIDUAL_ARTIFACT_ENCODING_H

#include "residual/oracle.h"

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

namespace residual {

struct EncodedPaths {
  std::vector<std::uint8_t> bytes;
  std::vector<std::string> canonical_ids;

  [[nodiscard]] std::uint8_t index_of(const Realization& realization) const;
};

// Canonically serializes the F1 fact schema, all referenced atom definitions,
// unique realization descriptors, and an explicit safe fallback. Selector
// payloads append to this prefix and must use index_of() for path references.
[[nodiscard]] EncodedPaths encode_f1_paths(
    const std::vector<Realization>& referenced, bool include_costs);
[[nodiscard]] std::vector<std::uint8_t> serialize_f1_dynamic_choice(
    const std::vector<Realization>& choices, std::uint8_t selector_tag);

void append_u8(std::vector<std::uint8_t>* bytes, std::uint8_t value);
void append_u32(std::vector<std::uint8_t>* bytes, std::uint32_t value);
[[nodiscard]] bool artifact_encoding_self_test(std::ostream& errors);

}  // namespace residual

#endif
