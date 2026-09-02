#include <joggle/joggle.h>

namespace {

bool bind(joggle::Compiler& compiler, const joggle::Module& module,
          joggle::Diagnostics& diagnostics) {
  const auto keep = module.function("keep");
  const auto converted = module.function("converted");
  const auto convert = module.function("convert");
  if (!keep || !converted || !convert) {
    diagnostics.report("external behavior does not match its Module");
    return false;
  }

  compiler.bind(*convert, [keep = *keep, converted = *converted](
                            joggle::Graph& graph,
                            joggle::Diagnostics& pass_diagnostics) {
    const auto operations = graph.all_operations();
    auto edit = graph.edit();
    for (const joggle::Operation& operation : operations) {
      if (operation.schema() == keep) {
        edit.replace(operation, converted);
      }
    }
    return edit.commit(pass_diagnostics);
  });
  return true;
}

}  // namespace

JOGGLE_EXPORT_BEHAVIOR(bind)
