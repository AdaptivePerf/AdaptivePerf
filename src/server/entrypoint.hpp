// AdaptivePerf: comprehensive profiling tool based on Linux perf
// Copyright (C) CERN. See LICENSE for details.

#ifndef SERVER_ENTRYPOINT_HPP_
#define SERVER_ENTRYPOINT_HPP_

/**
   AdaptivePerf namespace.
*/
namespace aperf {
#ifndef ENTRYPOINT_HPP_
  /**
     The version of AdaptivePerf.
  */
  extern const char *version;
#endif
  int server_entrypoint(int argc, char **argv);
};

#endif
