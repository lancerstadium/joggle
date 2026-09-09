#include "joggle/joggle.h"

namespace {

bool ping(jog_call* call, void*) {
  jog_value input{};
  if (call->api->arg_count(call) != 1 || !call->api->arg(call, 0, &input) ||
      input.kind != JOG_I64)
    return call->api->fail(call, "expected one integer");
  jog_value output{};
  output.kind = JOG_I64;
  output.data.integer = input.data.integer + 1;
  return call->api->ret(call, 0, &output);
}

bool echo(jog_call* call, void*) {
  jog_value value{};
  if (call->api->arg_count(call) != 1 || !call->api->arg(call, 0, &value) ||
      (value.kind != JOG_BYTES && value.kind != JOG_STR))
    return call->api->fail(call, "expected one string or byte string");
  return call->api->ret(call, 0, &value);
}

}  // namespace

JOGGLE_MODULE_EXPORT bool joggle_module(const jog_api* api,
                                        jog_module* module) {
  return joggle::compatible(api) &&
         api->bind(module, "sample.ping", ping, nullptr) &&
         api->bind(module, "sample.echo", echo, nullptr);
}
