#include "syntax_lexer.h"

#include <cctype>
#include <utility>

namespace joggle::detail {

Lexer::Lexer(std::string_view input, SourcePosition origin)
    : input_(input), line_(origin.line), column_(origin.column) {}

Token Lexer::take() {
  skip_trivia();
  const SourcePosition begin{line_, column_};
  if (offset_ == input_.size()) {
    return {TokenKind::End, {}, begin, begin};
  }

  const char first = input_[offset_];
  if (std::isalpha(static_cast<unsigned char>(first)) != 0 || first == '_') {
    std::string text;
    while (offset_ < input_.size()) {
      const char current = input_[offset_];
      if (std::isalnum(static_cast<unsigned char>(current)) == 0 &&
          current != '_') {
        break;
      }
      text.push_back(current);
      advance();
    }
    return {TokenKind::Name, std::move(text), begin, {line_, column_}};
  }
  if (std::isdigit(static_cast<unsigned char>(first)) != 0 ||
      (first == '-' && offset_ + 1U < input_.size() &&
       std::isdigit(static_cast<unsigned char>(input_[offset_ + 1U])) != 0)) {
    return number(begin);
  }
  if (first == '"') {
    return string(begin);
  }

  advance();
  switch (first) {
  case '{':
    return token(TokenKind::LeftBrace, "{", begin);
  case '}':
    return token(TokenKind::RightBrace, "}", begin);
  case '(':
    return token(TokenKind::LeftParen, "(", begin);
  case ')':
    return token(TokenKind::RightParen, ")", begin);
  case '[':
    return token(TokenKind::LeftBracket, "[", begin);
  case ']':
    return token(TokenKind::RightBracket, "]", begin);
  case ':':
    return token(TokenKind::Colon, ":", begin);
  case ';':
    return token(TokenKind::Semicolon, ";", begin);
  case ',':
    return token(TokenKind::Comma, ",", begin);
  case '@':
    return token(TokenKind::At, "@", begin);
  case '<':
    return token(TokenKind::Less, "<", begin);
  case '>':
    return token(TokenKind::Greater, ">", begin);
  case '^':
    return token(TokenKind::Caret, "^", begin);
  case '%':
    return token(TokenKind::Percent, "%", begin);
  case '$':
    return token(TokenKind::Dollar, "$", begin);
  case '=':
    if (offset_ < input_.size() && input_[offset_] == '>') {
      advance();
      return token(TokenKind::FatArrow, "=>", begin);
    }
    return token(TokenKind::Equal, "=", begin);
  case '-':
    if (offset_ < input_.size() && input_[offset_] == '>') {
      advance();
      return token(TokenKind::Arrow, "->", begin);
    }
    return token(TokenKind::Minus, "-", begin);
  case '.':
    if (offset_ + 1U < input_.size() && input_[offset_] == '.' &&
        input_[offset_ + 1U] == '.') {
      advance();
      advance();
      return token(TokenKind::Ellipsis, "...", begin);
    }
    return token(TokenKind::Dot, ".", begin);
  default:
    return invalid(std::string("unexpected character '") + first + "'", begin);
  }
}

Token Lexer::number(SourcePosition begin) {
  std::string text;
  bool non_integer = false;
  if (input_[offset_] == '-') {
    non_integer = true;
    text.push_back('-');
    advance();
  }
  while (offset_ < input_.size() &&
         std::isdigit(static_cast<unsigned char>(input_[offset_])) != 0) {
    text.push_back(input_[offset_]);
    advance();
  }
  if (offset_ + 1U < input_.size() && input_[offset_] == '.' &&
      std::isdigit(static_cast<unsigned char>(input_[offset_ + 1U])) != 0) {
    non_integer = true;
    text.push_back('.');
    advance();
    while (offset_ < input_.size() &&
           std::isdigit(static_cast<unsigned char>(input_[offset_])) != 0) {
      text.push_back(input_[offset_]);
      advance();
    }
  }
  if (offset_ < input_.size() &&
      (input_[offset_] == 'e' || input_[offset_] == 'E')) {
    non_integer = true;
    text.push_back(input_[offset_]);
    advance();
    if (offset_ < input_.size() &&
        (input_[offset_] == '+' || input_[offset_] == '-')) {
      text.push_back(input_[offset_]);
      advance();
    }
    while (offset_ < input_.size() &&
           std::isdigit(static_cast<unsigned char>(input_[offset_])) != 0) {
      text.push_back(input_[offset_]);
      advance();
    }
  }
  return {non_integer ? TokenKind::Number : TokenKind::Integer,
          std::move(text),
          begin,
          {line_, column_}};
}

Token Lexer::string(SourcePosition begin) {
  advance();
  std::string text;
  while (offset_ < input_.size() && input_[offset_] != '"') {
    char current = input_[offset_];
    advance();
    if (current != '\\') {
      text.push_back(current);
      continue;
    }
    if (offset_ == input_.size()) {
      return invalid("unterminated string escape", begin);
    }
    current = input_[offset_];
    advance();
    switch (current) {
    case '\\':
    case '"':
      text.push_back(current);
      break;
    case 'n':
      text.push_back('\n');
      break;
    case 'r':
      text.push_back('\r');
      break;
    case 't':
      text.push_back('\t');
      break;
    default:
      return invalid("unknown string escape", begin);
    }
  }
  if (offset_ == input_.size()) {
    return invalid("unterminated string literal", begin);
  }
  advance();
  return {TokenKind::String, std::move(text), begin, {line_, column_}};
}

Token Lexer::token(TokenKind kind, std::string text,
                   SourcePosition begin) const {
  return {kind, std::move(text), begin, {line_, column_}};
}

Token Lexer::invalid(std::string text, SourcePosition begin) const {
  return {TokenKind::Invalid, std::move(text), begin, {line_, column_}};
}

void Lexer::advance() {
  if (input_[offset_] == '\n') {
    ++line_;
    column_ = 1;
  } else {
    ++column_;
  }
  ++offset_;
}

void Lexer::skip_trivia() {
  while (offset_ < input_.size()) {
    if (std::isspace(static_cast<unsigned char>(input_[offset_])) != 0) {
      advance();
      continue;
    }
    if (input_[offset_] == '#') {
      while (offset_ < input_.size() && input_[offset_] != '\n') {
        advance();
      }
      continue;
    }
    if (input_[offset_] == '/' && offset_ + 1U < input_.size() &&
        input_[offset_ + 1U] == '/') {
      advance();
      advance();
      while (offset_ < input_.size() && input_[offset_] != '\n') {
        advance();
      }
      continue;
    }
    return;
  }
}

}  // namespace joggle::detail
