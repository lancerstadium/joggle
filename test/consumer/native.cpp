#include <joggle/joggle.h>

namespace {

bool twice(joggle_call* call, void*) {
  joggle_value input{};
  if (call->api->arg_count(call) != 1 ||
      !call->api->arg(call, 0, &input) || input.kind != JOGGLE_I64)
    return call->api->fail(call, "expected one integer");
  joggle_value output{};
  output.kind = JOGGLE_I64;
  output.data.integer = input.data.integer * 2;
  return call->api->ret(call, 0, &output);
}

}  // namespace

JOGGLE_MODULE_EXPORT bool joggle_module(const joggle_api* api,
                                        joggle_module* module) {
  return joggle::compatible(api) &&
         api->bind(module, "probe.twice", twice, nullptr);
}
