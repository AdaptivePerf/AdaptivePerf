// AdaptivePerf: comprehensive profiling tool based on Linux perf
// Copyright (C) CERN. See LICENSE for details.

#ifndef PROFILERS_HPP_
#define PROFILERS_HPP_

#include "profiling.hpp"
#include "requirements.hpp"
#include "print.hpp"
#include "server/server.hpp"
#include <regex>
#include <filesystem>
#include <future>
#include <cstdlib>
#include <unordered_map>
#include <unordered_set>

namespace aperf {


   /**
     A class describing metric reader profiler.
  */
  class MetricReader : public Profiler{

    private:
      std::string metric_command;
      std::string metric_name;
      std::string regex;
      int freq;
      unsigned int server_buffer;
      std::string name;
      std::vector<std::unique_ptr<Requirement>> requirements;
      std::future<int> process;
      std::map<const int, std::string> errorMessages;

    public:
      MetricReader(std::string metric_command,
      std::string metric_name,
      int freq,
      std::string regex,
      unsigned int server_buffer);

      ~MetricReader() {}

      void start(pid_t pid,
                  ServerConnInstrs &connection_instrs,
                  fs::path result_out,
                  fs::path result_processed,
                  bool capture_immediately);
      unsigned int get_thread_count();
      void resume();
      void pause();
      int wait();
      int handle_errors(int code, pid_t pid);
      std::string get_name();
      std::vector<std::unique_ptr<Requirement> > &get_requirements();
  };




  namespace fs = std::filesystem;

  /**
     A class describing a Linux "perf" event, used
     by the Perf class.
  */
  class PerfEvent {
  private:
    std::string name;
    std::vector<std::string> options;

  public:
    friend class Perf;

    // For thread tree profiling
    PerfEvent();

    // For main profiling
    PerfEvent(int freq,
              int off_cpu_freq,
              int buffer_events,
              int buffer_off_cpu_events);

    // For custom event profiling
    PerfEvent(std::string name,
              int period,
              int buffer_events);
  };

  /**
     A class describing a Linux "perf" profiler.
  */
  class Perf : public Profiler {
  private:
    fs::path perf_path;
    std::future<int> process;
    PerfEvent perf_event;
    CPUConfig &cpu_config;
    std::string name;
    std::vector<std::unique_ptr<Requirement> > requirements;
    int max_stack;

  public:
    Perf(fs::path perf_path,
         PerfEvent &perf_event,
         CPUConfig &cpu_config,
         std::string name);
    ~Perf() {}
    std::string get_name();
    void start(pid_t pid,
               ServerConnInstrs &connection_instrs,
               fs::path result_out,
               fs::path result_processed,
               bool capture_immediately);
    unsigned int get_thread_count();
    void resume();
    void pause();
    int wait();
    std::vector<std::unique_ptr<Requirement> > &get_requirements();
  };
};

#endif
