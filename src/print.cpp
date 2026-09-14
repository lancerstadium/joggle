#include "print.h"

#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>

namespace joggle::detail {

std::string attr_text(const Attr& value) {
  if (value.empty())
    return "nil";
  if (const auto item = value.boolean())
    return *item ? "true" : "false";
  if (const auto item = value.integer())
    return std::to_string(*item);
  if (const auto item = value.real()) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<double>::max_digits10)
        << *item;
    std::string text = out.str();
    if (text.find_first_of(".eE") == std::string::npos)
      text += ".0";
    return text;
  }
  if (const auto item = value.string()) {
    std::string out = "\"";
    for (const char ch : *item) {
      if (ch == '"' || ch == '\\')
        out.push_back('\\');
      if (ch == '\n')
        out += "\\n";
      else
        out.push_back(ch);
    }
    return out + '"';
  }
  if (const auto* item = value.bytes()) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out = "hex\"";
    out.reserve(item->size() * 2 + 5);
    for (const std::uint8_t byte : *item) {
      out.push_back(digits[byte >> 4]);
      out.push_back(digits[byte & 15]);
    }
    return out + '"';
  }
  if (const auto* item = value.list()) {
    std::string out = "[";
    for (std::size_t index = 0; index < item->size(); ++index) {
      if (index)
        out += ", ";
      out += attr_text((*item)[index]);
    }
    return out + ']';
  }
  if (const auto* item = value.dict()) {
    std::string out = "{";
    std::size_t index = 0;
    for (const auto& [name, entry] : *item) {
      if (index++)
        out += ", ";
      out += attr_text(Attr(name)) + ": " + attr_text(entry);
    }
    return out + '}';
  }
  return "nil";
}

}  // namespace joggle::detail
