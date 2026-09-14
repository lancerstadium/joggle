#include "joggle/joggle.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace joggle {

namespace {

std::string_view trim(std::string_view text) {
  while (!text.empty() &&
         std::isspace(static_cast<unsigned char>(text.front())))
    text.remove_prefix(1);
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
    text.remove_suffix(1);
  return text;
}

bool valid_atom(std::string_view text) {
  return !text.empty() &&
         text.find_first_of("<>[],()") == std::string_view::npos &&
         std::none_of(text.begin(), text.end(), [](char ch) {
           return std::isspace(static_cast<unsigned char>(ch));
         });
}

std::optional<std::vector<std::string_view>>
split_terms(std::string_view text) {
  std::vector<std::string_view> terms;
  std::size_t start = 0;
  std::vector<char> closes;
  for (std::size_t index = 0; index < text.size(); ++index) {
    switch (text[index]) {
    case '<':
      closes.push_back('>');
      break;
    case '[':
      closes.push_back(']');
      break;
    case '(':
      closes.push_back(')');
      break;
    case '>':
    case ']':
    case ')':
      if (closes.empty() || closes.back() != text[index])
        return std::nullopt;
      closes.pop_back();
      break;
    case ',':
      if (closes.empty()) {
        const std::string_view term = trim(text.substr(start, index - start));
        if (term.empty())
          return std::nullopt;
        terms.push_back(term);
        start = index + 1;
      }
      break;
    default:
      break;
    }
  }
  if (!closes.empty())
    return std::nullopt;
  const std::string_view tail = trim(text.substr(start));
  if (!tail.empty())
    terms.push_back(tail);
  else if (start != 0)
    return std::nullopt;
  return terms;
}

}  // namespace

Attr::Attr(bool value) : data_(value) {}
Attr::Attr(std::int64_t value) : data_(value) {}
Attr::Attr(double value) : data_(value) {}
Attr::Attr(std::string value) : data_(std::move(value)) {}
Attr::Attr(const char* value) : data_(std::string(value)) {}
Attr::Attr(Bytes value) : data_(std::move(value)) {}
Attr::Attr(List value) : data_(std::move(value)) {}
Attr::Attr(Dict value) : data_(std::move(value)) {}

const Attr::Data& Attr::data() const noexcept { return data_; }
bool Attr::empty() const noexcept {
  return std::holds_alternative<std::monostate>(data_);
}
std::optional<bool> Attr::boolean() const noexcept {
  const auto* value = std::get_if<bool>(&data_);
  return value ? std::optional<bool>(*value) : std::nullopt;
}
std::optional<std::int64_t> Attr::integer() const noexcept {
  const auto* value = std::get_if<std::int64_t>(&data_);
  return value ? std::optional<std::int64_t>(*value) : std::nullopt;
}
std::optional<double> Attr::real() const noexcept {
  const auto* value = std::get_if<double>(&data_);
  return value ? std::optional<double>(*value) : std::nullopt;
}
std::optional<std::string_view> Attr::string() const& noexcept {
  const auto* value = std::get_if<std::string>(&data_);
  return value ? std::optional<std::string_view>(*value) : std::nullopt;
}
const Attr::Bytes* Attr::bytes() const& noexcept {
  return std::get_if<Bytes>(&data_);
}
const Attr::List* Attr::list() const& noexcept {
  return std::get_if<List>(&data_);
}
const Attr::Dict* Attr::dict() const& noexcept {
  return std::get_if<Dict>(&data_);
}

Ty::Ty(std::string text) {
  const std::string_view source = trim(text);
  text_ = std::string(source);
  if (source.empty())
    return;
  std::size_t open = std::string_view::npos;
  char close = '\0';
  if (source.front() == '[' && source.back() == ']') {
    name_ = "[]";
    open = 0;
    close = ']';
  } else {
    open = source.find('<');
    if (open != std::string_view::npos && source.back() == '>') {
      name_ = std::string(trim(source.substr(0, open)));
      close = '>';
    } else if (open == std::string_view::npos)
      open = std::string_view::npos;
    else
      return;
  }
  if (open == std::string_view::npos) {
    if (!valid_atom(source)) {
      name_.clear();
      return;
    }
    name_ = std::string(source);
    text_ = name_;
    valid_ = true;
    return;
  }
  if (name_ != "[]" && !valid_atom(name_))
    return;
  const std::size_t first = open + 1;
  const std::size_t count = source.size() - first - 1;
  const auto terms = split_terms(source.substr(first, count));
  if (!terms || (close == '>' && terms->empty())) {
    name_.clear();
    return;
  }
  for (std::string_view term : *terms) {
    Ty arg{std::string(term)};
    if (!arg.valid()) {
      name_.clear();
      args_.clear();
      return;
    }
    args_.push_back(std::move(arg));
  }
  text_ = name_ == "[]" ? "[" : name_ + '<';
  for (std::size_t index = 0; index < args_.size(); ++index) {
    if (index)
      text_ += ", ";
    text_ += args_[index].text();
  }
  text_ += close;
  valid_ = true;
}

Ty::Ty(std::string name, std::span<const Ty> args) {
  const std::string_view constructor = trim(name);
  if ((constructor != "[]" &&
       (!valid_atom(constructor) || args.empty())) ||
      std::any_of(args.begin(), args.end(),
                  [](const Ty& arg) { return !arg.valid(); }))
    return;
  name_ = std::string(constructor);
  args_.assign(args.begin(), args.end());
  text_ = name_ == "[]" ? "[" : name_ + '<';
  for (std::size_t index = 0; index < args_.size(); ++index) {
    if (index)
      text_ += ", ";
    text_ += args_[index].text();
  }
  text_ += name_ == "[]" ? ']' : '>';
  valid_ = true;
}

bool Ty::empty() const noexcept { return text_.empty(); }
bool Ty::valid() const noexcept { return valid_; }
std::string_view Ty::text() const noexcept { return text_; }
std::string_view Ty::name() const noexcept { return name_; }
const std::vector<Ty>& Ty::args() const noexcept { return args_; }

}  // namespace joggle
