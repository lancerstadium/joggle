#include <cstdlib>
#include <iostream>
#include <string>

#include <joggle/joggle.h>

int main(int argc, char** argv) {
  if (argc != 4 && argc != 5) {
    return EXIT_FAILURE;
  }
  joggle::Compiler compiler;
  compiler.search(argv[1]);
  compiler.lock(argv[2]);
  compiler.load(argv[3]);
  if (!compiler.link()) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }
  const std::size_t expected =
      argc == 5 ? static_cast<std::size_t>(std::stoul(argv[4])) : 2U;
  return compiler.mods().size() == expected ? EXIT_SUCCESS : EXIT_FAILURE;
}
