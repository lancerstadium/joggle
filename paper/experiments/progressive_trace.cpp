#include "joggle/joggle.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

namespace {

bool read(std::string_view path, std::string& text) {
  std::ifstream input(std::string(path), std::ios::binary);
  if (!input)
    return false;
  std::ostringstream buffer;
  buffer << input.rdbuf();
  text = buffer.str();
  return input.good() || input.eof();
}

joggle::Op named_call(joggle::Fn fn, std::string_view name) {
  for (joggle::Op op : fn.ops()) {
    if (op.kind() != joggle::Op::Kind::call)
      continue;
    for (joggle::Val out : op.outs())
      if (out.name() == name)
        return op;
  }
  return {};
}

std::size_t calls(joggle::Fn fn, std::string_view callee) {
  std::size_t count = 0;
  for (joggle::Op op : fn.ops())
    if (op.kind() == joggle::Op::Kind::call && op.callee() == callee)
      ++count;
  return count;
}

std::size_t loops(joggle::Fn fn) {
  std::size_t count = 0;
  for (joggle::Op op : fn.ops())
    if (op.kind() == joggle::Op::Kind::loop)
      ++count;
  return count;
}

void fail(const joggle::Env& env, const joggle::Mod& mod,
          std::string_view stage) {
  std::fprintf(stderr, "stage=%.*s\n", static_cast<int>(stage.size()),
               stage.data());
  for (const auto& diag : env.diags())
    std::fprintf(stderr, "env: %s\n", diag.message.c_str());
  for (const auto& diag : mod.diags())
    std::fprintf(stderr, "mod: %s\n", diag.message.c_str());
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 5) {
    std::fprintf(stderr,
                 "usage: %s <model.jog> <examples> <modules> <build/modules>\n",
                 argv[0]);
    return 2;
  }
  std::string text;
  if (!read(argv[1], text)) {
    std::fprintf(stderr, "cannot read %s\n", argv[1]);
    return 2;
  }

  joggle::Env env;
  for (int i = 2; i < argc; ++i)
    env.path(argv[i]);
  if (!env.load("edge") || !env.load("c")) {
    joggle::Mod empty;
    fail(env, empty, "load modules");
    return 1;
  }

  joggle::Mod model;
  if (!joggle::parse(env, text, model, argv[1])) {
    fail(env, model, "parse model");
    return 1;
  }
  for (const std::string& name : model.uses())
    if (!env.load(name)) {
      fail(env, model, "load model dependency");
      return 1;
    }
  if (!model.verify(env)) {
    fail(env, model, "verify input");
    return 1;
  }

  const joggle::Fn original_main = model.find_fn("main");
  const joggle::Op original_conv = named_call(
      original_main, "mobilenetv20_features_conv0_fwd");
  const joggle::Op original_relu = named_call(
      original_main, "mobilenetv20_features_relu0_fwd");
  if (!original_main || !original_conv || !original_relu) {
    fail(env, model, "find traced calls");
    return 1;
  }
  const joggle::Val original_conv_out = original_conv.outs().front();
  const joggle::Val original_relu_out = original_relu.outs().front();
  const std::uint64_t initial_revision = model.revision();
  const std::size_t initial_semantic_convs = calls(original_main, "nn.conv2d");
  const std::size_t initial_semantic_relus = calls(original_main, "nn.relu");
  const std::size_t initial_loops = loops(original_main);

  if (!joggle::run(env, "edge.apply", model) || !model.verify(env)) {
    fail(env, model, "select external implementations");
    return 1;
  }
  const joggle::Fn selected_main = model.find_fn("main");
  const std::uint64_t selected_revision = model.revision();
  const joggle::Op selected_conv = named_call(
      selected_main, "mobilenetv20_features_conv0_fwd");
  const std::string selected_conv_callee(selected_conv.callee());
  const std::size_t selected_external_convs = calls(selected_main, "conv2d");
  const std::size_t selected_semantic_relus = calls(selected_main, "nn.relu");
  const std::size_t selected_loops = loops(selected_main);
  const bool main_survived_selection = original_main == selected_main;
  const bool conv_handle_after_selection = original_conv.valid();
  const bool conv_value_after_selection = original_conv_out.valid();
  const bool relu_handle_after_selection = original_relu.valid();
  const bool relu_value_after_selection = original_relu_out.valid();

  if (!joggle::run(env, "c.prepare", model) || !model.verify(env)) {
    fail(env, model, "prepare selected program");
    return 1;
  }
  const joggle::Fn prepared_main = model.find_fn("main");
  std::printf(
      "input_bytes=%zu revisions=%llu,%llu,%llu "
      "main_same_after_select=%s main_same_after_prepare=%s "
      "selected_conv_callee=%s semantic_convs_before=%zu "
      "external_convs_selected=%zu external_convs_prepared=%zu "
      "semantic_relus_before=%zu semantic_relus_selected=%zu semantic_relus_prepared=%zu "
      "loops_before=%zu loops_selected=%zu loops_prepared=%zu "
      "conv_op_live_after_select=%s conv_val_live_after_select=%s "
      "relu_op_live_after_select=%s relu_val_live_after_select=%s "
      "conv_op_live_after_prepare=%s conv_val_live_after_prepare=%s "
      "relu_op_live_after_prepare=%s relu_val_live_after_prepare=%s\n",
      text.size(), static_cast<unsigned long long>(initial_revision),
      static_cast<unsigned long long>(selected_revision),
      static_cast<unsigned long long>(model.revision()),
      main_survived_selection ? "true" : "false",
      original_main == prepared_main ? "true" : "false",
      selected_conv_callee.c_str(), initial_semantic_convs,
      selected_external_convs, calls(prepared_main, "conv2d"),
      initial_semantic_relus,
      selected_semantic_relus, calls(prepared_main, "nn.relu"),
      initial_loops, selected_loops, loops(prepared_main),
      conv_handle_after_selection ? "true" : "false",
      conv_value_after_selection ? "true" : "false",
      relu_handle_after_selection ? "true" : "false",
      relu_value_after_selection ? "true" : "false",
      original_conv.valid() ? "true" : "false",
      original_conv_out.valid() ? "true" : "false",
      original_relu.valid() ? "true" : "false",
      original_relu_out.valid() ? "true" : "false");
  return original_main == prepared_main && selected_external_convs > 0 &&
                 selected_semantic_relus > 0 &&
                 calls(prepared_main, "conv2d") > 0 &&
                 calls(prepared_main, "nn.relu") == 0 &&
                 loops(prepared_main) > 0
             ? 0
             : 1;
}
