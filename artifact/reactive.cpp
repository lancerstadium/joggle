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
  std::string edit = "all";
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
         "--revision GIT [--policy all|full|suffix|reactive|whole-mod|"
         "no-plan-cache] [--edit all|affected|unrelated] "
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
    else if (key == "--edit")
      config.edit = value;
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
      config.policy == "suffix" || config.policy == "reactive" ||
      config.policy == "whole-mod" || config.policy == "no-plan-cache";
  const bool edit_ok = config.edit == "all" || config.edit == "affected" ||
                       config.edit == "unrelated";
  const bool site_ok = config.site == "early" || config.site == "middle" ||
                       config.site == "late";
  const bool generated = config.input.empty() && config.subject_hash.empty() &&
                         config.affected_nodes > 0 &&
                         config.total_nodes > config.affected_nodes &&
                         config.fanout > 0;
  const bool model = !config.input.empty() && !config.subject_hash.empty();
  return !config.modules.empty() && !config.output.empty() &&
         !config.revision.empty() && policy_ok && edit_ok && site_ok &&
         (generated || model) && config.stage_count > 0 &&
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
    if (unrelated == op || unrelated.form() == joggle::Op::Form::hidden ||
        std::find(affected.begin(), affected.end(), unrelated) !=
            affected.end())
      continue;
    subject.hot = op;
    subject.cold = unrelated;
    subject.hot_value = roots.front();
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
    }
  }
  if (!subject.hot || !subject.cold || !subject.hot_value)
    return false;
  subject.total_ops = subject.mod.ops().size();
  for (const joggle::Op operation : subject.mod.ops())
    for (const joggle::Val output : operation.outs())
      subject.fanout = std::max(subject.fanout, output.users().size());
  const std::array<joggle::Val, 1> roots{subject.hot_value};
  subject.affected_ops = subject.mod.affected(roots).size();
  return true;
}

bool edit_subject(Subject& subject, std::string_view edit,
                  std::int64_t value) {
  const joggle::Op operation = edit == "affected" ? subject.hot : subject.cold;
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
  return {"full", "suffix", "reactive", "whole-mod"};
#else
  return {"no-plan-cache"};
#endif
}

std::vector<std::string> edits(const Config& config) {
  return config.edit == "all"
             ? std::vector<std::string>{"affected", "unrelated"}
             : std::vector<std::string>{config.edit};
}

bool compatible_policy(std::string_view policy) {
#if JOGGLE_ARTIFACT_PERSISTENT_PLANS
  return policy != "no-plan-cache";
#else
  return policy == "no-plan-cache";
#endif
}

bool benchmark(const Config& config, std::ofstream& output,
               std::string_view policy, std::string_view edit,
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

  const auto execute = [&](joggle::Attr& report, joggle::Attr& profile) {
    if (reactive)
      return schedule->run(subject.env, subject.mod, args, &report);
    std::span<const std::string_view> selected =
        std::span<const std::string_view>(stages).first(config.stage_count);
    if (policy == "suffix")
      selected = selected.subspan(0);
    return run_direct(subject, selected, profile);
  };

  joggle::Attr report;
  joggle::Attr profile;
  if (!execute(report, profile)) {
    subject.env.print_diags(stderr);
    return false;
  }

  const std::uint64_t offset = edit == "affected" ? UINT64_C(1000000)
                                                   : UINT64_C(2000000);
  for (std::size_t index = 0; index < config.warmups; ++index) {
    const auto value = static_cast<std::int64_t>(offset + config.seed + index);
    if (!edit_subject(subject, edit, value) || !execute(report, profile)) {
      subject.env.print_diags(stderr);
      return false;
    }
  }

  for (std::size_t index = 0; index < config.iterations; ++index) {
    const auto value = static_cast<std::int64_t>(
        offset + config.seed + config.warmups + index);
    if (!edit_subject(subject, edit, value))
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
    const std::string output_digest = digest(joggle::print(subject.mod));
    const std::string key = std::string(edit) + ":" + std::to_string(index);
    const auto [position, inserted] = expected.emplace(key, output_digest);
    const bool correct = verified &&
                         (inserted || position->second == output_digest);
    const Metrics measured =
        metrics(report, profile, reactive, config.stage_count);
    output << "joggle," << csv(config.revision) << ',' << csv(subject.name) << ','
           << subject.source_hash << ',' << subject.total_ops << ','
           << subject.affected_ops << ',' << subject.fanout << ','
           << config.stage_count << ',' << edit << ','
           << csv(edit == "affected" ? subject.hot_site : subject.cold_site)
           << ','
           << policy << ",warm," << index << ',' << wall.count() << ','
           << measured.select_ns << ',' << measured.evaluate_ns << ','
           << measured.verify_ns << ',' << measured.executed_stages << ','
           << measured.reused_stages << ',' << measured.observed_ops << ','
           << measured.observed_values << ',' << measured.changed_functions
           << ',' << measured.evaluated_ops << ',' << measured.plan_compiles
           << ',' << measured.plan_hits << ',' << measured.miss << ','
           << output_digest << ',' << (correct ? "true" : "false") << ','
           << config.seed << '\n';
    if (!correct)
      return false;
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
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
            "affected_ops,fanout,stages,edit_class,edit_site,policy,"
            "cache_state,iteration,wall_ns,select_ns,evaluate_ns,verify_ns,"
            "executed_stages,reused_stages,observed_ops,observed_values,"
            "changed_functions,evaluated_ops,plan_compiles,plan_hits,"
            "miss_reason,output_digest,correct,seed\n";

  std::map<std::string, std::string, std::less<>> expected;
  for (const std::string& policy : policies(config))
    for (const std::string& edit : edits(config))
      if (!benchmark(config, output, policy, edit, expected))
        return 1;
  return output ? 0 : 1;
}
