#include "residual/artifact_encoding.h"

#include <algorithm>
#include <array>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <string_view>

namespace residual {
namespace {

struct AtomRecord {
  std::string_view id;
  std::uint8_t scratch;
  bool requires_tensor;
  std::array<std::uint32_t, 4> costs;
};

constexpr std::array<AtomRecord, 10> kAtoms{{
    {"act.nchw", 1, false, {3, 5, 3, 5}},
    {"act.nhwc", 0, false, {2, 3, 2, 3}},
    {"act.none", 0, false, {0, 0, 0, 0}},
    {"join.nchw", 1, false, {2, 3, 2, 3}},
    {"join.nhwc_store", 2, false, {4, 5, 3, 4}},
    {"skip.nchw", 0, false, {1, 2, 1, 2}},
    {"skip.nhwc", 1, false, {3, 4, 4, 5}},
    {"stem.nchw", 1, false, {8, 20, 10, 24}},
    {"stem.nhwc", 1, true, {10, 12, 8, 13}},
    {"stem.nhwc_relu", 4, true, {8, 9, 7, 10}},
}};

[[nodiscard]] const AtomRecord& atom_record(const std::string& id) {
  const auto found = std::find_if(kAtoms.begin(), kAtoms.end(),
                                  [&id](const AtomRecord& atom) {
                                    return atom.id == id;
                                  });
  if (found == kAtoms.end()) {
    throw std::invalid_argument("unknown F1 atom: " + id);
  }
  return *found;
}

void append_string(std::vector<std::uint8_t>* bytes,
                   const std::string_view value) {
  if (value.size() > std::numeric_limits<std::uint8_t>::max()) {
    throw std::length_error("artifact string exceeds u8 length");
  }
  append_u8(bytes, static_cast<std::uint8_t>(value.size()));
  for (const char character : value) {
    append_u8(bytes, static_cast<std::uint8_t>(character));
  }
}

[[nodiscard]] Realization safe_fallback() {
  return f1_realization_universe().front();
}

}  // namespace

std::uint8_t EncodedPaths::index_of(const Realization& realization) const {
  const std::string id = realization.canonical_id();
  const auto found = std::lower_bound(canonical_ids.begin(),
                                      canonical_ids.end(), id);
  if (found == canonical_ids.end() || *found != id) {
    throw std::invalid_argument("realization is absent from artifact");
  }
  return static_cast<std::uint8_t>(
      static_cast<std::size_t>(found - canonical_ids.begin()));
}

EncodedPaths encode_f1_paths(const std::vector<Realization>& referenced,
                             const bool include_costs) {
  std::vector<Realization> paths = referenced;
  paths.push_back(safe_fallback());
  std::sort(paths.begin(), paths.end(),
            [](const Realization& lhs, const Realization& rhs) {
              return lhs.canonical_id() < rhs.canonical_id();
            });
  paths.erase(std::unique(paths.begin(), paths.end(),
                          [](const Realization& lhs, const Realization& rhs) {
                            return lhs.canonical_id() == rhs.canonical_id();
                          }),
              paths.end());
  if (paths.size() > std::numeric_limits<std::uint8_t>::max()) {
    throw std::length_error("too many paths for F1 encoding");
  }

  std::vector<std::string> atom_ids;
  for (const Realization& path : paths) {
    atom_ids.insert(atom_ids.end(), path.atoms.begin(), path.atoms.end());
  }
  std::sort(atom_ids.begin(), atom_ids.end());
  atom_ids.erase(std::unique(atom_ids.begin(), atom_ids.end()), atom_ids.end());
  if (atom_ids.size() > std::numeric_limits<std::uint8_t>::max()) {
    throw std::length_error("too many atoms for F1 encoding");
  }

  EncodedPaths encoded;
  encoded.bytes = {'J', 'R', 'D', '1'};
  append_u8(&encoded.bytes, 1U);  // Encoding version.
  append_u8(&encoded.bytes, 5U);  // Five binary fact dimensions.
  append_u8(&encoded.bytes, include_costs ? 1U : 0U);
  append_u8(&encoded.bytes, static_cast<std::uint8_t>(atom_ids.size()));
  for (const std::string& atom_id : atom_ids) {
    const AtomRecord& atom = atom_record(atom_id);
    append_string(&encoded.bytes, atom.id);
    append_u8(&encoded.bytes, atom.scratch);
    append_u8(&encoded.bytes, atom.requires_tensor ? 1U : 0U);
    if (include_costs) {
      for (const std::uint32_t cost : atom.costs) {
        append_u32(&encoded.bytes, cost);
      }
    }
  }

  append_u8(&encoded.bytes, static_cast<std::uint8_t>(paths.size()));
  for (const Realization& path : paths) {
    if (path.atoms.size() > std::numeric_limits<std::uint8_t>::max()) {
      throw std::length_error("too many atoms in F1 path");
    }
    append_u8(&encoded.bytes, static_cast<std::uint8_t>(path.atoms.size()));
    for (const std::string& atom_id : path.atoms) {
      const auto found =
          std::lower_bound(atom_ids.begin(), atom_ids.end(), atom_id);
      append_u8(&encoded.bytes, static_cast<std::uint8_t>(
                                    static_cast<std::size_t>(found -
                                                             atom_ids.begin())));
    }
    encoded.canonical_ids.push_back(path.canonical_id());
  }
  append_u8(&encoded.bytes, encoded.index_of(safe_fallback()));
  return encoded;
}

std::vector<std::uint8_t> serialize_f1_dynamic_choice(
    const std::vector<Realization>& choices, const std::uint8_t selector_tag) {
  EncodedPaths encoded = encode_f1_paths(choices, true);
  append_u8(&encoded.bytes, selector_tag);
  if (choices.size() > std::numeric_limits<std::uint8_t>::max()) {
    throw std::length_error("too many dynamic choices");
  }
  append_u8(&encoded.bytes, static_cast<std::uint8_t>(choices.size()));
  bool has_unfused_nhwc = false;
  bool has_multiple_layouts = false;
  bool saw_nchw = false;
  bool saw_nhwc = false;
  const std::vector<Realization> universe = f1_realization_universe();
  for (const Realization& choice : choices) {
    const std::string id = choice.canonical_id();
    const auto found = std::find_if(
        universe.begin(), universe.end(), [&id](const Realization& candidate) {
          return candidate.canonical_id() == id;
        });
    if (found == universe.end()) {
      throw std::invalid_argument("unknown dynamic F1 choice");
    }
    const std::size_t kind =
        static_cast<std::size_t>(found - universe.begin());
    append_u8(&encoded.bytes, encoded.index_of(choice));
    append_u8(&encoded.bytes, kind == 0U ? 0U : 1U);
    append_u8(&encoded.bytes,
              kind == 0U ? 0U : (kind == 1U ? 4U : 7U));
    append_u8(&encoded.bytes, kind == 0U ? 0U : 1U);
    has_unfused_nhwc = has_unfused_nhwc || kind == 1U;
    saw_nchw = saw_nchw || kind == 0U;
    saw_nhwc = saw_nhwc || kind != 0U;
  }
  has_multiple_layouts = saw_nchw && saw_nhwc;
  append_u8(&encoded.bytes, has_unfused_nhwc ? 1U : 0U);
  if (has_unfused_nhwc) {
    append_u8(&encoded.bytes, encoded.index_of(universe[1]));
    append_u8(&encoded.bytes, 1U << 0U);  // Shape is specified.
    append_u8(&encoded.bytes, 0U);        // Shape=small.
    append_u32(&encoded.bytes, 2U);
    append_u32(&encoded.bytes, 1U);
  }
  append_u8(&encoded.bytes, has_multiple_layouts ? 1U : 0U);
  if (has_multiple_layouts) {
    append_u32(&encoded.bytes, 3U);
    append_u32(&encoded.bytes, 2U);
  }
  return encoded.bytes;
}

void append_u8(std::vector<std::uint8_t>* bytes, const std::uint8_t value) {
  bytes->push_back(value);
}

void append_u32(std::vector<std::uint8_t>* bytes, const std::uint32_t value) {
  for (std::uint32_t shift = 0; shift < 32U; shift += 8U) {
    append_u8(bytes, static_cast<std::uint8_t>((value >> shift) & 0xffU));
  }
}

bool artifact_encoding_self_test(std::ostream& errors) {
  const std::vector<Realization> universe = f1_realization_universe();
  const EncodedPaths forward = encode_f1_paths(universe, true);
  std::vector<Realization> reordered = {universe[2], universe[0], universe[1],
                                        universe[2]};
  const EncodedPaths reverse = encode_f1_paths(reordered, true);
  if (forward.bytes != reverse.bytes ||
      forward.canonical_ids != reverse.canonical_ids) {
    errors << "artifact path encoding is not canonical\n";
    return false;
  }
  if (forward.bytes.size() < 4U || forward.bytes[0] != 'J' ||
      forward.bytes[1] != 'R' || forward.bytes[2] != 'D' ||
      forward.bytes[3] != '1') {
    errors << "artifact path encoding has invalid magic\n";
    return false;
  }
  for (const Realization& realization : universe) {
    if (forward.canonical_ids[forward.index_of(realization)] !=
        realization.canonical_id()) {
      errors << "artifact path index is inconsistent\n";
      return false;
    }
  }
  return true;
}

}  // namespace residual
