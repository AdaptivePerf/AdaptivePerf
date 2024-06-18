// AdaptivePerf: comprehensive profiling tool based on Linux perf
// Copyright (C) CERN. See LICENSE for details.

#ifndef REQUIREMENTS_HPP_
#define REQUIREMENTS_HPP_

#include "profiling.hpp"

namespace aperf {
  class SysKernelDebugReq : public Requirement {
  protected:
    bool check_internal();

  public:
    std::string get_name();
  };

  class PerfEventKernelSettingsReq : public Requirement {
  private:
    int &max_stack;

  protected:
    bool check_internal();

  public:
    PerfEventKernelSettingsReq(int &max_stack);
    std::string get_name();
  };

  class NUMAMitigationReq : public Requirement {
  protected:
    bool check_internal();

  public:
    std::string get_name();
  };
};

#endif
