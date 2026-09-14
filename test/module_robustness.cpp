#include "joggle/joggle.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
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

  // Exercise a dependency graph that is deep and repeatedly rejoins. Every
  // module exports the same `value` spelling, while qualified calls and the
  // single transitive generic must remain deterministic across all diamonds.
  constexpr int graph_size = 48;
  for (int index = 0; index < graph_size; ++index) {
    const std::string name = "graph.m" + std::to_string(index);
    CHECK(fs::create_directories(root / name, error) && !error);
    std::string source = "module " + name + "\n";
    if (index == 0) {
      source += "fn identity<T: Ty>(x: T) -> T { return x }\n";
      source += "fn value(x: i32) -> i32 { return x }\n";
    } else {
      const int direct = index - 1;
      const int shortcut = index / 2;
      source += "use graph.m" + std::to_string(direct) + "\n";
      if (shortcut != direct)
        source += "use graph.m" + std::to_string(shortcut) + "\n";
      source += "fn value(x: i32) -> i32 { return graph.m" +
                std::to_string(direct) + ".value(identity(x)) }\n";
    }
    CHECK(write(root / name / "module.jog", source));
  }

  joggle::Env graph;
  graph.path(root.string());
  CHECK(graph.load("graph.m47"));
  CHECK(graph.modules().size() == static_cast<std::size_t>(graph_size));
  joggle::Mod consumer;
  CHECK(joggle::parse(
      graph,
      "module graph.consumer\nuse graph.m47\n"
      "fn run(x: i32) -> i32 { return graph.m47.value(identity(x)) }\n",
      consumer, "consumer.jog"));
  CHECK(consumer.verify(graph));

  // A failed closure must not publish any prefix or poison a later retry.
  CHECK(fs::create_directories(root / "graph.fail_leaf", error) && !error);
  CHECK(fs::create_directories(root / "graph.fail_top", error) && !error);
  CHECK(write(root / "graph.fail_leaf" / "module.jog",
              "module graph.fail_leaf\nuse graph.missing\n"
              "fn leaf(x: i32) -> i32 { return x }\n"));
  CHECK(write(root / "graph.fail_top" / "module.jog",
              "module graph.fail_top\nuse graph.fail_leaf\n"
              "fn top(x: i32) -> i32 { return leaf(x) }\n"));
  joggle::Env retry;
  retry.path(root.string());
  CHECK(!retry.load("graph.fail_top"));
  CHECK(!retry.loaded("graph.fail_top"));
  CHECK(!retry.loaded("graph.fail_leaf"));
  CHECK(!retry.loaded("graph.missing"));
  CHECK(fs::create_directories(root / "graph.missing", error) && !error);
  CHECK(write(root / "graph.missing" / "module.jog",
              "module graph.missing\n"
              "fn missing(x: i32) -> i32 { return x }\n"));
  retry.clear_diags();
  CHECK(retry.load("graph.fail_top"));
  CHECK(retry.loaded("graph.fail_top"));
  CHECK(retry.loaded("graph.fail_leaf"));
  CHECK(retry.loaded("graph.missing"));

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
