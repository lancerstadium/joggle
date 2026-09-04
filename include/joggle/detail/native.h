#pragma once

#include <cstddef>
#include <cstdint>

namespace joggle {

class Compiler;
class Diag;
class Mod;

namespace detail {
extern const char native_mod_identity[];

inline constexpr std::uint32_t native_abi = 1;
inline constexpr const char* native_entry = "joggle_native_v1";

#if defined(_WIN32) && defined(_M_X64)
inline constexpr const char* native_target = "windows-x86_64";
#elif defined(_WIN32) && defined(_M_ARM64)
inline constexpr const char* native_target = "windows-arm64";
#elif defined(__APPLE__) && defined(__aarch64__)
inline constexpr const char* native_target = "macos-arm64";
#elif defined(__APPLE__) && defined(__x86_64__)
inline constexpr const char* native_target = "macos-x86_64";
#elif defined(__linux__) && defined(__aarch64__)
inline constexpr const char* native_target = "linux-arm64";
#elif defined(__linux__) && defined(__x86_64__)
inline constexpr const char* native_target = "linux-x86_64";
#else
inline constexpr const char* native_target = "unknown-target";
#endif

struct NativeLib {
  using Load = void (*)(Compiler&, const Mod&, Diag&);

  constexpr explicit NativeLib(Load fn)
      : abi(native_abi), size(sizeof(NativeLib)),
        mod_identity(detail::native_mod_identity), target(native_target),
        load(fn) {}

  std::uint32_t abi;
  std::size_t size;
  const char* mod_identity;
  const char* target;
  Load load;
};

using NativeEntry = const NativeLib* (*)();

}  // namespace detail

}  // namespace joggle
