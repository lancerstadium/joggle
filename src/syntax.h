#ifndef JOGGLE_SYNTAX_H
#define JOGGLE_SYNTAX_H

#include "joggle/joggle.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace joggle::syntax {

enum class Tk : std::uint8_t { end, name, number, string, symbol };

struct Token {
  Tk kind = Tk::end;
  std::string text;
  Loc loc;
};

std::vector<Token> lex(std::span<const Source> sources);
std::vector<Token> lex(std::string_view source, std::string_view file);

}  // namespace joggle::syntax

#endif
