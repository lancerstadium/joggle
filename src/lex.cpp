#include "syntax.h"

#include <cctype>
#include <iterator>
#include <string_view>
#include <utility>

namespace joggle::syntax {

namespace {

class Lexer {
public:
  Lexer(std::string_view source, std::string_view file)
      : source_(source), file_(file) {}

  std::vector<Token> run() {
    std::vector<Token> out;
    for (;;) {
      skip_space();
      const Loc begin{file_, line_, column_};
      if (at_end()) {
        out.push_back({Tk::end, {}, begin});
        return out;
      }
      const char ch = peek();
      if (std::isalpha(static_cast<unsigned char>(ch)) || ch == '_') {
        std::string text;
        while (!at_end()) {
          const char next = peek();
          if (std::isalnum(static_cast<unsigned char>(next)) || next == '_')
            text.push_back(take());
          else if (next == '.' && peek(1) != '.')
            text.push_back(take());
          else
            break;
        }
        out.push_back({Tk::name, std::move(text), begin});
        continue;
      }
      if (std::isdigit(static_cast<unsigned char>(ch))) {
        std::string text;
        bool dot = false;
        while (!at_end()) {
          const char next = peek();
          if (std::isdigit(static_cast<unsigned char>(next)))
            text.push_back(take());
          else if (next == '.' && peek(1) != '.' && !dot) {
            dot = true;
            text.push_back(take());
          } else
            break;
        }
        if ((peek() == 'e' || peek() == 'E') &&
            (std::isdigit(static_cast<unsigned char>(peek(1))) ||
             ((peek(1) == '+' || peek(1) == '-') &&
              std::isdigit(static_cast<unsigned char>(peek(2)))))) {
          text.push_back(take());
          if (peek() == '+' || peek() == '-')
            text.push_back(take());
          while (std::isdigit(static_cast<unsigned char>(peek())))
            text.push_back(take());
        }
        out.push_back({Tk::number, std::move(text), begin});
        continue;
      }
      if (ch == '"') {
        take();
        std::string text;
        while (!at_end() && peek() != '"') {
          if (peek() == '\\') {
            take();
            if (at_end())
              break;
            const char escaped = take();
            text.push_back(escaped == 'n' ? '\n' : escaped);
          } else
            text.push_back(take());
        }
        if (!at_end())
          take();
        out.push_back({Tk::string, std::move(text), begin});
        continue;
      }

      std::string text(1, take());
      const std::string pair = text + std::string(1, peek());
      if (pair == "->" || pair == ".." || pair == "+=" || pair == "-=" ||
          pair == "*=" || pair == "/=" || pair == "%=" || pair == "|=" ||
          pair == "^=" || pair == "&=" || pair == "==" || pair == "!=" ||
          pair == "<=" || pair == ">=" || pair == "&&" || pair == "||" ||
          pair == "<<" || pair == ">>")
        text.push_back(take());
      if ((text == "<<" || text == ">>") && peek() == '=')
        text.push_back(take());
      out.push_back({Tk::symbol, std::move(text), begin});
    }
  }

private:
  bool at_end() const { return offset_ >= source_.size(); }
  char peek(std::size_t ahead = 0) const {
    const std::size_t at = offset_ + ahead;
    return at < source_.size() ? source_[at] : '\0';
  }
  char take() {
    const char ch = source_[offset_++];
    if (ch == '\n') {
      ++line_;
      column_ = 1;
    } else
      ++column_;
    return ch;
  }
  void skip_space() {
    for (;;) {
      while (!at_end() && std::isspace(static_cast<unsigned char>(peek())))
        take();
      if (peek() != '/' || peek(1) != '/')
        return;
      while (!at_end() && take() != '\n') {
      }
    }
  }

  std::string_view source_;
  std::string file_;
  std::size_t offset_ = 0;
  std::size_t line_ = 1;
  std::size_t column_ = 1;
};

}  // namespace

std::vector<Token> lex(std::string_view source, std::string_view file) {
  return Lexer(source, file).run();
}

std::vector<Token> lex(std::span<const Source> sources) {
  if (sources.empty())
    return Lexer({}, {}).run();

  std::vector<Token> out;
  for (std::size_t index = 0; index < sources.size(); ++index) {
    std::vector<Token> tokens =
        Lexer(sources[index].text, sources[index].file).run();
    if (index + 1 != sources.size())
      tokens.pop_back();
    out.insert(out.end(), std::make_move_iterator(tokens.begin()),
               std::make_move_iterator(tokens.end()));
  }
  return out;
}

}  // namespace joggle::syntax
