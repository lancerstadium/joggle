#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "joggle/diag.h"

namespace joggle::detail {

enum class TokenKind {
  End,
  Invalid,
  Name,
  Integer,
  Number,
  String,
  LeftBrace,
  RightBrace,
  LeftParen,
  RightParen,
  LeftBracket,
  RightBracket,
  Colon,
  Semicolon,
  Comma,
  At,
  Dot,
  Ellipsis,
  Equal,
  Less,
  Greater,
  Plus,
  Minus,
  Star,
  Slash,
  Arrow,
  FatArrow,
  Caret,
  Dollar,
  Pipe,
  Operator,
};

struct Token {
  TokenKind kind = TokenKind::Invalid;
  std::string text;
  Loc::Pos begin;
  Loc::Pos end;
};

class Lexer {
public:
  explicit Lexer(std::string_view input, Loc::Pos origin = Loc::Pos{});

  Token take();

private:
  Token number(Loc::Pos begin);
  Token string(Loc::Pos begin);
  Token symbol(Loc::Pos begin);
  Token token(TokenKind kind, std::string text, Loc::Pos begin) const;
  Token invalid(std::string text, Loc::Pos begin) const;
  void advance();
  void skip_trivia();

  std::string_view input_;
  std::size_t offset_ = 0;
  std::size_t line_ = 1;
  std::size_t column_ = 1;
};

}  // namespace joggle::detail
