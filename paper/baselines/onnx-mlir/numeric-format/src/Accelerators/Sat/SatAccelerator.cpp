/* SPDX-License-Identifier: Apache-2.0 */

#include "src/Accelerators/Sat/SatAccelerator.hpp"
#include "src/Accelerators/Sat/SatDialect.hpp"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

#include "src/Compiler/CompilerOptions.hpp"
#include "src/Compiler/CompilerPasses.hpp"

using namespace mlir;

namespace onnx_mlir {
namespace {

llvm::cl::opt<std::string> formatMap("sat-format-map",
    llvm::cl::desc("JSON map assigning sat widths to ONNX node names"),
    llvm::cl::value_desc("filename"));
llvm::cl::opt<bool> reportSat("sat-report",
    llvm::cl::desc("Report sat type materialization and lowering"),
    llvm::cl::init(false));

FailureOr<llvm::StringMap<unsigned>> readMap() {
  if (formatMap.empty()) {
    llvm::errs() << "sat: --sat-format-map is required\n";
    return failure();
  }
  auto buffer = llvm::MemoryBuffer::getFile(formatMap);
  if (!buffer) {
    llvm::errs() << "sat: cannot read format map: " << formatMap << '\n';
    return failure();
  }
  auto parsed = llvm::json::parse(buffer.get()->getBuffer());
  if (!parsed) {
    llvm::errs() << "sat: invalid JSON format map\n";
    return failure();
  }
  llvm::json::Object *root = parsed->getAsObject();
  llvm::json::Array *operations = root ? root->getArray("operations") : nullptr;
  if (!operations) {
    llvm::errs() << "sat: format map has no operations array\n";
    return failure();
  }
  llvm::StringMap<unsigned> result;
  for (llvm::json::Value &value : *operations) {
    llvm::json::Object *entry = value.getAsObject();
    auto name = entry ? entry->getString("node") : std::nullopt;
    auto width = entry ? entry->getInteger("width") : std::nullopt;
    if (!name || !width || *width < 2 || *width > 63 ||
        !result.try_emplace(*name, static_cast<unsigned>(*width)).second) {
      llvm::errs() << "sat: invalid or duplicate operation mapping\n";
      return failure();
    }
  }
  return result;
}

Type withElement(Type type, Type element) {
  if (auto ranked = dyn_cast<RankedTensorType>(type))
    return RankedTensorType::get(ranked.getShape(), element);
  if (isa<UnrankedTensorType>(type))
    return UnrankedTensorType::get(element);
  return {};
}

std::string helperName(unsigned width, RankedTensorType carrier) {
  std::string name = "sat_add_" + std::to_string(width) + "_";
  if (carrier.getRank() == 0)
    return name + "scalar";
  llvm::raw_string_ostream stream(name);
  llvm::interleave(carrier.getShape(), stream, "x");
  return name;
}

FailureOr<func::FuncOp> getOrCreateHelper(
    ModuleOp module, Location loc, unsigned width, RankedTensorType carrier) {
  std::string name = helperName(width, carrier);
  auto functionType = FunctionType::get(
      module.getContext(), TypeRange{carrier, carrier}, TypeRange{carrier});
  if (Operation *symbol = module.lookupSymbol(name)) {
    auto function = dyn_cast<func::FuncOp>(symbol);
    auto storedWidth = symbol->getAttrOfType<IntegerAttr>("sat.width");
    if (!function || function.getFunctionType() != functionType ||
        !storedWidth || storedWidth.getInt() != width) {
      symbol->emitError("collides with generated sat helper '") << name << "'";
      return failure();
    }
    return function;
  }

  OpBuilder builder(module.getContext());
  builder.setInsertionPointToEnd(module.getBody());
  auto function = func::FuncOp::create(builder, loc, name, functionType);
  function.setPrivate();
  function->setAttr("sat.generated", builder.getUnitAttr());
  function->setAttr("sat.width", builder.getI64IntegerAttr(width));
  Block *entry = function.addEntryBlock();
  builder.setInsertionPointToStart(entry);
  auto create = [&](StringRef operationName, ValueRange operands,
                    ArrayRef<NamedAttribute> attributes = {}) {
    OperationState state(loc, operationName);
    state.addOperands(operands);
    state.addTypes(carrier);
    state.addAttributes(attributes);
    return builder.create(state);
  };
  auto constant = [&](int64_t value) {
    auto dense = DenseElementsAttr::get(
        carrier, APInt(64, static_cast<uint64_t>(value), true));
    NamedAttribute attribute(builder.getStringAttr("value"), dense);
    return create("onnx.Constant", {}, {attribute})->getResult(0);
  };
  int64_t low = -(int64_t{1} << (width - 1));
  int64_t high = (int64_t{1} << (width - 1)) - 1;
  Value sum = create("onnx.Add", entry->getArguments())->getResult(0);
  Value clippedLow =
      create("onnx.Max", ValueRange{sum, constant(low)})->getResult(0);
  Value clipped =
      create("onnx.Min", ValueRange{clippedLow, constant(high)})->getResult(0);
  func::ReturnOp::create(builder, loc, clipped);
  return function;
}

struct MaterializePass final
    : PassWrapper<MaterializePass, OperationPass<ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(MaterializePass)

  StringRef getArgument() const final { return "sat-materialize"; }
  StringRef getDescription() const final {
    return "Materialize mapped ONNX additions with parametric sat types";
  }

  void runOnOperation() final {
    auto mapping = readMap();
    if (failed(mapping)) {
      signalPassFailure();
      return;
    }
    SmallVector<Operation *> additions;
    getOperation().walk([&](Operation *operation) {
      if (operation->getName().getStringRef() == "onnx.Add")
        additions.push_back(operation);
    });
    llvm::StringMap<bool> matched;
    for (Operation *addition : additions) {
      auto name = addition->getAttrOfType<StringAttr>("onnx_node_name");
      auto found = name ? mapping->find(name.getValue()) : mapping->end();
      if (found == mapping->end())
        continue;
      if (addition->getNumOperands() != 2 || addition->getNumResults() != 1) {
        addition->emitError("sat mapping requires binary one-result Add");
        signalPassFailure();
        return;
      }
      Type carrier = addition->getResult(0).getType();
      auto shaped = dyn_cast<ShapedType>(carrier);
      if (!shaped || !shaped.getElementType().isInteger(64)) {
        addition->emitError("sat mapping requires an int64 tensor carrier");
        signalPassFailure();
        return;
      }
      Type element = sat::IntType::get(&getContext(), found->second);
      Type encoded = withElement(carrier, element);
      OpBuilder builder(addition);
      SmallVector<Value> inputs;
      for (Value operand : addition->getOperands()) {
        auto cast = UnrealizedConversionCastOp::create(
            builder, addition->getLoc(), TypeRange{encoded}, ValueRange{operand});
        inputs.push_back(cast.getResult(0));
      }
      OperationState state(addition->getLoc(), "sat.add");
      state.addOperands(inputs);
      state.addTypes(encoded);
      Operation *satAdd = builder.create(state);
      auto decoded = UnrealizedConversionCastOp::create(
          builder, addition->getLoc(), TypeRange{carrier}, satAdd->getResults());
      addition->getResult(0).replaceAllUsesWith(decoded.getResult(0));
      addition->erase();
      matched[found->first()] = true;
      if (reportSat)
        llvm::outs() << "sat-materialize node=" << found->first()
                     << " type=!sat.int<" << found->second << ">\n";
    }
    for (const auto &entry : *mapping) {
      if (!matched.contains(entry.getKey())) {
        getOperation().emitError("sat mapping names no matching ONNX Add: ")
            << entry.getKey();
        signalPassFailure();
        return;
      }
    }
  }
};

struct LowerPass final : PassWrapper<LowerPass, OperationPass<ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(LowerPass)

  StringRef getArgument() const final { return "sat-lower"; }
  StringRef getDescription() const final {
    return "Lower parametric sat additions to carrier ONNX operations";
  }

  void runOnOperation() final {
    SmallVector<Operation *> additions;
    getOperation().walk([&](Operation *operation) {
      if (operation->getName().getStringRef() == "sat.add")
        additions.push_back(operation);
    });
    for (Operation *addition : additions) {
      if (addition->getNumOperands() != 2 || addition->getNumResults() != 1) {
        addition->emitError("sat.add requires two operands and one result");
        signalPassFailure();
        return;
      }
      auto resultType = dyn_cast<ShapedType>(addition->getResult(0).getType());
      auto integer = resultType
                         ? dyn_cast<sat::IntType>(resultType.getElementType())
                         : sat::IntType{};
      if (!integer) {
        addition->emitError("sat.add requires tensor<...x!sat.int<W>>");
        signalPassFailure();
        return;
      }
      SmallVector<Value> carriers;
      for (Value operand : addition->getOperands()) {
        auto cast = operand.getDefiningOp<UnrealizedConversionCastOp>();
        if (!cast || cast.getInputs().size() != 1) {
          addition->emitError("sat.add operand is missing its carrier cast");
          signalPassFailure();
          return;
        }
        auto operandType = dyn_cast<ShapedType>(operand.getType());
        auto operandInteger = operandType
                                  ? dyn_cast<sat::IntType>(
                                        operandType.getElementType())
                                  : sat::IntType{};
        if (!operandInteger || operandInteger.getWidth() != integer.getWidth()) {
          addition->emitError("sat.add operand widths must match its result");
          signalPassFailure();
          return;
        }
        carriers.push_back(cast.getInputs()[0]);
      }
      if (!addition->getResult(0).hasOneUse()) {
        addition->emitError("sat.add result requires one carrier cast");
        signalPassFailure();
        return;
      }
      auto decoded = dyn_cast<UnrealizedConversionCastOp>(
          *addition->getResult(0).getUsers().begin());
      if (!decoded || decoded.getOutputs().size() != 1) {
        addition->emitError("sat.add result is missing its carrier cast");
        signalPassFailure();
        return;
      }
      Type carrierType = decoded.getResult(0).getType();
      auto carrierShape = dyn_cast<ShapedType>(carrierType);
      if (!carrierShape || !carrierShape.hasStaticShape() ||
          !carrierShape.getElementType().isInteger(64)) {
        addition->emitError("sat.add lowering requires a static int64 carrier");
        signalPassFailure();
        return;
      }
      int64_t low = -(int64_t{1} << (integer.getWidth() - 1));
      int64_t high = (int64_t{1} << (integer.getWidth() - 1)) - 1;
      auto rankedCarrier = dyn_cast<RankedTensorType>(carrierType);
      auto helper = rankedCarrier ? getOrCreateHelper(getOperation(),
                                      addition->getLoc(), integer.getWidth(),
                                      rankedCarrier)
                                  : FailureOr<func::FuncOp>(failure());
      if (failed(helper)) {
        if (!rankedCarrier)
          addition->emitError("sat.add lowering requires a ranked carrier");
        signalPassFailure();
        return;
      }
      OpBuilder builder(addition);
      auto call = func::CallOp::create(builder, addition->getLoc(),
          helper->getSymName(), TypeRange{carrierType}, carriers);
      decoded.getResult(0).replaceAllUsesWith(call.getResult(0));
      decoded.erase();
      SmallVector<Operation *> casts;
      for (Value operand : addition->getOperands())
        casts.push_back(operand.getDefiningOp());
      addition->erase();
      for (Operation *cast : casts)
        if (cast->use_empty())
          cast->erase();
      if (reportSat)
        llvm::outs() << "sat-lower width=" << integer.getWidth()
                     << " low=" << low << " high=" << high << '\n';
    }
  }
};

} // namespace

namespace accel {

Accelerator *createSat() { return SatAccelerator::getInstance(); }

SatAccelerator *SatAccelerator::getInstance() {
  static SatAccelerator instance;
  return &instance;
}

SatAccelerator::SatAccelerator() : Accelerator(Accelerator::Kind::Sat) {
  acceleratorTargets.push_back(this);
  addCompilerConfig(CCM_SHARED_LIB_DEPS, {"RuntimeSat"}, true);
}

uint64_t SatAccelerator::getVersionNumber() const { return 0x000100; }

void SatAccelerator::addPasses(mlir::OwningOpRef<mlir::ModuleOp> &module,
    mlir::PassManager &manager,
    onnx_mlir::EmissionTargetType &emissionTarget,
    std::string outputNameNoExt) const {
  manager.addPass(std::make_unique<MaterializePass>());
  manager.addPass(std::make_unique<LowerPass>());
  onnx_mlir::addPasses(module, manager, emissionTarget, outputNameNoExt);
}

void SatAccelerator::registerDialects(mlir::DialectRegistry &registry) const {
  registry.insert<sat::SatDialect>();
}

void SatAccelerator::registerPasses(int) const {
  mlir::registerPass([]() -> std::unique_ptr<mlir::Pass> {
    return std::make_unique<MaterializePass>();
  });
  mlir::registerPass([]() -> std::unique_ptr<mlir::Pass> {
    return std::make_unique<LowerPass>();
  });
}

void SatAccelerator::configurePasses() const {}
mlir::MemRefType SatAccelerator::convertTensorTypeToMemRefType(
    mlir::TensorType) const { return nullptr; }
void SatAccelerator::conversionTargetONNXToKrnl(
    mlir::ConversionTarget &) const {}
void SatAccelerator::rewritePatternONNXToKrnl(mlir::RewritePatternSet &,
    mlir::TypeConverter &, mlir::MLIRContext *) const {}
void SatAccelerator::conversionTargetKrnlToLLVM(
    mlir::ConversionTarget &) const {}
void SatAccelerator::rewritePatternKrnlToLLVM(mlir::RewritePatternSet &,
    mlir::LLVMTypeConverter &, mlir::MLIRContext *) const {}

} // namespace accel
} // namespace onnx_mlir
