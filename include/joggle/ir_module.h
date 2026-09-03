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
// flows through Prelude's builtin `program` type. Copies share immutable
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

struct Dependency {
  std::string name;
  Version version;

  bool operator==(const Dependency&) const = default;
};

// Exact schema packages referenced by every Function in the executable
// Module. Prelude is included here even though canonical source links it
// implicitly.
std::vector<Dependency> dependencies(const Module& module);

}  // namespace joggle::ir

namespace joggle {

// Serializes an executable Module as one ordinary, canonical `.joggle`
// package. No second artifact syntax or Graph declaration is introduced.
std::string format(const ir::Module& module, std::string_view name,
                   Version version);

}  // namespace joggle
