// AdaptivePerf: comprehensive profiling tool based on Linux perf
// Copyright (C) CERN. See LICENSE for details.

#ifndef REQUIREMENTS_HPP_
#define REQUIREMENTS_HPP_

#include "profiling.hpp"

namespace aperf {
  /**
     A class describing the requirement of /sys/kernel/debug
     being mounted.
  */
  class SysKernelDebugReq : public Requirement {
  protected:
    bool check_internal();

  public:
    std::string get_name();
  };

  /**
     A class describing the requirement of the correct
     "perf"-specific kernel settings.

     These settings are kernel.perf_event_max_stack and
     kernel.perf_event_paranoid.
  */
  class PerfEventKernelSettingsReq : public Requirement {
  private:
    int &max_stack;

  protected:
    bool check_internal();

  public:
    PerfEventKernelSettingsReq(int &max_stack);
    std::string get_name();
  };

  /**
     A class describing the requirement of having proper
     NUMA-specific mitigations.

     The behaviour of this class depends on whether
     AdaptivePerf is compiled with libnuma support.
  */
  class NUMAMitigationReq : public Requirement {
  protected:
    bool check_internal();

  public:
    std::string get_name();
  };
};

#endif
