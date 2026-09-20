#include "module.h"

#include "diag.h"

#include "joggle/joggle.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

namespace {

namespace fs = std::filesystem;

int usage() {
  std::cerr << "usage:\n"
               "  joggle [--diagnostics text|jog] <command> ...\n"
               "  joggle --version\n"
               "  joggle check <file.jog|-> [-M <module-dir>]...\n"
               "  joggle read <module.fn> <file|-> [-M <module-dir>]...\n"
               "  joggle run <module.fn>... <file.jog|-> "
               "[--arg <Attr>]... [--report <file>] [--timing <file>] "
               "[-M <module-dir>]...\n"
               "  joggle query <module.fn> <file.jog|-> "
               "[--arg <Attr>]... [-M <module-dir>]...\n"
               "  joggle emit <module.fn> <file.jog|-> "
               "[--arg <Attr>]... [-M <module-dir>]...\n"
               "  joggle mod list [-M <module-dir>]...\n"
               "  joggle mod info <name> [-M <module-dir>]...\n"
               "  joggle mod check <name> [-M <module-dir>]...\n"
               "  joggle mod install <directory> <module-dir> "
               "[-M <dependency-dir>]...\n"
               "  joggle mod upgrade <directory> <module-dir> "
               "[-M <dependency-dir>]...\n"
               "  joggle mod uninstall <name> <module-dir>\n";
  return 2;
}

bool options(int argc, char** argv, int first, bool allow_report,
             bool allow_timing, bool allow_args, std::vector<fs::path>& roots,
             std::optional<fs::path>& report,
             std::optional<fs::path>& timing,
             std::vector<std::string>& args) {
  for (int index = first; index < argc; index += 2) {
    if (index + 1 >= argc)
      return false;
    const std::string_view option = argv[index];
    if (option == "-M") {
      roots.emplace_back(argv[index + 1]);
    } else if (allow_report && option == "--report" && !report) {
      report.emplace(argv[index + 1]);
    } else if (allow_timing && option == "--timing" && !timing) {
      timing.emplace(argv[index + 1]);
    } else if (allow_args && option == "--arg") {
      args.emplace_back(argv[index + 1]);
    } else {
      return false;
    }
  }
  return true;
}

joggle::Attr timing_attr(const joggle::RunTiming& timing) {
  const auto ns = [](std::chrono::nanoseconds value) {
    return joggle::Attr(static_cast<std::int64_t>(value.count()));
  };
  joggle::Attr::List steps;
  steps.reserve(timing.steps.size());
  for (const joggle::RunStepTiming& step : timing.steps) {
    joggle::Attr::Dict fields;
    fields["function"] = joggle::Attr(step.function);
    fields["succeeded"] = joggle::Attr(step.succeeded);
    fields["verification_cached"] =
        joggle::Attr(step.verification_cached);
    fields["counters_enabled"] = joggle::Attr(step.counters_enabled);
    fields["before"] = joggle::Attr(static_cast<std::int64_t>(step.before));
    fields["after"] = joggle::Attr(static_cast<std::int64_t>(step.after));
    fields["evaluated_ops"] =
        joggle::Attr(static_cast<std::int64_t>(step.evaluated_ops));
    fields["frame_lookups"] =
        joggle::Attr(static_cast<std::int64_t>(step.frame_lookups));
    fields["frame_probes"] =
        joggle::Attr(static_cast<std::int64_t>(step.frame_probes));
    fields["frame_writes"] =
        joggle::Attr(static_cast<std::int64_t>(step.frame_writes));
    fields["frame_pool_hits"] =
        joggle::Attr(static_cast<std::int64_t>(step.frame_pool_hits));
    fields["frame_pool_misses"] =
        joggle::Attr(static_cast<std::int64_t>(step.frame_pool_misses));
    fields["frame_growths"] =
        joggle::Attr(static_cast<std::int64_t>(step.frame_growths));
    fields["frame_peak_capacity"] =
        joggle::Attr(static_cast<std::int64_t>(step.frame_peak_capacity));
    fields["plan_compiles"] =
        joggle::Attr(static_cast<std::int64_t>(step.plan_compiles));
    fields["plan_hits"] =
        joggle::Attr(static_cast<std::int64_t>(step.plan_hits));
    fields["plan_fallbacks"] =
        joggle::Attr(static_cast<std::int64_t>(step.plan_fallbacks));
    fields["plan_persistent_hits"] =
        joggle::Attr(static_cast<std::int64_t>(step.plan_persistent_hits));
    fields["plan_cache_resets"] =
        joggle::Attr(static_cast<std::int64_t>(step.plan_cache_resets));
    fields["plan_window_hits"] =
        joggle::Attr(static_cast<std::int64_t>(step.plan_window_hits));
    fields["plan_window_misses"] =
        joggle::Attr(static_cast<std::int64_t>(step.plan_window_misses));
    fields["plan_branches"] =
        joggle::Attr(static_cast<std::int64_t>(step.plan_branches));
    fields["plan_loops"] =
        joggle::Attr(static_cast<std::int64_t>(step.plan_loops));
    fields["plan_loop_iterations"] =
        joggle::Attr(static_cast<std::int64_t>(step.plan_loop_iterations));
    fields["plan_returns"] =
        joggle::Attr(static_cast<std::int64_t>(step.plan_returns));
    fields["plan_yields"] =
        joggle::Attr(static_cast<std::int64_t>(step.plan_yields));
    fields["plan_direct_yields"] =
        joggle::Attr(static_cast<std::int64_t>(step.plan_direct_yields));
    fields["plan_direct_block_entries"] = joggle::Attr(
        static_cast<std::int64_t>(step.plan_direct_block_entries));
    fields["plan_call_argument_vectors"] = joggle::Attr(
        static_cast<std::int64_t>(step.plan_call_argument_vectors));
    fields["plan_call_argument_items"] = joggle::Attr(
        static_cast<std::int64_t>(step.plan_call_argument_items));
    fields["plan_call_argument_arity_0"] = joggle::Attr(
        static_cast<std::int64_t>(step.plan_call_argument_arity_0));
    fields["plan_call_argument_arity_1"] = joggle::Attr(
        static_cast<std::int64_t>(step.plan_call_argument_arity_1));
    fields["plan_call_argument_arity_2"] = joggle::Attr(
        static_cast<std::int64_t>(step.plan_call_argument_arity_2));
    fields["plan_call_argument_arity_many"] = joggle::Attr(
        static_cast<std::int64_t>(step.plan_call_argument_arity_many));
    fields["plan_call_argument_materializations"] = joggle::Attr(
        static_cast<std::int64_t>(step.plan_call_argument_materializations));
    fields["plan_call_result_vectors"] = joggle::Attr(
        static_cast<std::int64_t>(step.plan_call_result_vectors));
    fields["plan_call_result_items"] = joggle::Attr(
        static_cast<std::int64_t>(step.plan_call_result_items));
    fields["plan_call_direct_results"] = joggle::Attr(
        static_cast<std::int64_t>(step.plan_call_direct_results));
    fields["plan_call_passthroughs"] = joggle::Attr(
        static_cast<std::int64_t>(step.plan_call_passthroughs));
    fields["plan_call_lists"] =
        joggle::Attr(static_cast<std::int64_t>(step.plan_call_lists));
    fields["plan_call_intrinsics"] =
        joggle::Attr(static_cast<std::int64_t>(step.plan_call_intrinsics));
    fields["plan_call_operators"] =
        joggle::Attr(static_cast<std::int64_t>(step.plan_call_operators));
    fields["plan_call_fundamentals"] = joggle::Attr(
        static_cast<std::int64_t>(step.plan_call_fundamentals));
    fields["plan_call_invocations"] = joggle::Attr(
        static_cast<std::int64_t>(step.plan_call_invocations));
    fields["plan_direct_operator_links"] = joggle::Attr(
        static_cast<std::int64_t>(step.plan_direct_operator_links));
    fields["dispatch_hits"] =
        joggle::Attr(static_cast<std::int64_t>(step.dispatch_hits));
    fields["dispatch_misses"] =
        joggle::Attr(static_cast<std::int64_t>(step.dispatch_misses));
    fields["plan_dispatch_hits"] =
        joggle::Attr(static_cast<std::int64_t>(step.plan_dispatch_hits));
    fields["plan_dispatch_misses"] =
        joggle::Attr(static_cast<std::int64_t>(step.plan_dispatch_misses));
    joggle::Attr::Dict functions;
    for (const auto& [name, function] : step.functions) {
      joggle::Attr::Dict counters;
      counters["invocations"] = joggle::Attr(
          static_cast<std::int64_t>(function.invocations));
      counters["memo_hits"] =
          joggle::Attr(static_cast<std::int64_t>(function.memo_hits));
      counters["plan_evaluated_ops"] = joggle::Attr(
          static_cast<std::int64_t>(function.plan_evaluated_ops));
      counters["plan_loop_iterations"] = joggle::Attr(
          static_cast<std::int64_t>(function.plan_loop_iterations));
      functions.emplace(name, joggle::Attr(std::move(counters)));
    }
    fields["functions"] = joggle::Attr(std::move(functions));
    fields["resolve_ns"] = ns(step.resolve);
    fields["evaluation_ns"] = ns(step.evaluation);
    fields["verification_ns"] = ns(step.verification);
    fields["total_ns"] = ns(step.total);
    steps.emplace_back(std::move(fields));
  }
  joggle::Attr::Dict fields;
  fields["succeeded"] = joggle::Attr(timing.succeeded);
  fields["initial_verification_cached"] =
      joggle::Attr(timing.initial_verification_cached);
  fields["structural_snapshot"] = joggle::Attr(timing.structural_snapshot);
  fields["snapshot_ns"] = ns(timing.snapshot);
  fields["initial_verification_ns"] = ns(timing.initial_verification);
  fields["steps"] = joggle::Attr(std::move(steps));
  return joggle::Attr(std::move(fields));
}

bool write_timing(const fs::path& path, const joggle::RunTiming& timing,
                  joggle::tool::DiagFormat format) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (output && (output << joggle::print(timing_attr(timing)) << '\n'))
    return true;
  joggle::tool::print_error(stderr, "cannot write timing " + path.string(),
                            format);
  return false;
}

bool load_uses(joggle::Env& env, const joggle::Mod& mod,
               joggle::tool::DiagFormat format) {
  for (const std::string& dependency : mod.uses()) {
    if (!env.load(dependency)) {
      joggle::tool::print_diags(stderr, env.diags(), format);
      return false;
    }
  }
  return true;
}

bool input(std::string_view file, bool binary, std::string& contents,
           joggle::tool::DiagFormat format) {
  std::ifstream stream;
  std::istream* source = &std::cin;
  if (file != "-") {
    stream.open(std::string(file), std::ios::binary);
    if (!stream) {
      joggle::tool::print_error(
          stderr, "cannot open " + std::string(file), format);
      return false;
    }
    source = &stream;
  }
#if defined(_WIN32)
  else if (binary && _setmode(_fileno(stdin), _O_BINARY) == -1) {
    joggle::tool::print_error(
        stderr, "cannot set standard input to binary mode", format);
    return false;
  }
#else
  (void)binary;
#endif
  std::ostringstream buffer;
  buffer << source->rdbuf();
  if (source->bad()) {
    joggle::tool::print_error(
        stderr,
        "cannot read " +
            (file == "-" ? std::string("standard input") : std::string(file)),
        format);
    return false;
  }
  contents = std::move(buffer).str();
  return true;
}

int process(int argc, char** argv, joggle::tool::DiagFormat format) {
  if (argc < 3)
    return usage();

  const std::string command = argv[1];
  const bool execute = command == "run";
  const bool decode = command == "read";
  const bool inspect = command == "query";
  const bool emit = command == "emit";
  if (command != "check" && !execute && !decode && !inspect && !emit)
    return usage();
  if ((!execute && !decode && !inspect && !emit && argc < 3) ||
      ((execute || decode || inspect || emit) && argc < 4))
    return usage();

  const bool selects_function = execute || decode || inspect || emit;
  std::vector<std::string> functions;
  std::string file;
  int first_option = 0;
  if (execute) {
    int positional_end = 2;
    while (positional_end < argc &&
           std::string_view(argv[positional_end]) != "-M" &&
           std::string_view(argv[positional_end]) != "--report" &&
           std::string_view(argv[positional_end]) != "--timing" &&
           std::string_view(argv[positional_end]) != "--arg")
      ++positional_end;
    if (positional_end < 4)
      return usage();
    file = argv[positional_end - 1];
    for (int index = 2; index + 1 < positional_end; ++index)
      functions.emplace_back(argv[index]);
    first_option = positional_end;
  } else if (selects_function) {
    functions.emplace_back(argv[2]);
    file = argv[3];
    first_option = 4;
  } else {
    file = argv[2];
    first_option = 3;
  }
  std::vector<fs::path> roots;
  std::optional<fs::path> report_file;
  std::optional<fs::path> timing_file;
  std::vector<std::string> argument_sources;
  if (!options(argc, argv, first_option, execute, execute,
               execute || inspect || emit, roots, report_file, timing_file,
               argument_sources))
    return usage();

  std::string source;
  if (!input(file, decode, source, format))
    return 1;
  const std::string source_name = file == "-" ? "<stdin>" : file;

  joggle::Env env;
  for (const fs::path& root : roots)
    env.path(root.string());

  std::vector<joggle::Attr> arguments;
  arguments.reserve(argument_sources.size());
  for (const std::string& text : argument_sources) {
    joggle::Attr argument;
    if (!joggle::parse(env, text, argument, "<argument>")) {
      joggle::tool::print_diags(stderr, env.diags(), format);
      return 1;
    }
    arguments.push_back(std::move(argument));
  }
  if (selects_function) {
    for (const std::string& function : functions) {
      const std::size_t dot = function.rfind('.');
      if (dot == std::string::npos || !env.load(function.substr(0, dot))) {
        joggle::tool::print_diags(stderr, env.diags(), format);
        return 1;
      }
    }
  }

  const std::string& function = functions.empty() ? file : functions.front();

  if (decode) {
    joggle::Attr::Bytes bytes;
    bytes.reserve(source.size());
    for (const unsigned char byte : source)
      bytes.push_back(byte);
    const std::vector<joggle::Attr> args{joggle::Attr(std::move(bytes))};
    std::vector<joggle::Attr> returns;
    if (!env.call(function, args, returns)) {
      joggle::tool::print_diags(stderr, env.diags(), format);
      return 1;
    }
    if (returns.size() != 1 || !returns.front().string()) {
      joggle::tool::print_error(
          stderr, "read function must return one str", format);
      return 1;
    }
    source = std::string(*returns.front().string());
  }

  joggle::Mod mod;
  if (!joggle::parse(env, source, mod, source_name))
    return joggle::tool::print_diags(stderr, mod.diags(), format);
  if (!load_uses(env, mod, format))
    return 1;
  if (!mod.verify(env))
    return joggle::tool::print_diags(stderr, mod.diags(), format);
  if (inspect || emit) {
    joggle::Attr result;
    if (!joggle::query(env, function, mod, result, arguments)) {
      joggle::tool::print_diags(stderr, env.diags(), format);
      return 1;
    }
    if (inspect) {
      if (!joggle::print(stdout, result) || std::fputc('\n', stdout) == EOF)
        return 1;
    } else if (const auto value = result.string()) {
      if (std::fwrite(value->data(), 1, value->size(), stdout) != value->size())
        return 1;
    } else if (const auto* value = result.bytes()) {
#if defined(_WIN32)
      if (_setmode(_fileno(stdout), _O_BINARY) == -1) {
        joggle::tool::print_error(
            stderr, "cannot set standard output to binary mode", format);
        return 1;
      }
#endif
      if (std::fwrite(value->data(), 1, value->size(), stdout) != value->size())
        return 1;
    } else {
      joggle::tool::print_error(
          stderr, "emit function must return str or bytes", format);
      return 1;
    }
    return 0;
  }
  joggle::Attr report;
  joggle::RunTiming timing;
  if (execute) {
    bool ran = false;
    if (functions.size() == 1) {
      ran = timing_file
                ? joggle::run(env, function, mod, report, arguments, timing)
                : report_file
                      ? joggle::run(env, function, mod, report, arguments)
                      : joggle::run(env, function, mod, arguments);
    } else {
      std::vector<std::string_view> names;
      names.reserve(functions.size());
      for (const std::string& name : functions)
        names.emplace_back(name);
      ran = timing_file
                ? joggle::run(env, names, mod, report, arguments, timing)
                : report_file
                      ? joggle::run(env, names, mod, report, arguments)
                      : joggle::run(env, names, mod, arguments);
    }
    if (!ran) {
      if (timing_file && !write_timing(*timing_file, timing, format))
        return 1;
      std::vector<joggle::Diag> diagnostics(mod.diags().begin(),
                                            mod.diags().end());
      diagnostics.insert(diagnostics.end(), env.diags().begin(),
                         env.diags().end());
      joggle::tool::print_diags(stderr, diagnostics, format);
      return 1;
    }
  }
  if (report_file) {
    std::ofstream output(*report_file, std::ios::binary | std::ios::trunc);
    if (!output || !(output << joggle::print(report) << '\n')) {
      joggle::tool::print_error(
          stderr, "cannot write report " + report_file->string(), format);
      return 1;
    }
  }
  if (timing_file) {
    if (!write_timing(*timing_file, timing, format))
      return 1;
  }
  return joggle::print(stdout, mod) ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  joggle::tool::DiagFormat format = joggle::tool::DiagFormat::text;
  if (argc >= 2 && std::string_view(argv[1]) == "--diagnostics") {
    if (argc < 3 || !joggle::tool::parse_diag_format(argv[2], format)) {
      joggle::tool::print_error(
          stderr, "diagnostics format must be 'text' or 'jog'",
          joggle::tool::DiagFormat::text);
      return usage();
    }
    argc -= 2;
    argv += 2;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--version") {
    std::cout << "joggle " JOGGLE_VERSION "\n";
    return 0;
  }
  if (argc >= 2 && std::string_view(argv[1]) == "mod") {
    const int result = joggle::tool::module(argc, argv, format);
    return result == 2 ? usage() : result;
  }
  return process(argc, argv, format);
}
