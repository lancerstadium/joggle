#include <joggle/joggle.h>

namespace {

bool bind(joggle::Compiler& compiler, const joggle::Module& module,
          joggle::Diagnostics& diagnostics) {
  const auto keep = module.operation("keep");
  const auto lowered = module.operation("lowered");
  const auto lower = module.pass("lower");
  if (!keep || !lowered || !lower) {
    diagnostics.report("external behavior does not match its Module");
    return false;
  }

  compiler.bind(*lower, [keep = *keep, lowered = *lowered](
                            joggle::Graph& graph,
                            joggle::Diagnostics& pass_diagnostics) {
    const auto operations = graph.all_operations();
    auto edit = graph.edit();
    for (const joggle::Operation& operation : operations) {
      if (operation.schema() == keep) {
        edit.replace(operation, lowered);
      }
    }
    return edit.commit(pass_diagnostics);
  });
  return true;
}

}  // namespace

JOGGLE_EXPORT_BEHAVIOR(bind)
