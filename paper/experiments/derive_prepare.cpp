#include "joggle/joggle.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

namespace {

using Clock = std::chrono::steady_clock;

double seconds(Clock::time_point start) {
  return std::chrono::duration<double>(Clock::now() - start).count();
}

bool read(std::string_view path, std::string& text) {
  std::ifstream input(std::string(path), std::ios::binary);
  if (!input)
    return false;
  std::ostringstream buffer;
  buffer << input.rdbuf();
  text = buffer.str();
  return input.good() || input.eof();
}

void fail(std::string_view stage, const joggle::Env& env,
          const joggle::Mod* mod = nullptr) {
  std::fprintf(stderr, "stage=%.*s\n", static_cast<int>(stage.size()),
               stage.data());
  for (const auto& diag : env.diags())
    std::fprintf(stderr, "env: %s\n", diag.message.c_str());
  if (mod)
    for (const auto& diag : mod->diags())
      std::fprintf(stderr, "mod: %s\n", diag.message.c_str());
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 4) {
    std::fprintf(stderr, "usage: %s <source.jog> <modules> <build/modules>\n",
                 argv[0]);
    return 2;
  }

  std::string text;
  if (!read(argv[1], text)) {
    std::fprintf(stderr, "cannot read %s\n", argv[1]);
    return 2;
  }

  joggle::Env env;
  env.path(argv[2]);
  env.path(argv[3]);
  if (!env.load("c")) {
    fail("load c", env);
    return 1;
  }

  joggle::Fn source;
  for (joggle::Fn candidate : env.find_fns("c.prepare"))
    if (candidate.params().size() == 1 &&
        candidate.params().front().type() == joggle::Ty("Mod"))
      source = candidate;
  if (!source) {
    std::fprintf(stderr, "stage=resolve c.prepare\n");
    return 1;
  }

  joggle::Mod code;
  if (!joggle::parse(env, "module derive\nuse c\n", code,
                     "derive.jog")) {
    fail("parse compiler module", env, &code);
    return 1;
  }
  const auto derivation_start = Clock::now();
  const joggle::Fn derived = code.clone(env, source, "derived_prepare");
  if (!derived || !code.verify(env)) {
    fail("clone compiler procedure", env, &code);
    return 1;
  }
  const double derivation_seconds = seconds(derivation_start);
  const std::size_t fn_count = code.fns().size();
  const std::size_t op_count = code.ops().size();
  const std::uint64_t revision_before = code.revision();
  const std::string code_before = joggle::print(code);

  std::size_t expose_calls = 0;
  bool edited = false;
  for (joggle::Op op : code.find_fn("derived_prepare_prepare_with").ops()) {
    if (op.kind() != joggle::Op::Kind::call ||
        op.callee() != "opt.expose")
      continue;
    if (++expose_calls == 2)
      edited = code.replace(op, joggle::Attr(false));
  }
  if (expose_calls != 2 || !edited || !code.verify(env)) {
    fail("edit compiler procedure", env, &code);
    return 1;
  }
  const std::uint64_t revision_after = code.revision();

  joggle::Mod original, target;
  if (!joggle::parse(env, text, original, argv[1])) {
    fail("parse original model", env, &original);
    return 1;
  }
  if (!joggle::parse(env, text, target, argv[1])) {
    fail("parse derived model", env, &target);
    return 1;
  }
  for (const std::string& name : original.uses())
    if (!env.load(name)) {
      fail("load model dependency", env, &original);
      return 1;
    }
  const std::uint64_t original_initial_revision = original.revision();
  const std::uint64_t target_initial_revision = target.revision();

  const auto original_start = Clock::now();
  if (!joggle::run(env, "c.prepare", original)) {
    fail("run original c.prepare", env, &original);
    return 1;
  }
  const double original_seconds = seconds(original_start);
  std::fprintf(stderr, "original c.prepare completed in %.3f s\n",
               original_seconds);

  joggle::Attr report;
  const auto target_start = Clock::now();
  if (!joggle::run(env, derived, target, report)) {
    fail("run derived c.prepare", env, &target);
    return 1;
  }
  const double derived_seconds = seconds(target_start);
  std::fprintf(stderr, "derived c.prepare completed in %.3f s\n",
               derived_seconds);

  const std::string original_ir = joggle::print(original);
  const std::string target_ir = joggle::print(target);
  const bool same_ir = target_ir == original_ir;
  const bool original_valid = original.verify(env);
  const bool derived_valid = target.verify(env);
  joggle::Attr original_source, derived_source;
  const bool sources_exist =
      joggle::query(env, "c.source", original, original_source) &&
      joggle::query(env, "c.source", target, derived_source);
  if (!sources_exist)
    fail("generate C source", env, &target);
  const bool same_source = sources_exist && original_source == derived_source;
  const bool code_edited = joggle::print(code) != code_before;
  std::printf("input_bytes=%zu compiler_fns=%zu compiler_ops=%zu "
              "compiler_revisions=%llu->%llu derivation_seconds=%.3f "
              "original_seconds=%.3f derived_seconds=%.3f "
              "original_model_revisions=%llu->%llu "
              "derived_model_revisions=%llu->%llu "
              "original_ir_bytes=%zu derived_ir_bytes=%zu "
              "same_ir=%s same_c_source=%s original_valid=%s "
              "derived_valid=%s compiler_code_edited=%s\n",
              text.size(), fn_count, op_count,
              static_cast<unsigned long long>(revision_before),
              static_cast<unsigned long long>(revision_after),
              derivation_seconds, original_seconds, derived_seconds,
              static_cast<unsigned long long>(original_initial_revision),
              static_cast<unsigned long long>(original.revision()),
              static_cast<unsigned long long>(target_initial_revision),
              static_cast<unsigned long long>(target.revision()),
              original_ir.size(), target_ir.size(),
              same_ir ? "true" : "false",
              same_source ? "true" : "false",
              original_valid ? "true" : "false",
              derived_valid ? "true" : "false",
              code_edited ? "true" : "false");
  if (!original_valid || !derived_valid)
    fail("verify prepared model", env, !derived_valid ? &target : &original);
  return same_ir && same_source && original_valid && derived_valid &&
                 code_edited
             ? 0
             : 1;
}
