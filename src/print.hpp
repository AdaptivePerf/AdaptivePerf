// AdaptivePerf: comprehensive profiling tool based on Linux perf
// Copyright (C) CERN. See LICENSE for details.

#ifndef PRINT_HPP_
#define PRINT_HPP_

#include <string>

namespace aperf {
  extern bool quiet;

  void print_notice();
  void print(std::string message, bool sub = false, bool error = false);
  void print(char *message, int len);
};

#endif
