#include <joggle/joggle.h>

namespace {

bool twice(jog_call* call, void*) {
  jog_value input{};
  if (call->api->arg_count(call) != 1 ||
      !call->api->arg(call, 0, &input) || input.kind != JOG_I64)
    return call->api->fail(call, "expected one integer");
  jog_value output{};
  output.kind = JOG_I64;
  output.data.integer = input.data.integer * 2;
  return call->api->ret(call, 0, &output);
}

}  // namespace

JOGGLE_MODULE_EXPORT bool joggle_module(const jog_api* api,
                                        jog_module* module) {
  return joggle::compatible(api) &&
         api->bind(module, "probe.twice", twice, nullptr);
}
