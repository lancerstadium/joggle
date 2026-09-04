#include "lang/fn.h"

#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace joggle::detail {

bool verify_fn_body(const FnBody& body, Diag& diagnostics) {
  const std::size_t initial_diagnostics = diagnostics.size();
  const auto locate = [&](SyntaxRange range) -> std::optional<Loc> {
    if (body.source.empty()) {
      return std::nullopt;
    }
    return Loc{body.source, range.begin, range.end};
  };
  const auto report = [&](std::string message, SyntaxRange range) {
    diagnostics.report(std::move(message), locate(range));
  };

  if (body.blocks.empty()) {
    report("a fn definition must have a body", body.range);
    return false;
  }

  if (body.blocks.front().name != "entry") {
    report("the first block must be named 'entry'", body.blocks.front().range);
  }
  if (!body.blocks.front().arguments.empty()) {
    report("the entry block cannot declare block arguments",
           body.blocks.front().range);
  }

  std::unordered_map<std::string_view, const BlkSyntax*> blocks;
  std::unordered_set<std::string_view> definitions;
  const auto falls_through =
      [&](const auto& self,
          const std::vector<StatementSyntax>& statements) -> bool {
    for (const StatementSyntax& statement : statements) {
      if (statement.kind == StatementSyntax::Kind::Return ||
          statement.kind == StatementSyntax::Kind::Break ||
          statement.kind == StatementSyntax::Kind::Continue) {
        return false;
      }
      if (statement.kind == StatementSyntax::Kind::If && statement.has_else &&
          !self(self, statement.body) && !self(self, statement.otherwise)) {
        return false;
      }
    }
    return true;
  };
  const auto check_statements =
      [&](const auto& self, const std::vector<StatementSyntax>& statements,
          std::unordered_set<std::string_view>& names) -> void {
    bool reachable = true;
    for (const StatementSyntax& statement : statements) {
      if (!reachable) {
        report("unreachable statement after a control transfer",
               statement.range);
      }
      if (statement.kind != StatementSyntax::Kind::Expr) {
        auto nested = names;
        if (statement.kind == StatementSyntax::Kind::For) {
          if (!statement.iterator) {
            report("for statement has no iterator", statement.range);
          } else {
            nested.insert(statement.iterator->name);
          }
        }
        self(self, statement.body, nested);
        if (statement.kind == StatementSyntax::Kind::If) {
          nested = names;
          self(self, statement.otherwise, nested);
        }
        if (statement.kind == StatementSyntax::Kind::Return ||
            statement.kind == StatementSyntax::Kind::Break ||
            statement.kind == StatementSyntax::Kind::Continue ||
            (statement.kind == StatementSyntax::Kind::If &&
             statement.has_else &&
             !falls_through(falls_through, statement.body) &&
             !falls_through(falls_through, statement.otherwise))) {
          reachable = false;
        }
        continue;
      }
      for (const BindingSyntax& binding : statement.bindings) {
        if (!binding.rebind && !names.insert(binding.name).second) {
          report("duplicate local value '" + binding.name + "'",
                 statement.range);
        }
      }
    }
  };
  for (const BlkSyntax& block : body.blocks) {
    if (block.name.empty()) {
      report("a block must have a name", block.range);
    } else if (!blocks.emplace(block.name, &block).second) {
      report("duplicate block '" + block.name + "'", block.range);
    }
    for (const BlkArgSyntax& argument : block.arguments) {
      if (!definitions.insert(argument.name).second) {
        report("duplicate local value '" + argument.name + "'", argument.range);
      }
    }
    check_statements(check_statements, block.statements, definitions);
  }

  const bool structured =
      body.blocks.size() == 1U && !body.blocks.front().terminator;
  if (structured &&
      falls_through(falls_through, body.blocks.front().statements)) {
    report("a fn body has a path that does not return",
           body.blocks.front().range);
  }

  const auto check_successor = [&](const SuccessorSyntax& successor) {
    const auto target = blocks.find(successor.target);
    if (target == blocks.end()) {
      report("unknown successor block '" + successor.target + "'",
             successor.range);
      return;
    }
    if (target->second == &body.blocks.front()) {
      report("control flow cannot branch to the entry block", successor.range);
    }
    if (successor.arguments.size() != target->second->arguments.size()) {
      report("successor '" + successor.target + "' expects " +
                 std::to_string(target->second->arguments.size()) +
                 " block argument(s), but the edge provides " +
                 std::to_string(successor.arguments.size()),
             successor.range);
    }
  };

  for (const BlkSyntax& block : body.blocks) {
    if (!block.terminator) {
      if (!structured) {
        report("explicit block '" + block.name + "' has no terminator",
               block.range);
      }
      continue;
    }
    const TermSyntax& terminator = *block.terminator;
    switch (terminator.kind) {
    case TermSyntax::Kind::Return:
      if (terminator.condition || !terminator.successors.empty()) {
        report("return cannot have a condition or successor", terminator.range);
      }
      break;
    case TermSyntax::Kind::Jump:
      if (terminator.condition || !terminator.values.empty() ||
          terminator.successors.size() != 1U) {
        report("jump must have exactly one successor", terminator.range);
      }
      break;
    case TermSyntax::Kind::Branch:
      if (!terminator.condition || !terminator.values.empty() ||
          terminator.successors.size() != 2U) {
        report("branch must have one condition and two successors",
               terminator.range);
      }
      break;
    }
    for (const SuccessorSyntax& successor : terminator.successors) {
      check_successor(successor);
    }
  }

  if (blocks.size() == body.blocks.size() &&
      blocks.find("entry") != blocks.end()) {
    std::unordered_set<std::string_view> reachable{"entry"};
    std::vector<std::string_view> pending{"entry"};
    while (!pending.empty()) {
      const std::string_view name = pending.back();
      pending.pop_back();
      const BlkSyntax* block = blocks.at(name);
      if (!block->terminator) {
        continue;
      }
      for (const SuccessorSyntax& successor : block->terminator->successors) {
        if (blocks.find(successor.target) != blocks.end() &&
            reachable.insert(successor.target).second) {
          pending.push_back(successor.target);
        }
      }
    }
    for (const BlkSyntax& block : body.blocks) {
      if (reachable.find(block.name) == reachable.end()) {
        report("unreachable block '" + block.name + "'", block.range);
      }
    }
  }

  return diagnostics.size() == initial_diagnostics;
}

}  // namespace joggle::detail
