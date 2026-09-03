#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "joggle/diagnostic.h"
#include "joggle/ir.h"

namespace joggle::ir {

// A named set of executable Functions. Vocabulary and package declarations
// remain in joggle::Module; this value is the transformable IR artifact that
// may flow through the installable `ir.module` type. Copies share immutable
// Functions and detach a Function on first mutable access.
class Module {
public:
  Module();
  Module(const Module& other);
  Module& operator=(const Module& other);
  Module(Module&&) noexcept;
  Module& operator=(Module&&) noexcept;
  ~Module();

  bool insert(std::string name, Function function, Diagnostics& diagnostics);
  Function* function(std::string_view name);
  const Function* function(std::string_view name) const;
  std::vector<std::string> function_names() const;
  std::size_t size() const;
  bool empty() const;

private:
  struct Storage;
  std::unique_ptr<Storage> storage_;
};

}  // namespace joggle::ir
