#include "joggle/joggle.h"

namespace {

bool ping(jog_call_v1* call, void*) {
  jog_value_v1 input{};
  if (call->api->arg_count(call) != 1 || !call->api->arg(call, 0, &input) ||
      input.kind != JOG_I64_V1)
    return call->api->fail(call, "expected one integer");
  jog_value_v1 output{};
  output.kind = JOG_I64_V1;
  output.data.integer = input.data.integer + 1;
  return call->api->ret(call, 0, &output);
}

}  // namespace

JOGGLE_MODULE_EXPORT bool joggle_module_v1(const jog_api_v1* api,
                                           jog_module_v1* module) {
  return api && api->abi_version == joggle::module_abi_version &&
         api->bind(module, "sample.ping", ping, nullptr);
}
