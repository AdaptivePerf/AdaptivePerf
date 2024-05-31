// AdaptivePerf: comprehensive profiling tool based on Linux perf
// Copyright (C) CERN. See LICENSE for details.

#include "server/entrypoint.hpp"

int main(int argc, char **argv) {
  return server_entrypoint(argc, argv);
}
