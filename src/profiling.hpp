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
#include "server/socket.hpp"

namespace aperf {
  namespace fs = std::filesystem;

  /**
     A class describing a requirement of a profiler that needs to be
     satisfied before the profiler is used.
  */
  class Requirement {
    inline static std::unordered_map<std::type_index, bool> already_checked;

  protected:
    /**
       Determines whether the requirement is satisfied (internal method
       called by check()).

       This is an internal method which should *always* perform the check
       and return its result.
    */
    virtual bool check_internal() = 0;

  public:
    /**
       Gets the name of the requirement (e.g. for diagnostic purposes).
    */
    virtual std::string get_name() = 0;

    /**
       Determines whether the requirement is satisfied.

       On the first call, the check is performed and its result is
       cached. On all subsequent calls, the cached result
       is returned immediately, regardless of how many objects of a
       given Requirement-derived class are constructed.
    */
    bool check() {
      std::type_index index(typeid(*this));
      if (Requirement::already_checked.find(index) ==
          Requirement::already_checked.end()) {
        Requirement::already_checked[index] = this->check_internal();
      }

      return Requirement::already_checked[index];
    }
  };

  /**
     A class describing the configuration of CPU cores for profiling.

     Specifically, CPUConfig describes what cores should be used for
     post-processing + profiling, what cores should be used for running
     the command, what cores should be used for both, and what cores should
     not be used at all.
  */
  class CPUConfig {
  private:
    bool valid;
    int profiler_thread_count;
    cpu_set_t cpu_profiler_set;
    cpu_set_t cpu_command_set;

  public:
    CPUConfig(std::string mask);
    bool is_valid() const;
    int get_profiler_thread_count() const;
    cpu_set_t get_cpu_profiler_set() const;
    cpu_set_t get_cpu_command_set() const;
  };

  /**
     A class describing adaptiveperf-server connection instructions
     for profilers, sent by adaptiveperf-server during the initial
     setup phase.
  */
  class ServerConnInstrs {
  private:
    std::string type;
    std::queue<std::string> methods;

  public:
    ServerConnInstrs(std::string all_connection_instrs);
    std::string get_instructions(int thread_count);
  };

  /**
     A class describing a profiler.
  */
  class Profiler {
  protected:
    std::unique_ptr<Acceptor> acceptor;
    std::unique_ptr<Connection> connection;
    unsigned int buf_size;

  public:
    virtual ~Profiler() { }

    /**
       Gets the name of this profiler instance.
    */
    virtual std::string get_name() = 0;

    /**
       Starts the profiler (and establishes the generic message connection
       if the acceptor is set via set_acceptor()).

       @param pid                 The PID of a process the profiler should
                                  be attached to. This may be left unused by
                                  classes deriving from Profiler.
       @param connection_instrs   adaptiveperf-server connection
                                  instructions, sent by adaptiveperf-server
                                  during the initial setup phase.
       @param result_out          The path to the "out" directory of
                                  results of the current profiling session.
       @param result_processed    The path to the "processed" directory of
                                  results of the current profiling session.
       @param capture_immediately Indicates whether event capturing should
                                  become immediately after starting the profiler.
                                  If set to false, the call to start() must be
                                  followed by the call to resume() at some point.
    */
    virtual void start(pid_t pid,
                       ServerConnInstrs &connection_instrs,
                       fs::path result_out,
                       fs::path result_processed,
                       bool capture_immediately) = 0;

    /**
       Resumes event capturing by the profiler.

       This is used for implementing partial profiling of the command.
    */
    virtual void resume() = 0;

    /**
       Pauses event capturing by the profiler.

       This is used for implementing partial profiling of the command.
    */
    virtual void pause() = 0;

    /**
       Waits for the profiler to finish executing and returns its exit code.
    */
    virtual int wait() = 0;

    /**
       Gets the number of threads the profiler is expected to use.
    */
    virtual unsigned int get_thread_count() = 0;

    /**
       Gets the list of requirements that must be satisfied for the profiler
       to run.
    */
    virtual std::vector<std::unique_ptr<Requirement> > &get_requirements() = 0;

    /**
       Sets the acceptor used for establishing a connection for
       exchanging generic messages with the profiler.

       @param acceptor The acceptor to use.
       @param buf_size The buffer size for a connection that the
                       acceptor will accept.
    */
    void set_acceptor(std::unique_ptr<Acceptor> &acceptor,
                      unsigned int buf_size) {
      this->acceptor = std::move(acceptor);
      this->buf_size = buf_size;
    }

    /**
       Gets the connection used for exchanging generic messages with
       the profiler.

       WARNING: A null pointer will be returned if start() hasn't been
       called before or the acceptor is not set via set_acceptor().
    */
    std::unique_ptr<Connection> &get_connection() {
      return this->connection;
    }
  };

  CPUConfig get_cpu_config(int post_processing_threads, bool external_server);
  int start_profiling_session(std::vector<std::unique_ptr<Profiler> > &profilers,
                              std::vector<std::string> &command_elements,
                              std::string server_address,
                              unsigned int buf_size, unsigned int warmup,
                              CPUConfig &cpu_config, fs::path tmp_dir,
                              std::vector<pid_t> &spawned_children,
                              std::unordered_map<std::string, std::string> &event_dict);
};

#endif
