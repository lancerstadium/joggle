#include <cstdlib>
#include <string_view>

#include <joggle/digest.h>

int main() {
  const bool empty = joggle::sha256("") == "e3b0c44298fc1c149afbf4c8996fb924"
                                           "27ae41e4649b934ca495991b7852b855";
  const bool abc = joggle::sha256("abc") == "ba7816bf8f01cfea414140de5dae2223"
                                            "b00361a396177a9cb410ff61f20015ad";
  const bool binary = joggle::sha256(std::string_view{"a\0b", 3}) ==
                      "59b271ae1bbcb1d31d41929817f4b16f"
                      "b439eb4f31520b5ad1d5ce98920a7138";
  return empty && abc && binary ? EXIT_SUCCESS : EXIT_FAILURE;
}
