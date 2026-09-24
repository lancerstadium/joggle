#include "detail.h"

#include <array>
#include <bit>
#include <stdexcept>

namespace joggle::detail {

// Byte-oriented SHA-256, following RFC 6234 sections 4--6. This identity is
// independent of std::hash, host byte order, and process-local addresses.
std::string sha256(std::string_view bytes) {
  if (bytes.size() > std::numeric_limits<std::uint64_t>::max() / 8)
    throw std::length_error("SHA-256 input exceeds its bit-length field");
  constexpr std::array<std::uint32_t, 64> constants{
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
      0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
      0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
      0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
      0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
      0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
      0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
      0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
      0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
      0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
      0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
      0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
      0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
      0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
      0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
      0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
  std::array<std::uint32_t, 8> state{
      0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  const auto length = static_cast<std::uint64_t>(bytes.size()) * 8;
  const auto blocks = bytes.size() / 64 + (bytes.size() % 64 < 56 ? 1 : 2);
  for (std::size_t block = 0; block < blocks; ++block) {
    std::array<std::uint32_t, 64> words{};
    for (std::size_t index = 0; index < 64; ++index) {
      const auto offset = block * 64 + index;
      std::uint32_t byte = 0;
      if (offset < bytes.size())
        byte = static_cast<unsigned char>(bytes[offset]);
      else if (offset == bytes.size())
        byte = 0x80;
      else if (block + 1 == blocks && index >= 56)
        byte = (length >> ((63 - index) * 8)) & 255;
      words[index / 4] |= byte << ((3 - index % 4) * 8);
    }
    for (std::size_t index = 16; index < 64; ++index) {
      const auto x = words[index - 15], y = words[index - 2];
      words[index] = words[index - 16] + words[index - 7] +
          (std::rotr(x, 7) ^ std::rotr(x, 18) ^ (x >> 3)) +
          (std::rotr(y, 17) ^ std::rotr(y, 19) ^ (y >> 10));
    }
    auto [a, b, c, d, e, f, g, h] = state;
    for (std::size_t index = 0; index < 64; ++index) {
      const auto first = h + (std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25)) +
          ((e & f) ^ (~e & g)) + constants[index] + words[index];
      const auto second = (std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22)) +
          ((a & b) ^ (a & c) ^ (b & c));
      h = g; g = f; f = e; e = d + first;
      d = c; c = b; b = a; a = first + second;
    }
    const std::array next{a, b, c, d, e, f, g, h};
    for (std::size_t index = 0; index < state.size(); ++index)
      state[index] += next[index];
  }
  constexpr char digits[] = "0123456789abcdef";
  std::string out;
  out.reserve(64);
  for (auto word : state)
    for (int shift = 28; shift >= 0; shift -= 4)
      out.push_back(digits[(word >> shift) & 15]);
  return out;
}

std::string fingerprint(const Attr& value) {
  std::string bytes = "joggle-content-v1";
  const auto integer = [&](std::uint64_t number) {
    for (int shift = 56; shift >= 0; shift -= 8)
      bytes.push_back(static_cast<char>((number >> shift) & 255));
  };
  const auto text = [&](std::string_view text) {
    integer(text.size());
    bytes.append(text);
  };
  const auto encode = [&](const auto& self, const Attr& item) -> void {
    bytes.push_back(static_cast<char>(item.data().index()));
    if (auto v = item.boolean()) bytes.push_back(*v ? 1 : 0);
    else if (auto v = item.integer()) integer(static_cast<std::uint64_t>(*v));
    else if (auto v = item.real()) integer(std::bit_cast<std::uint64_t>(*v));
    else if (auto v = item.string()) text(*v);
    else if (const auto* v = item.bytes()) {
      integer(v->size());
      for (auto byte : *v) bytes.push_back(static_cast<char>(byte));
    } else if (const auto* v = item.list()) {
      integer(v->size());
      for (const auto& child : *v) self(self, child);
    } else if (const auto* v = item.dict()) {
      integer(v->size());
      for (const auto& [key, child] : *v) { text(key); self(self, child); }
    }
  };
  encode(encode, value);
  return "sha256:" + sha256(bytes);
}

}  // namespace joggle::detail
