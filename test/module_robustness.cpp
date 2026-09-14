#include "joggle/joggle.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string_view>

#define CHECK(expression)                                                      \
  do {                                                                         \
    if (!(expression)) {                                                       \
      std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__,    \
                   #expression);                                               \
      return 1;                                                                \
    }                                                                          \
  } while (false)

namespace {

namespace fs = std::filesystem;

struct Cleanup {
  fs::path root;

  ~Cleanup() {
    std::error_code ignored;
    fs::remove_all(root, ignored);
  }
};

bool write(const fs::path& path, std::string_view source) {
  std::ofstream output(path);
  return output && output.write(source.data(),
                                static_cast<std::streamsize>(source.size()));
}

bool rejects(const fs::path& root, std::string_view name,
             std::string_view message) {
  joggle::Env env;
  env.path(root.string());
  if (env.load(name) || env.loaded(name) || env.diags().empty())
    return false;
  return env.diags().back().message.find(message) != std::string::npos;
}

}  // namespace

int main(int argc, char** argv) {
  CHECK(argc == 2);
  const fs::path root = fs::absolute(argv[1]).lexically_normal();
  Cleanup cleanup{root};
  std::error_code error;
  fs::remove_all(root, error);
  error.clear();
  CHECK(fs::create_directories(root / "plain", error) && !error);
  CHECK(write(root / "plain" / "module.jog",
              "module plain\nfn value() -> i32 { return 1 }\n"));

  joggle::Env plain;
  plain.path(root.string());
  CHECK(plain.load("plain"));

#if !defined(_WIN32)
  CHECK(fs::create_directories(root / "source_loop", error) && !error);
  fs::create_symlink("module.jog", root / "source_loop" / "module.jog",
                     error);
  CHECK(!error);
  CHECK(rejects(root, "source_loop", "cannot inspect module source"));

  CHECK(fs::create_directories(root / "fragments_loop", error) && !error);
  CHECK(write(root / "fragments_loop" / "module.jog",
              "module fragments_loop\n"));
  fs::create_directory_symlink("lib", root / "fragments_loop" / "lib",
                               error);
  CHECK(!error);
  CHECK(rejects(root, "fragments_loop", "cannot inspect module fragments"));

  CHECK(fs::create_directories(root / "native_loop", error) && !error);
  CHECK(write(root / "native_loop" / "module.jog", "module native_loop\n"));
  fs::create_directory_symlink("native", root / "native_loop" / "native",
                               error);
  CHECK(!error);
  CHECK(rejects(root, "native_loop",
                "cannot inspect native module directory"));
#endif

  return 0;
}
