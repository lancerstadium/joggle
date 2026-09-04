#pragma once

#include <cstddef>
#include <cstdint>

namespace joggle {

class Compiler;
class Diagnostics;
class Module;

namespace detail {
extern const char native_module_identity[];

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

struct NativeLibrary {
  using Load = void (*)(Compiler&, const Module&, Diagnostics&);

  constexpr explicit NativeLibrary(Load function)
      : abi(native_abi), size(sizeof(NativeLibrary)),
        module_identity(detail::native_module_identity), target(native_target),
        load(function) {}

  std::uint32_t abi;
  std::size_t size;
  const char* module_identity;
  const char* target;
  Load load;
};

using NativeEntry = const NativeLibrary* (*)();

}  // namespace detail

}  // namespace joggle
