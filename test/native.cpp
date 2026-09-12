#include "joggle/joggle.h"

namespace {

bool ping(joggle_call* call, void*) {
  joggle_value input{};
  if (call->api->arg_count(call) != 1 || !call->api->arg(call, 0, &input) ||
      input.kind != JOGGLE_I64)
    return call->api->fail(call, "expected one integer");
  joggle_value output{};
  output.kind = JOGGLE_I64;
  output.data.integer = input.data.integer + 1;
  return call->api->ret(call, 0, &output);
}

bool echo(joggle_call* call, void*) {
  joggle_value value{};
  if (call->api->arg_count(call) != 1 || !call->api->arg(call, 0, &value) ||
      (value.kind != JOGGLE_BYTES && value.kind != JOGGLE_STR))
    return call->api->fail(call, "expected one string or byte string");
  return call->api->ret(call, 0, &value);
}

}  // namespace

JOGGLE_MODULE_EXPORT bool joggle_module(const joggle_api* api,
                                        joggle_module* module) {
  return joggle::compatible(api) &&
         api->bind(module, "sample.ping", ping, nullptr) &&
         api->bind(module, "sample.echo", echo, nullptr);
}
