// AdaptivePerf: comprehensive profiling tool based on Linux perf
// Copyright (C) CERN. See LICENSE for details.

#ifndef ENTRYPOINT_HPP_
#define ENTRYPOINT_HPP_

/**
   AdaptivePerf namespace.
*/
namespace aperf {
  /**
     The version of AdaptivePerf.
  */
  extern const char *version;

  int main_entrypoint(int argc, char **argv);
};

#endif
