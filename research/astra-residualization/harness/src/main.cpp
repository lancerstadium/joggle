#include "residual/artifact_encoding.h"
#include "residual/baseline.h"
#include "residual/conditioned.h"
#include "residual/decision_diagram.h"
#include "residual/exploration.h"
#include "residual/materializer.h"
#include "residual/oracle.h"
#include "residual/transition.h"

#include <iostream>
#include <string_view>

int main(const int argc, const char* const* argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--self-test") {
    if (!residual::self_test(std::cerr) ||
        !residual::artifact_encoding_self_test(std::cerr) ||
        !residual::baseline_self_test(std::cerr) ||
        !residual::conditioned_self_test(std::cerr) ||
        !residual::mtbdd_self_test(std::cerr) ||
        !residual::exploration_self_test(std::cerr) ||
        !residual::materializer_self_test(std::cerr) ||
        !residual::transition_self_test(std::cerr)) {
      return 1;
    }
    std::cout << "F1 oracle self-test passed\n";
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--baselines") {
    residual::write_baseline_csv(std::cout);
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--materializer") {
    residual::write_materializer_csv(std::cout);
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--conditioned") {
    residual::write_conditioned_csv(std::cout);
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--mtbdd") {
    residual::write_mtbdd_csv(std::cout);
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--exploration") {
    residual::write_exploration_csv(std::cout);
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--transitions") {
    residual::write_transition_summary(std::cout);
    return 0;
  }
  if (argc != 1) {
    std::cerr
        << "usage: residual_oracle [--self-test|--baselines|--conditioned|"
           "--exploration|--materializer|--mtbdd|--transitions]\n";
    return 2;
  }
  residual::write_csv(std::cout);
  return 0;
}
