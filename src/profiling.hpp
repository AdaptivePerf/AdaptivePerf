// AdaptivePerf: comprehensive profiling tool based on Linux perf
// Copyright (C) CERN. See LICENSE for details.

#ifndef PROFILING_HPP_
#define PROFILING_HPP_

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <typeindex>
#include <typeinfo>
#include <filesystem>
#include <queue>
#include <sched.h>
#include <thread>

namespace aperf {
  namespace fs = std::filesystem;

  class Requirement {
    inline static std::unordered_map<std::type_index, bool> already_checked;

  protected:
    virtual bool check_internal() = 0;

  public:
    virtual std::string get_name() = 0;
    bool check() {
      std::type_index index(typeid(*this));
      if (Requirement::already_checked.find(index) ==
          Requirement::already_checked.end()) {
        Requirement::already_checked[index] = this->check_internal();
      }

      return Requirement::already_checked[index];
    }
  };

  class CPUConfig {
  private:
    bool valid;
    int profiler_thread_count;
    cpu_set_t cpu_profiler_set;
    cpu_set_t cpu_command_set;

  public:
    // mask[i] = ' ' means the i-th core is not used
    // mask[i] = 'p' means the i-th core is used for post-processing and profilers
    // mask[i] = 'c' means the i-th core is used for the profiled command
    // mask[i] = 'b' means the i-th core is used for everything
    CPUConfig(std::string mask);
    bool is_valid();
    int get_profiler_thread_count();
    cpu_set_t &get_cpu_profiler_set();
    cpu_set_t &get_cpu_command_set();
  };

  class ServerConnInstrs {
  private:
    std::string type;
    std::queue<std::string> methods;

  public:
    ServerConnInstrs(std::string all_connection_instrs);
    std::string get_instructions(int thread_count);
  };

  class Profiler {
  public:
    virtual ~Profiler() { }
    virtual std::string get_name() = 0;
    virtual void start(pid_t pid,
                       ServerConnInstrs &connection_instrs,
                       fs::path result_out,
                       fs::path result_processed,
                       bool capture_immediately) = 0;
    virtual void resume() = 0;
    virtual void pause() = 0;
    virtual int wait() = 0;
    virtual unsigned int get_thread_count() = 0;
    virtual std::vector<std::unique_ptr<Requirement> > &get_requirements() = 0;
  };

  class Child {
  private:
    bool is_thread;
    union {
      pid_t pid;
      std::thread thread;
    } data;
  };

  CPUConfig get_cpu_config(int post_processing_threads, bool external_server);
  int start_profiling_session(std::vector<std::unique_ptr<Profiler> > &profilers,
                              std::string command, std::string server_address,
                              unsigned int buf_size, unsigned int warmup,
                              CPUConfig &cpu_config, fs::path tmp_dir,
                              std::vector<pid_t> &spawned_children);
};

#endif
