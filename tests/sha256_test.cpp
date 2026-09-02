#include <cstdlib>

#include "sha256.h"

int main() {
  const bool empty = joggle::detail::sha256("") ==
                     "e3b0c44298fc1c149afbf4c8996fb924"
                     "27ae41e4649b934ca495991b7852b855";
  const bool abc = joggle::detail::sha256("abc") ==
                   "ba7816bf8f01cfea414140de5dae2223"
                   "b00361a396177a9cb410ff61f20015ad";
  return empty && abc ? EXIT_SUCCESS : EXIT_FAILURE;
}
