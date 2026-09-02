#pragma once

#include <cstddef>
#include <cstdint>

namespace joggle {

class Compiler;
class Diagnostics;
class Module;

namespace detail {
extern const char behavior_module_identity[];
}

inline constexpr std::uint32_t behavior_abi = 1;
inline constexpr const char* behavior_entry = "joggle_behavior_v1";

#if defined(_WIN32) && defined(_M_X64)
inline constexpr const char* behavior_target = "windows-x86_64";
#elif defined(_WIN32) && defined(_M_ARM64)
inline constexpr const char* behavior_target = "windows-arm64";
#elif defined(__APPLE__) && defined(__aarch64__)
inline constexpr const char* behavior_target = "macos-arm64";
#elif defined(__APPLE__) && defined(__x86_64__)
inline constexpr const char* behavior_target = "macos-x86_64";
#elif defined(__linux__) && defined(__aarch64__)
inline constexpr const char* behavior_target = "linux-arm64";
#elif defined(__linux__) && defined(__x86_64__)
inline constexpr const char* behavior_target = "linux-x86_64";
#else
inline constexpr const char* behavior_target = "unknown-target";
#endif

struct Behavior {
  using Bind = bool (*)(Compiler&, const Module&, Diagnostics&);

  constexpr explicit Behavior(Bind function)
      : abi(behavior_abi), size(sizeof(Behavior)),
        module_identity(detail::behavior_module_identity),
        target(behavior_target), bind(function) {}

  std::uint32_t abi;
  std::size_t size;
  const char* module_identity;
  const char* target;
  Bind bind;
};

using BehaviorEntry = const Behavior* (*)();

}  // namespace joggle

#if defined(_WIN32)
#define JOGGLE_BEHAVIOR_EXPORT __declspec(dllexport)
#else
#define JOGGLE_BEHAVIOR_EXPORT __attribute__((visibility("default")))
#endif

#define JOGGLE_EXPORT_BEHAVIOR(bind_function)                               \
  extern "C" JOGGLE_BEHAVIOR_EXPORT const ::joggle::Behavior*              \
  joggle_behavior_v1() {                                                   \
    static const ::joggle::Behavior descriptor{bind_function};             \
    return &descriptor;                                                    \
  }
