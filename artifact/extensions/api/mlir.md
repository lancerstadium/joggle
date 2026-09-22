# MLIR analysis extension interface

Edit the supplied C++ implementation. Its public entry point is:

```cpp
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinOps.h"
#include "llvm/Support/JSON.h"

llvm::json::Object analyze(mlir::ModuleOp module) {
  return {};
}
```

The test driver parses a builtin module with a `study.request` dictionary attribute.
Read that native IR attribute with:

```cpp
auto request = module->getAttrOfType<mlir::DictionaryAttr>("study.request");
auto items = llvm::cast<mlir::ArrayAttr>(request.get("items"));
auto count = items.size();
auto first = llvm::cast<mlir::IntegerAttr>(items[0]).getInt();
```

Arrays are iterable and indexable. Integer attributes in this interface use
`i64`. Standard C++ containers, control flow, and arithmetic are available.
Construct JSON objects and arrays using LLVM's support library:

```cpp
llvm::json::Array values;
values.push_back(first);
return llvm::json::Object{{"count", int64_t(count)}, {"values", std::move(values)}};
```

The driver links the extension with MLIRParser, MLIRIR, and MLIRSupport, then
calls `analyze` and compares its JSON result to the task's expected result.
Return semantic rejection as the error dictionary specified by the task,
rather than aborting the process. Do not change the driver or fixtures.
