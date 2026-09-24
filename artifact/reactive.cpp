#include "joggle/joggle.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#ifndef JOGGLE_ARTIFACT_PERSISTENT_PLANS
#define JOGGLE_ARTIFACT_PERSISTENT_PLANS 1
#endif

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::array<std::string_view, 5> stages{
    "artifact.reactive.analyze", "artifact.reactive.canonicalize",
    "artifact.reactive.select", "artifact.reactive.plan",
    "artifact.reactive.prepare"};

constexpr std::array<std::string_view, 5> full_stages{
    "artifact.reactive.full_analyze",
    "artifact.reactive.full_canonicalize",
    "artifact.reactive.full_select", "artifact.reactive.full_plan",
    "artifact.reactive.full_prepare"};

constexpr std::array<std::string_view, 5> whole_stages{
    "artifact.reactive.whole_analyze",
    "artifact.reactive.whole_canonicalize",
    "artifact.reactive.whole_select", "artifact.reactive.whole_plan",
    "artifact.reactive.whole_prepare"};

struct Config {
  std::string modules;
  std::string output;
  std::string revision;
  std::string input;
  std::string subject_hash;
  std::string policy = "all";
  std::string edit_class = "operation_metadata";
  std::string scope = "all";
  std::string site = "late";
  std::size_t total_nodes = 1000;
  std::size_t affected_nodes = 8;
  std::size_t fanout = 1;
  std::size_t stage_count = stages.size();
  std::size_t warmups = 10;
  std::size_t iterations = 100;
  std::uint64_t seed = 1;
};

struct Metrics {
  std::int64_t select_ns = 0;
  std::int64_t evaluate_ns = 0;
  std::int64_t verify_ns = 0;
  std::int64_t evaluated_ops = 0;
  std::int64_t plan_compiles = 0;
  std::int64_t plan_hits = 0;
  std::int64_t executed_stages = 0;
  std::int64_t reused_stages = 0;
  std::int64_t observed_ops = 0;
  std::int64_t observed_values = 0;
  std::int64_t changed_functions = 0;
  std::string miss = "none";
};

const joggle::Attr& field(const joggle::Attr& value, std::string_view key) {
  static const joggle::Attr missing;
  const auto* fields = value.dict();
  if (!fields)
    return missing;
  const auto found = fields->find(key);
  return found == fields->end() ? missing : found->second;
}

std::int64_t integer(const joggle::Attr& value, std::string_view key) {
  return field(value, key).integer().value_or(0);
}

std::string_view string(const joggle::Attr& value, std::string_view key) {
  return field(value, key).string().value_or(std::string_view{});
}

std::string csv(std::string_view value) {
  if (value.find_first_of(",\"\r\n") == std::string_view::npos)
    return std::string(value);
  std::string out = "\"";
  for (const char ch : value) {
    out += ch;
    if (ch == '"')
      out += '"';
  }
  out += '"';
  return out;
}

std::uint64_t fnv1a(std::string_view value) {
  std::uint64_t hash = UINT64_C(14695981039346656037);
  for (const unsigned char byte : value) {
    hash ^= byte;
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

std::string digest(std::string_view value) {
  std::ostringstream out;
  out << std::hex << std::setfill('0') << std::setw(16) << fnv1a(value);
  return out.str();
}

std::optional<std::size_t> size_value(std::string_view text) {
  try {
    std::size_t end = 0;
    const auto value = std::stoull(std::string(text), &end);
    if (end != text.size())
      return std::nullopt;
    return static_cast<std::size_t>(value);
  } catch (...) {
    return std::nullopt;
  }
}

int usage() {
  std::cerr
      << "usage: joggle-artifact-reactive --modules DIR --output FILE "
         "--revision GIT [--policy all|full|reactive|whole-mod|"
         "no-plan-cache] [--edit-class no_op|operation_metadata|value_type] "
         "[--scope all|affected|unrelated] "
         "[--site early|middle|late] "
         "[--input MODEL.onnx --subject-hash SHA256] "
         "[--total-nodes N] [--affected-nodes N] [--warmups N] "
         "[--fanout N] [--stages N] [--iterations N] [--seed N]\n";
  return 2;
}

bool parse_args(int argc, char** argv, Config& config) {
  for (int index = 1; index < argc; ++index) {
    const std::string_view key = argv[index];
    if (index + 1 >= argc)
      return false;
    const std::string_view value = argv[++index];
    if (key == "--modules")
      config.modules = value;
    else if (key == "--output")
      config.output = value;
    else if (key == "--revision")
      config.revision = value;
    else if (key == "--input")
      config.input = value;
    else if (key == "--subject-hash")
      config.subject_hash = value;
    else if (key == "--policy")
      config.policy = value;
    else if (key == "--edit-class")
      config.edit_class = value;
    else if (key == "--scope")
      config.scope = value;
    else if (key == "--site")
      config.site = value;
    else if (key == "--total-nodes") {
      const auto parsed = size_value(value);
      if (!parsed)
        return false;
      config.total_nodes = *parsed;
    } else if (key == "--affected-nodes") {
      const auto parsed = size_value(value);
      if (!parsed)
        return false;
      config.affected_nodes = *parsed;
    } else if (key == "--fanout") {
      const auto parsed = size_value(value);
      if (!parsed)
        return false;
      config.fanout = *parsed;
    } else if (key == "--stages") {
      const auto parsed = size_value(value);
      if (!parsed)
        return false;
      config.stage_count = *parsed;
    } else if (key == "--warmups") {
      const auto parsed = size_value(value);
      if (!parsed)
        return false;
      config.warmups = *parsed;
    } else if (key == "--iterations") {
      const auto parsed = size_value(value);
      if (!parsed)
        return false;
      config.iterations = *parsed;
    } else if (key == "--seed") {
      const auto parsed = size_value(value);
      if (!parsed)
        return false;
      config.seed = *parsed;
    } else {
      return false;
    }
  }
  const bool policy_ok =
      config.policy == "all" || config.policy == "full" ||
      config.policy == "reactive" ||
      config.policy == "whole-mod" || config.policy == "no-plan-cache";
  const bool edit_class_ok =
      config.edit_class == "no_op" ||
      config.edit_class == "operation_metadata" ||
      config.edit_class == "value_type";
  const bool scope_ok = config.scope == "all" || config.scope == "affected" ||
                        config.scope == "unrelated";
  const bool site_ok = config.site == "early" || config.site == "middle" ||
                       config.site == "late";
  const bool generated = config.input.empty() && config.subject_hash.empty() &&
                         config.affected_nodes > 0 &&
                         config.total_nodes > config.affected_nodes &&
                         config.fanout > 0;
  const bool model = !config.input.empty() && !config.subject_hash.empty();
  return !config.modules.empty() && !config.output.empty() &&
         !config.revision.empty() && policy_ok && edit_class_ok && scope_ok &&
         site_ok && (generated || model) && config.stage_count > 0 &&
         config.stage_count <= stages.size() && config.iterations > 0;
}

std::string subject_source(std::size_t total, std::size_t affected,
                           std::size_t fanout) {
  const std::size_t unrelated = total - affected;
  std::ostringstream out;
  out << "mod artifact.subject\n"
         "fn node(x: int) -> int { return x }\n"
         "fn graph() -> int {\n"
         "  let hot_0: int = 1\n";
  for (std::size_t index = 1; index < affected; ++index)
    out << "  let hot_" << index << ": int = node(hot_"
        << (index - 1) / fanout
        << ")\n";
  out << "  let cold_0: int = 2\n";
  for (std::size_t index = 1; index < unrelated; ++index)
    out << "  let cold_" << index << ": int = node(cold_" << index - 1
        << ")\n";
  out << "  return hot_" << affected - 1 << "\n}\n";
  return out.str();
}

std::vector<std::string> stage_names(
    std::span<const std::string_view> source) {
  std::vector<std::string> out;
  out.reserve(source.size());
  for (const std::string_view name : source)
    out.emplace_back(name);
  return out;
}

Metrics metrics(const joggle::Attr& report, const joggle::Attr& profile,
                bool reactive, std::size_t stage_count) {
  Metrics out;
  const joggle::Attr& execution = reactive ? field(report, "execution")
                                            : profile;
  out.select_ns = reactive ? integer(report, "select_ns") : 0;
  out.executed_stages = reactive ? integer(report, "executed_stages")
                                 : static_cast<std::int64_t>(stage_count);
  out.reused_stages = reactive ? integer(report, "reused_stages") : 0;
  out.verify_ns = integer(execution, "initial_verification_ns");
  if (const auto* steps = field(execution, "steps").list()) {
    for (const joggle::Attr& step : *steps) {
      out.evaluate_ns += integer(step, "evaluation_ns");
      out.verify_ns += integer(step, "verification_ns");
      out.evaluated_ops += integer(step, "evaluated_ops");
      out.plan_compiles += integer(step, "plan_compiles");
      out.plan_hits += integer(step, "plan_hits");
    }
  }
  if (reactive) {
    std::string misses;
    if (const auto* items = field(report, "stages").list()) {
      for (const joggle::Attr& stage : *items) {
        out.observed_ops += integer(stage, "observed_operations");
        out.observed_values += integer(stage, "observed_values");
        out.changed_functions += integer(stage, "changed_functions");
        const std::string_view miss = string(stage, "miss");
        if (miss.empty() || miss == "none" ||
            misses.find(miss) != std::string::npos)
          continue;
        if (!misses.empty())
          misses += ';';
        misses += miss;
      }
    }
    if (!misses.empty())
      out.miss = std::move(misses);
  }
  return out;
}

struct Subject {
  joggle::Env env;
  joggle::Mod mod;
  joggle::Op hot;
  joggle::Op cold;
  joggle::Val hot_value;
  joggle::Val cold_value;
  joggle::Ty hot_type;
  joggle::Ty cold_type;
  std::size_t hot_index = 0;
  std::size_t total_ops = 0;
  std::size_t affected_ops = 0;
  std::size_t fanout = 0;
  std::string source_hash;
  std::string name = "generated-chain";
  std::string owner = "graph";
  std::string hot_site = "synthetic:hot_0";
  std::string cold_site = "synthetic:cold_0";
  bool generated = true;
};

std::string artifact_state(const Subject& subject) {
  std::ostringstream out;
  out << "root:" << subject.hot_value.type().text() << ':';
  if (subject.hot_value.is_const())
    out << joggle::print(subject.hot_value.constant());
  if (const joggle::Attr* policy =
          subject.hot.meta("artifact.input_revision"))
    out << joggle::print(*policy);
  out << '\n';
  const std::array<joggle::Val, 1> roots{subject.hot_value};
  for (const joggle::Op operation : subject.mod.affected(roots)) {
    out << static_cast<unsigned>(operation.kind()) << ':' << operation.callee()
        << ':' << operation.args().size() << ':';
    for (const joggle::Val value : operation.outs()) {
      out << value.type().text() << ',';
      if (value.is_const())
        out << joggle::print(value.constant()) << ',';
    }
    if (const joggle::Attr* artifact = operation.meta("artifact.artifact"))
      out << joggle::print(*artifact);
    out << '\n';
  }
  return out.str();
}

bool parse_model(const Config& config, Subject& subject) {
  std::ifstream input(config.input, std::ios::binary);
  if (!input)
    return false;
  const std::vector<unsigned char> raw{std::istreambuf_iterator<char>(input),
                                       std::istreambuf_iterator<char>()};
  if (!subject.env.load("onnx"))
    return false;
  const std::array<joggle::Attr, 1> args{
      joggle::Attr(joggle::Attr::Bytes(raw.begin(), raw.end()))};
  std::vector<joggle::Attr> returns;
  if (!subject.env.call("onnx.read", args, returns) || returns.size() != 1 ||
      !returns.front().string())
    return false;
  subject.source_hash = config.subject_hash;
  subject.name = std::filesystem::path(config.input).stem().string();
  subject.owner = "main";
  subject.generated = false;
  return joggle::parse(subject.env, *returns.front().string(), subject.mod,
                       config.input) &&
         subject.mod.verify(subject.env);
}

bool select_model_sites(Subject& subject, std::string_view site) {
  const joggle::Fn graph = subject.mod.find_fn("main");
  if (!graph)
    return false;
  const std::vector<joggle::Op> operations = graph.body().ops();
  for (const joggle::Op operation : subject.mod.ops())
    for (const joggle::Val output : operation.outs())
      subject.fanout = std::max(subject.fanout, output.users().size());
  std::vector<std::size_t> candidates;
  for (std::size_t index = 0; index < operations.size(); ++index) {
    const joggle::Op op = operations[index];
    if (op.kind() != joggle::Op::Kind::call || op.outs().size() != 1 ||
        !op.outs().front().type().valid() ||
        op.outs().front().type().text() == "_" ||
        op.form() == joggle::Op::Form::hidden ||
        op.callee() == "onnx.tensor" || op.callee() == "onnx.model")
      continue;
    candidates.push_back(index);
  }
  if (candidates.empty())
    return false;
  const std::size_t position = site == "early"    ? 0
                               : site == "middle" ? candidates.size() / 2
                                                  : candidates.size() - 1;
  const std::size_t index = candidates[position];
  const joggle::Op op = operations[index];
  const std::array<joggle::Val, 1> roots{op.outs().front()};
  const std::vector<joggle::Op> affected = subject.mod.affected(roots);
  for (std::size_t candidate = 0; candidate < operations.size(); ++candidate) {
    const joggle::Op unrelated = operations[candidate];
    if (unrelated == op || unrelated.outs().size() != 1 ||
        !unrelated.outs().front().type().valid() ||
        unrelated.outs().front().type().text() == "_" ||
        unrelated.form() == joggle::Op::Form::hidden ||
        std::find(affected.begin(), affected.end(), unrelated) !=
            affected.end())
      continue;
    subject.hot = op;
    subject.cold = unrelated;
    subject.hot_value = roots.front();
    subject.cold_value = unrelated.outs().front();
    subject.hot_type = subject.hot_value.type();
    subject.cold_type = subject.cold_value.type();
    subject.hot_index = index;
    subject.affected_ops = affected.size();
    const std::string prefix = std::string(site) + ":";
    subject.hot_site =
        prefix + std::string(op.callee()) + "@" + std::to_string(index);
    subject.cold_site =
        prefix +
        (unrelated.kind() == joggle::Op::Kind::call
             ? std::string(unrelated.callee()) + "@" +
                   std::to_string(candidate)
             : "op@" + std::to_string(candidate));
    return true;
  }
  return false;
}

bool prepare(const Config& config, Subject& subject) {
  subject.env.path(config.modules);
  if (!subject.env.load("artifact.reactive")) {
    subject.env.print_diags(stderr);
    return false;
  }
  if (!config.input.empty()) {
    if (!parse_model(config, subject) ||
        !select_model_sites(subject, config.site)) {
      subject.env.print_diags(stderr);
      subject.mod.print_diags(stderr);
      return false;
    }
    subject.total_ops = subject.mod.ops().size();
    return true;
  }
  const std::string source =
      subject_source(config.total_nodes, config.affected_nodes, config.fanout);
  subject.source_hash = digest(source);
  if (!joggle::parse(subject.env, source, subject.mod, "generated.jog") ||
      !subject.mod.verify(subject.env)) {
    subject.mod.print_diags(stderr);
    return false;
  }
  const joggle::Fn graph = subject.mod.find_fn("graph");
  const std::vector<joggle::Op> operations = graph.body().ops();
  for (std::size_t index = 0; index < operations.size(); ++index) {
    const joggle::Op op = operations[index];
    if (op.kind() != joggle::Op::Kind::constant || op.outs().size() != 1)
      continue;
    const joggle::Val value = op.outs().front();
    if (value.name() == "hot_0") {
      subject.hot = op;
      subject.hot_value = value;
      subject.hot_index = index;
    } else if (value.name() == "cold_0") {
      subject.cold = op;
      subject.cold_value = value;
    }
  }
  if (!subject.hot || !subject.cold || !subject.hot_value ||
      !subject.cold_value)
    return false;
  subject.hot_type = subject.hot_value.type();
  subject.cold_type = subject.cold_value.type();
  subject.total_ops = subject.mod.ops().size();
  for (const joggle::Op operation : subject.mod.ops())
    for (const joggle::Val output : operation.outs())
      subject.fanout = std::max(subject.fanout, output.users().size());
  const std::array<joggle::Val, 1> roots{subject.hot_value};
  subject.affected_ops = subject.mod.affected(roots).size();
  return true;
}

bool edit_subject(Subject& subject, std::string_view edit_class,
                  std::string_view scope, std::int64_t value) {
  if (edit_class == "no_op")
    return true;
  if (edit_class == "value_type") {
    (void)value;
    const bool affected = scope == "affected";
    const joggle::Val target = affected ? subject.hot_value : subject.cold_value;
    const joggle::Ty original = affected ? subject.hot_type : subject.cold_type;
    const joggle::Ty next =
        target.type().text() == "_" ? original : joggle::Ty("_");
    return subject.mod.type(target, next);
  }
  const joggle::Op operation =
      scope == "affected" ? subject.hot : subject.cold;
  if (subject.generated)
    return subject.mod.replace(operation, joggle::Attr(value));
  return subject.mod.set(operation, "artifact.input_revision",
                         joggle::Attr(value));
}

bool run_direct(Subject& subject, std::span<const std::string_view> selected,
                joggle::Attr& profile) {
  const std::array<joggle::Attr, 2> args{joggle::Attr(subject.owner),
                                         joggle::Attr(static_cast<std::int64_t>(
                                             subject.hot_index))};
  return joggle::run(subject.env, selected, subject.mod, args, nullptr,
                     &profile);
}

std::vector<std::string> policies(const Config& config) {
  if (config.policy != "all")
    return {config.policy};
#if JOGGLE_ARTIFACT_PERSISTENT_PLANS
  return {"full", "reactive", "whole-mod"};
#else
  return {"no-plan-cache"};
#endif
}

std::vector<std::string> scopes(const Config& config) {
  if (config.edit_class == "no_op")
    return {"none"};
  return config.scope == "all"
             ? std::vector<std::string>{"affected", "unrelated"}
             : std::vector<std::string>{config.scope};
}

bool compatible_policy(std::string_view policy) {
#if JOGGLE_ARTIFACT_PERSISTENT_PLANS
  return policy != "no-plan-cache";
#else
  return policy == "no-plan-cache";
#endif
}

bool benchmark(const Config& config, std::ofstream& output,
               std::string_view policy, std::string_view scope,
               std::map<std::string, std::string, std::less<>>& expected) {
  Subject subject;
  if (!prepare(config, subject)) {
    std::cerr << "failed to prepare generated subject\n";
    return false;
  }

  const bool reactive = policy == "reactive" || policy == "whole-mod" ||
                        policy == "no-plan-cache";
  const std::span<const std::string_view> selected_names =
      policy == "whole-mod"
          ? std::span<const std::string_view>(whole_stages).first(
                config.stage_count)
          : std::span<const std::string_view>(stages).first(config.stage_count);
  std::unique_ptr<joggle::ReactiveSchedule> schedule;
  if (reactive)
    schedule = std::make_unique<joggle::ReactiveSchedule>(
        stage_names(selected_names));
  const std::array<joggle::Attr, 2> args{joggle::Attr(subject.owner),
                                         joggle::Attr(static_cast<std::int64_t>(
                                             subject.hot_index))};
  const std::span<const std::string_view> complete =
      std::span<const std::string_view>(full_stages).first(config.stage_count);

  const auto execute = [&](joggle::Attr& report, joggle::Attr& profile) {
    if (reactive)
      return schedule->run(subject.env, subject.mod, args, &report);
    return run_direct(subject, complete, profile);
  };

  joggle::Attr report;
  joggle::Attr profile;
  joggle::Attr initialization;
  if (!run_direct(subject, complete, initialization)) {
    subject.env.print_diags(stderr);
    return false;
  }
  if (!execute(report, profile)) {
    subject.env.print_diags(stderr);
    return false;
  }

  const std::uint64_t offset = scope == "affected" ? UINT64_C(1000000)
                                                    : UINT64_C(2000000);
  for (std::size_t index = 0; index < config.warmups; ++index) {
    const auto value = static_cast<std::int64_t>(offset + config.seed + index);
    if (!edit_subject(subject, config.edit_class, scope, value) ||
        !execute(report, profile)) {
      subject.env.print_diags(stderr);
      return false;
    }
  }

  for (std::size_t index = 0; index < config.iterations; ++index) {
    const auto value = static_cast<std::int64_t>(
        offset + config.seed + config.warmups + index);
    if (!edit_subject(subject, config.edit_class, scope, value))
      return false;
    report = {};
    profile = {};
    const auto begin = Clock::now();
    const bool ran = execute(report, profile);
    const auto wall = std::chrono::duration_cast<std::chrono::nanoseconds>(
        Clock::now() - begin);
    const bool verified = ran && subject.mod.verify(subject.env);
    if (!verified) {
      subject.env.print_diags(stderr);
      subject.mod.print_diags(stderr);
    }
    const std::string output_digest = digest(artifact_state(subject));
    const std::string key = config.edit_class + ":" + std::string(scope) +
                            ":" + std::to_string(index);
    const auto [position, inserted] = expected.emplace(key, output_digest);
    const bool correct = verified &&
                         (inserted || position->second == output_digest);
    const Metrics measured =
        metrics(report, profile, reactive, config.stage_count);
    // These diagnostic stages each traverse their selected operation list
    // once and never change its topology. Count that traversal separately
    // from evaluator instructions, outside the timed interval.
    const std::array<joggle::Val, 1> roots{subject.hot_value};
    const auto operations_per_stage = reactive
        ? subject.mod.affected(roots).size()
        : subject.mod.find_fn(subject.owner).ops().size();
    const auto visited_graph_ops = operations_per_stage *
        static_cast<std::size_t>(measured.executed_stages);
    output << "joggle," << csv(config.revision) << ',' << csv(subject.name) << ','
           << subject.source_hash << ',' << subject.total_ops << ','
           << subject.affected_ops << ',' << subject.fanout << ','
           << config.stage_count << ',' << config.edit_class << ',' << scope
           << ','
           << csv(scope == "unrelated" ? subject.cold_site : subject.hot_site)
           << ','
           << policy << ",warm," << index << ',' << wall.count() << ','
           << measured.select_ns << ',' << measured.evaluate_ns << ','
           << measured.verify_ns << ',' << measured.executed_stages << ','
           << measured.reused_stages << ',' << measured.observed_ops << ','
           << measured.observed_values << ',' << measured.changed_functions
           << ',' << measured.evaluated_ops << ',' << visited_graph_ops
           << ',' << measured.plan_compiles
           << ',' << measured.plan_hits << ',' << measured.miss << ','
           << output_digest << ',' << (correct ? "true" : "false") << ','
           << config.seed << '\n';
    if (!correct)
      return false;
  }
  return true;
}

}  // namespace

// Compile fresh source graphs while retaining only native environment state.
bool load_production_env(joggle::Env& env, const char* modules) {
  env.path(modules);
  for (const std::string_view module : {"onnx.nn", "c", "tile", "mem"}) {
    if (!env.load(module)) {
      env.print_diags(stderr);
      return false;
    }
  }
  return true;
}

int compile_source(joggle::Env& env, const char* source,
                   const std::filesystem::path& directory, int sequence,
                   bool external_data,
                   joggle::Mod* prepared_cache = nullptr) {
  const auto begin = Clock::now();
  std::ifstream input(source, std::ios::binary);
  if (!input) {
    std::cerr << "cannot read source: " << source << '\n';
    return 1;
  }
  const std::string text((std::istreambuf_iterator<char>(input)), {});
  joggle::Mod mod;
  if (!joggle::parse(env, text, mod, source)) {
    env.print_diags(stderr);
    return 1;
  }
  const auto parsed = Clock::now();
  constexpr std::array<std::string_view, 6> pipeline{
      "onnx.nn.infer", "onnx.nn.convert", "c.prepare", "tile.scalarize",
      "mem.plan", "c.noalias"};
  joggle::Attr profile;
  std::size_t imported_instances = 0;
  const auto lower = [&]() {
    if (!prepared_cache)
      return joggle::run(env, pipeline, mod, {}, nullptr, &profile);
    joggle::Attr::List steps;
    std::int64_t initial_ns = 0, snapshot_ns = 0;
    std::int64_t import_ns = 0, materialize_ns = 0, capture_ns = 0;
    std::size_t reachable_imports = 0, materialized_instances = 0;
    std::vector<joggle::Fn> imported;
    std::vector<joggle::Fn> cached_sources;
    for (const auto stage : pipeline) {
      if (stage == "c.prepare") {
        const auto import_begin = Clock::now();
        std::map<std::string, bool> imported_keys;
        std::vector<std::string> import_names;
        for (const auto fn : prepared_cache->fns()) {
          const auto* key = fn.meta("opt.instance");
          if (!key || !imported_keys.emplace(joggle::print(*key), true).second)
            continue;
          std::string name = "__prepared_" + std::to_string(imported_instances);
          while (!mod.find_fns(name).empty()) name += "_";
          import_names.push_back(name);
          cached_sources.push_back(fn);
          ++imported_instances;
        }
        imported = mod.declare(env, cached_sources, import_names);
        if (imported.size() != cached_sources.size()) {
          mod.print_diags(stderr);
          return false;
        }
        import_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            Clock::now() - import_begin).count();
      }
      joggle::Attr part;
      if (!joggle::run(env, stage, mod, {}, nullptr, &part)) return false;
      initial_ns += integer(part, "initial_verification_ns");
      snapshot_ns += integer(part, "snapshot_ns");
      const auto* entries = field(part, "steps").list();
      if (!entries) return false;
      steps.insert(steps.end(), entries->begin(), entries->end());
      if (stage == "c.prepare") {
        const auto materialize_begin = Clock::now();
        // Only signatures participate in preparation. Reconnect referenced
        // prepared bodies afterwards, before model-wide lowering/planning.
        std::vector<std::vector<joggle::Op>> declaration_calls(imported.size());
        for (const auto op : mod.ops()) {
          if (op.kind() != joggle::Op::Kind::call) continue;
          const auto target = env.resolve(mod, op);
          const auto found = std::find(imported.begin(), imported.end(), target);
          if (found != imported.end())
            declaration_calls[static_cast<std::size_t>(found - imported.begin())].push_back(op);
        }
        std::vector<joggle::Fn> materialize_sources;
        std::vector<std::string> materialize_names;
        for (std::size_t i = 0; i < imported.size(); ++i) {
          if (declaration_calls[i].empty()) continue;
          std::string name = std::string(imported[i].name()) + "_body";
          while (!mod.find_fns(name).empty()) name += "_";
          materialize_sources.push_back(cached_sources[i]);
          materialize_names.push_back(name);
        }
        const auto bodies = mod.clone(env, materialize_sources, materialize_names);
        if (bodies.size() != materialize_sources.size()) {
          mod.print_diags(stderr);
          return false;
        }
        std::size_t body_index = 0;
        for (std::size_t i = 0; i < imported.size(); ++i) {
          const auto declaration = imported[i];
          const auto& callers = declaration_calls[i];
          if (!callers.empty()) {
            const auto body = bodies[body_index++];
            for (const auto op : callers)
              if (!mod.retarget(env, op, body)) { mod.print_diags(stderr); return false; }
            imported[i] = body;
            ++materialized_instances;
          }
          if (!mod.erase(env, declaration)) { mod.print_diags(stderr); return false; }
        }
        materialize_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            Clock::now() - materialize_begin).count();
        const auto capture_begin = Clock::now();
        // Save bodies before scalarization and model-wide memory planning.
        // The environment and pipeline are fixed for this diagnostic session.
        joggle::Mod next_cache;
        if (!joggle::parse(env, "mod artifact.prepared\n", next_cache)) return false;
        std::vector<joggle::Fn> reachable;
        const auto owned = mod.fns();
        for (const auto fn : owned)
          if (!fn.local()) reachable.push_back(fn);
        for (std::size_t i = 0; i < reachable.size(); ++i) {
          for (const auto op : reachable[i].ops()) {
            if (op.kind() != joggle::Op::Kind::call) continue;
            const auto fn = env.resolve(mod, op);
            if (fn && std::find(owned.begin(), owned.end(), fn) != owned.end() &&
                std::find(reachable.begin(), reachable.end(), fn) == reachable.end())
              reachable.push_back(fn);
          }
        }
        for (const auto fn : imported) {
          if (std::find(reachable.begin(), reachable.end(), fn) != reachable.end()) {
            ++reachable_imports;
          }
        }
        std::size_t saved = 0;
        std::map<std::string, bool> saved_keys;
        std::vector<joggle::Fn> save_sources;
        std::vector<std::string> save_names;
        for (const auto fn : reachable) {
          const auto* key = fn.meta("opt.instance");
          if (!fn.local() || !key || !fn.generics().empty() ||
              !saved_keys.emplace(joggle::print(*key), true).second)
            continue;
          save_sources.push_back(fn);
          save_names.push_back("saved_" + std::to_string(saved++));
        }
        if (next_cache.clone(env, save_sources, save_names).size() != save_sources.size()) {
          next_cache.print_diags(stderr);
          return false;
        }
        if (!next_cache.verify(env)) {
          next_cache.print_diags(stderr);
          return false;
        }
        *prepared_cache = std::move(next_cache);
        capture_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            Clock::now() - capture_begin).count();
      }
    }
    joggle::Attr::Dict combined;
    combined["succeeded"] = joggle::Attr(true);
    combined["initial_verification_ns"] = joggle::Attr(initial_ns);
    combined["snapshot_ns"] = joggle::Attr(snapshot_ns);
    combined["steps"] = joggle::Attr(std::move(steps));
    combined["imported_instances"] = joggle::Attr(static_cast<std::int64_t>(imported_instances));
    combined["reachable_imports"] = joggle::Attr(static_cast<std::int64_t>(reachable_imports));
    combined["materialized_instances"] = joggle::Attr(static_cast<std::int64_t>(materialized_instances));
    combined["import_ns"] = joggle::Attr(import_ns);
    combined["materialize_ns"] = joggle::Attr(materialize_ns);
    combined["capture_ns"] = joggle::Attr(capture_ns);
    profile = joggle::Attr(std::move(combined));
    return true;
  };
  if (!lower()) {
    env.print_diags(stderr);
    return 1;
  }
  const std::array<joggle::Attr, 1> placement{joggle::Attr("static")};
  if (!joggle::run(env, "c.place", mod, placement)) {
    env.print_diags(stderr);
    return 1;
  }
  joggle::Attr frontier;
  if (!joggle::query(env, "c.frontier", mod, frontier) ||
      !frontier.list() || !frontier.list()->empty()) {
    env.print_diags(stderr);
    std::cerr << "production source retains an unsupported C frontier\n";
    return 1;
  }
  const auto lowered = Clock::now();
  const auto ns = [](auto duration) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
  };
  joggle::Attr::Dict emit_components;
  std::int64_t measured_emit_ns = 0;
  const auto record_emit = [&](std::string key, auto begin, auto end) {
    const auto duration = ns(end - begin);
    emit_components[std::move(key)] = joggle::Attr(duration);
    measured_emit_ns += duration;
  };
  const std::string prefix = std::to_string(sequence);
  // Keep tensor payloads out of the C translation unit. The C mod owns the
  // layout and propagates this data parameter through calls that need it.
  const std::array<joggle::Attr, 1> data_name{joggle::Attr("weights")};
  const std::span<const joggle::Attr> data_argument =
      external_data ? std::span<const joggle::Attr>(data_name)
                    : std::span<const joggle::Attr>();
  for (const auto& [function, suffix] :
       std::array<std::pair<std::string_view, std::string_view>, 3>{{
           {"c.source", ".c"}, {"c.header", ".h"}, {"c.api", ".api.json"}}}) {
    joggle::Attr result;
    const auto query_begin = Clock::now();
    if (!joggle::query(env, function, mod, result, data_argument)) {
      env.print_diags(stderr);
      return 1;
    }
    const auto query_end = Clock::now();
    record_emit(std::string(function) + ".query", query_begin, query_end);
    const auto write_begin = Clock::now();
    std::ofstream file(directory / (prefix + std::string(suffix)), std::ios::binary);
    if (result.string()) file << *result.string();
    else file << joggle::print(result);
    file.close();
    if (!file) return 1;
    record_emit(std::string(function) + ".write", write_begin, Clock::now());
  }
  if (external_data) {
    joggle::Attr data;
    const auto data_begin = Clock::now();
    if (!joggle::query(env, "c.data", mod, data) || !data.bytes()) {
      env.print_diags(stderr);
      std::cerr << "C data query did not produce a binary payload\n";
      return 1;
    }
    record_emit("c.data.query", data_begin, Clock::now());
    const auto payload_begin = Clock::now();
    std::ofstream payload(directory / (prefix + ".bin"), std::ios::binary);
    const auto& bytes = *data.bytes();
    if (!bytes.empty())
      payload.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    payload.close();
    if (!payload) return 1;
    record_emit("c.data.write", payload_begin, Clock::now());
  }
  const auto end = Clock::now();
  emit_components["overhead"] = joggle::Attr(ns(end - lowered) - measured_emit_ns);
  std::ofstream timing(directory / (prefix + ".timing.json"));
  timing << "{\"schema\":\"resident-lowering/v1\",\"sequence\":" << sequence
         << ",\"parse_ns\":" << ns(parsed - begin)
         << ",\"lower_ns\":" << ns(lowered - parsed)
         << ",\"emit_ns\":" << ns(end - lowered)
         << ",\"wall_ns\":" << ns(end - begin) << "}\n";
  std::ofstream detail(directory / (prefix + ".profile.attr"));
  // Nested diagnostic components partition emit_ns; they are not additional
  // stages in the complete ready interval. Keep the aggregate timing intact.
  if (!profile.dict()) return 1;
  auto detailed_profile = *profile.dict();
  detailed_profile["emit_components_ns"] = joggle::Attr(std::move(emit_components));
  detail << joggle::print(joggle::Attr(std::move(detailed_profile)));
  if (!timing || !detail) return 1;
  return 0;
}

int compile_sequence(int argc, char** argv) {
  if (argc < 5) {
    std::cerr << "usage: joggle-artifact-reactive --compile-sequence MODULES OUTDIR SOURCE...\n";
    return 2;
  }
  const std::filesystem::path directory(argv[3]);
  if (std::filesystem::exists(directory)) {
    std::cerr << "refusing to replace an existing output directory\n";
    return 2;
  }
  std::filesystem::create_directories(directory);
  joggle::Env env;
  if (!load_production_env(env, argv[2])) return 1;
  joggle::Mod prepared_cache;
  const bool reuse = std::string_view(argv[1]) == "--compile-reuse-sequence";
  for (int index = 4; index < argc; ++index)
    if (compile_source(env, argv[index], directory, index - 4, false,
                       reuse ? &prepared_cache : nullptr)) return 1;
  return 0;
}

int compile_server(int argc, char** argv) {
  if (argc != 3) return 2;
  joggle::Env env;
  if (!load_production_env(env, argv[2])) return 1;
  joggle::Mod prepared_cache;
  const bool reuse = std::string_view(argv[1]) == "--compile-reuse-server";
  std::cout << "{\"ready\":true}\n" << std::flush;
  std::string line;
  int sequence = 0;
  while (std::getline(std::cin, line)) {
    joggle::Attr request;
    if (!joggle::parse(env, line, request, "compile-request") ||
        !request.dict() || request.dict()->size() != 2) return 2;
    const auto source = field(request, "source").string();
    const auto output = field(request, "output").string();
    if (!source || !output || source->empty() || output->empty()) return 2;
    const std::filesystem::path directory{std::string(*output)};
    if (std::filesystem::exists(directory)) {
      std::cerr << "refusing to replace an existing output directory\n";
      return 2;
    }
    std::filesystem::create_directories(directory);
    if (compile_source(env, std::string(*source).c_str(), directory, 0, true,
                       reuse ? &prepared_cache : nullptr)) return 1;
    std::cout << "{\"ok\":true,\"sequence\":" << sequence++ << "}\n" << std::flush;
  }
  return 0;
}

int main(int argc, char** argv) {
  if (argc > 1 && (std::string_view(argv[1]) == "--compile-server" ||
                   std::string_view(argv[1]) == "--compile-reuse-server"))
    return compile_server(argc, argv);
  if (argc > 1 && (std::string_view(argv[1]) == "--compile-sequence" ||
                   std::string_view(argv[1]) == "--compile-reuse-sequence"))
    return compile_sequence(argc, argv);
  Config config;
  if (!parse_args(argc, argv, config))
    return usage();
  for (const std::string& policy : policies(config)) {
    if (!compatible_policy(policy)) {
      std::cerr << "policy " << policy
                << " does not match this evaluator build\n";
      return 2;
    }
  }

  std::ofstream output(config.output, std::ios::binary | std::ios::trunc);
  if (!output) {
    std::cerr << "cannot open output: " << config.output << '\n';
    return 1;
  }
  output << "system,system_revision,subject,subject_hash,total_ops,"
            "affected_ops,fanout,stages,edit_class,edit_scope,edit_site,"
            "policy,cache_state,iteration,wall_ns,select_ns,evaluate_ns,"
            "verify_ns,"
            "executed_stages,reused_stages,observed_ops,observed_values,"
            "changed_functions,evaluated_ops,visited_graph_ops,plan_compiles,plan_hits,"
            "miss_reason,output_digest,correct,seed\n";

  std::map<std::string, std::string, std::less<>> expected;
  std::vector<std::string> selected_policies = policies(config);
  std::mt19937_64 random(config.seed);
  std::shuffle(selected_policies.begin(), selected_policies.end(), random);
  for (const std::string& policy : selected_policies)
    for (const std::string& scope : scopes(config))
      if (!benchmark(config, output, policy, scope, expected))
        return 1;
  return output ? 0 : 1;
}
