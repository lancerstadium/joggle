#include "joggle/ir_module.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <utility>

namespace joggle::ir {

struct Module::Storage {
  std::map<std::string, std::shared_ptr<Function>, std::less<>> functions;
};

namespace {

bool valid_name(std::string_view name) {
  if (name.empty() ||
      (std::isalpha(static_cast<unsigned char>(name.front())) == 0 &&
       name.front() != '_')) {
    return false;
  }
  return std::all_of(name.begin() + 1, name.end(), [](char character) {
    return std::isalnum(static_cast<unsigned char>(character)) != 0 ||
           character == '_';
  });
}

}  // namespace

Module::Module() : storage_(std::make_unique<Storage>()) {}

Module::Module(const Module& other)
    : storage_(std::make_unique<Storage>(*other.storage_)) {}

Module& Module::operator=(const Module& other) {
  if (this != &other) {
    Module copy(other);
    storage_.swap(copy.storage_);
  }
  return *this;
}

Module::Module(Module&&) noexcept = default;
Module& Module::operator=(Module&&) noexcept = default;
Module::~Module() = default;

bool Module::insert(std::string name, Function function,
                    Diagnostics& diagnostics) {
  if (!valid_name(name)) {
    diagnostics.report("IR Module function name '" + name + "' is invalid");
    return false;
  }
  if (storage_->functions.contains(name)) {
    diagnostics.report("IR Module already contains function '" + name + "'");
    return false;
  }
  storage_->functions.emplace(
      std::move(name), std::make_shared<Function>(std::move(function)));
  return true;
}

Function* Module::function(std::string_view name) {
  const auto found = storage_->functions.find(name);
  if (found == storage_->functions.end()) {
    return nullptr;
  }
  if (found->second.use_count() != 1) {
    found->second = std::make_shared<Function>(found->second->clone());
  }
  return found->second.get();
}

const Function* Module::function(std::string_view name) const {
  const auto found = storage_->functions.find(name);
  return found == storage_->functions.end() ? nullptr : found->second.get();
}

std::vector<std::string> Module::function_names() const {
  std::vector<std::string> result;
  result.reserve(storage_->functions.size());
  for (const auto& [name, unused] : storage_->functions) {
    static_cast<void>(unused);
    result.push_back(name);
  }
  return result;
}

std::size_t Module::size() const { return storage_->functions.size(); }

bool Module::empty() const { return storage_->functions.empty(); }

}  // namespace joggle::ir
