// AdaptivePerf: comprehensive profiling tool based on Linux perf
// Copyright (C) CERN. See LICENSE for details.

#ifndef PRINT_HPP_
#define PRINT_HPP_

#include <string>

namespace aperf {
  /**
     Indicates whether the quiet mode is enabled.

     If the value of this is true, print_notice() and print() will not
     print anything under any circumstances.
  */
  extern bool quiet;

  void print_notice();
  void print(std::string message, bool sub = false, bool error = false);
};

#endif
