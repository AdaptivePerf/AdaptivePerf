// AdaptivePerf: comprehensive profiling tool based on Linux perf
// Copyright (C) CERN. See LICENSE for details.

#include "profilers.hpp"
#include <cstdlib>
#include <future>
#include <iostream>
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <nlohmann/json.hpp>

#ifndef APERF_SCRIPT_PATH
#define APERF_SCRIPT_PATH "."
#endif

namespace aperf {
  /**
     Constructs a PerfEvent object corresponding to thread tree
     profiling.

     Thread tree profiling traces all system calls relevant to
     spawning new threads/processes and exiting from them so that
     a thread/process tree can be created for later analysis.
  */
  PerfEvent::PerfEvent() {
    this->name = "<thread_tree>";
  }

  /**
     Constructs a PerfEvent object corresponding to on-CPU/off-CPU
     profiling.

     @param freq                  An on-CPU sampling frequency in Hz.
     @param off_cpu_freq          An off-CPU sampling frequency in Hz.
                                  0 disables off-CPU profiling.
     @param buffer_events         A number of on-CPU events that
                                  should be buffered before sending
                                  them for post-processing. 1
                                  effectively disables buffering.
     @param buffer_off_cpu_events A number of off-CPU events that
                                  should be buffered before sending
                                  them for post-processing. 0 leaves
                                  the default adaptive buffering, 1
                                  effectively disables buffering.
  */
  PerfEvent::PerfEvent(int freq,
                       int off_cpu_freq,
                       int buffer_events,
                       int buffer_off_cpu_events) {
    this->name = "<main>";
    this->options.push_back(std::to_string(freq));
    this->options.push_back(std::to_string(off_cpu_freq));
    this->options.push_back(std::to_string(buffer_events));
    this->options.push_back(std::to_string(buffer_off_cpu_events));
  }

  /**
     Constructs a PerfEvent object corresponding to a custom
     Linux "perf" event.

     @param name          The name of a "perf" event as displayed by
                          "perf list".
     @param period        A sampling period. The value of X means
                          "do a sample on every X occurrences of the
                          event".
     @param buffer_events A number of events that should be buffered
                          before sending them for post-processing. 1
                          effectively disables buffering.
  */
  PerfEvent::PerfEvent(std::string name,
                       int period,
                       int buffer_events) {
    this->name = name;
    this->options.push_back(std::to_string(period));
    this->options.push_back(std::to_string(buffer_events));
  }

  /**
     Constructs a Perf object.

     @param perf_path  The full path to the "perf" executable.
     @param perf_event The PerfEvent object corresponding to a "perf" event
                       to be used in this "perf" instance.
     @param cpu_config A CPUConfig object describing how CPU cores should
                       be used for profiling.
     @param name       The name of this "perf" instance.
  */
  Perf::Perf(fs::path perf_path,
             PerfEvent &perf_event,
             CPUConfig &cpu_config,
             std::string name) : cpu_config(cpu_config) {
    this->perf_path = perf_path;
    this->perf_event = perf_event;
    this->name = name;
    this->max_stack = 1024;

    this->requirements.push_back(std::make_unique<SysKernelDebugReq>());
    this->requirements.push_back(std::make_unique<PerfEventKernelSettingsReq>(this->max_stack));
    this->requirements.push_back(std::make_unique<NUMAMitigationReq>());
  }

  std::string Perf::get_name() {
    return this->name;
  }

  void Perf::start(pid_t pid,
                   ServerConnInstrs &connection_instrs,
                   fs::path result_out,
                   fs::path result_processed,
                   bool capture_immediately) {
    std::string instrs = connection_instrs.get_instructions(this->get_thread_count());

    fs::path stdout, stderr_record, stderr_script;
    std::vector<std::string> argv_record;
    std::vector<std::string> argv_script;
    std::vector<std::string> env_script;

    if (this->perf_event.name == "<thread_tree>") {
      stdout = result_out / "perf_script_syscall_stdout.log";
      stderr_record = result_out / "perf_record_syscall_stderr.log";
      stderr_script = result_out / "perf_script_syscall_stderr.log";

      argv_record = {"perf", "record", "-o", "-", "--call-graph", "fp", "-k",
                     "CLOCK_MONOTONIC", "--buffer-events", "1", "-e",
                     "syscalls:sys_exit_execve,syscalls:sys_exit_execveat,"
                     "sched:sched_process_fork,sched:sched_process_exit",
                     "--sorted-stream", "--pid=" + std::to_string(pid)};
      argv_script = { "perf", "script", "-s", APERF_SCRIPT_PATH "/adaptiveperf-syscall-process.py",
                      "--demangle", "--demangle-kernel",
                      "--max-stack=" + std::to_string(this->max_stack)};
    } else if (this->perf_event.name == "<main>") {
      stdout = result_out / "perf_script_main_stdout.log";
      stderr_record = result_out / "perf_record_main_stderr.log";
      stderr_script = result_out / "perf_script_main_stderr.log";

      argv_record = {"perf", "record", "-s", "-o", "-", "--call-graph", "fp", "-k",
                     "CLOCK_MONOTONIC", "--sorted-stream", "-e",
                     "task-clock", "-F", this->perf_event.options[0],
                     "--off-cpu", this->perf_event.options[1],
                     "--buffer-events", this->perf_event.options[2],
                     "--buffer-off-cpu-events", this->perf_event.options[3],
                     "--pid=" + std::to_string(pid)};
      argv_script = {"perf", "script","-s", APERF_SCRIPT_PATH "/adaptiveperf-process.py",
                     "-F", "comm,tid,pid,time,event,ip,sym,dso,period",
                     "--ns", "--demangle", "--demangle-kernel",
                     "--max-stack=" + std::to_string(this->max_stack)};
    } else {
      stdout = result_out / ("perf_script_" + this->perf_event.name + "_stdout.log");
      stderr_record = result_out / ("perf_record_" + this->perf_event.name + "_stderr.log");
      stderr_script = result_out / ("perf_script_" + this->perf_event.name + "_stderr.log");

      argv_record = {"perf", "record", "-s", "-o", "-", "--call-graph", "fp", "-k",
                     "CLOCK_MONOTONIC", "--sorted-stream", "-e",
                     this->perf_event.name + "/period=" + this->perf_event.options[0] + "/",
                     "--buffer-events", this->perf_event.options[1],
                     "--pid=" + std::to_string(pid)};
      argv_script = {"perf", "script", "-s", APERF_SCRIPT_PATH "/adaptiveperf-process.py",
                     "-F", "comm,tid,pid,time,event,ip,sym,dso,period",
                     "--ns", "--demangle", "--demangle-kernel",
                     "--max-stack=" + std::to_string(this->max_stack)};
    }

    env_script = {"APERF_SERV_CONNECT=" + instrs};

    int pipe_fd[2];

    if (pipe(pipe_fd) == -1) {
      print("Could not establish communication pipe between perf-record "
            "and perf-script in \"" + this->get_name() + "\"! Terminating "
            "the profiled command wrapper.", true, true);
      kill(pid, SIGTERM);
      return;
    }

    const int ERROR_STDOUT = 200;
    const int ERROR_STDERR = 201;
    const int ERROR_STDOUT_DUP2 = 202;
    const int ERROR_STDERR_DUP2 = 203;
    const int ERROR_STDIN_DUP2 = 204;

    pid_t forked_record = fork();

    if (forked_record == 0) {
      // This executes in a separate process with everything effectively copied (NOT shared!)
      close(pipe_fd[0]);

      fs::current_path(result_processed);

      int stderr_fd = creat(stderr_record.c_str(),
                            S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

      if (stderr_fd == -1) {
        std::exit(ERROR_STDERR);
      }

      if (dup2(stderr_fd, STDERR_FILENO) == -1) {
        std::exit(ERROR_STDERR_DUP2);
      }

      if (dup2(pipe_fd[1], STDOUT_FILENO) == -1) {
        std::exit(ERROR_STDOUT_DUP2);
      }

      close(pipe_fd[1]);
      close(stderr_fd);

      char *argv[argv_record.size() + 1];

      for (int i = 0; i < argv_record.size(); i++) {
        argv[i] = (char *)argv_record[i].c_str();
      }

      argv[argv_record.size()] = nullptr;

      execv(this->perf_path.c_str(), argv);

      // This is reached only if execv fails
      std::exit(errno);
    }

    pid_t forked_script = fork();

    if (forked_script == 0) {
      // This executes in a separate process with everything effectively copied (NOT shared!)
      close(pipe_fd[1]);

      fs::current_path(result_processed);

      int stdout_fd = creat(stdout.c_str(),
                            S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

      if (stdout_fd == -1) {
        std::exit(ERROR_STDOUT);
      }

      int stderr_fd = creat(stderr_script.c_str(),
                            S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

      if (stderr_fd == -1) {
        std::exit(ERROR_STDERR);
      }

      if (dup2(pipe_fd[0], STDIN_FILENO) == -1) {
        std::exit(ERROR_STDIN_DUP2);
      }

      if (dup2(stdout_fd, STDOUT_FILENO) == -1) {
        std::exit(ERROR_STDOUT_DUP2);
      }

      if (dup2(stderr_fd, STDERR_FILENO) == -1) {
        std::exit(ERROR_STDERR_DUP2);
      }

      close(pipe_fd[0]);
      close(stdout_fd);
      close(stderr_fd);

      char *argv[argv_script.size() + 1];

      for (int i = 0; i < argv_script.size(); i++) {
        argv[i] = (char *)argv_script[i].c_str();
      }

      argv[argv_script.size()] = nullptr;

      char *env[env_script.size() + 1];

      for (int i = 0; i < env_script.size(); i++) {
        env[i] = (char *)env_script[i].c_str();
      }

      env[env_script.size()] = nullptr;

      execve(this->perf_path.c_str(), argv, env);

      // This is reached only if execve fails
      std::exit(errno);
    }

    close(pipe_fd[0]);
    close(pipe_fd[1]);

    this->process = std::async([=]() {
      int status_record;
      int result = waitpid(forked_record, &status_record, 0);

      if (result != forked_record) {
        print("Could not wait properly for the perf-record process of "
              "\"" + this->get_name() + "\"! Terminating "
              "the profiled command wrapper.", true, true);
        kill(pid, SIGTERM);
        return 2;
      }

      int code = WEXITSTATUS(status_record);

      if (code != 0) {
        int status = waitpid(pid, nullptr, WNOHANG);

        if (status == 0) {
          print("Profiler \"" + this->get_name() + "\" (perf-record) has "
                "returned non-zero exit code " + std::to_string(code) + ". "
                "Terminating the profiled command wrapper.", true, true);
          kill(pid, SIGTERM);
        } else {
          print("Profiler \"" + this->get_name() + "\" (perf-record) "
                "has returned non-zero exit code " + std::to_string(code) + " "
                "and the profiled command "
                "wrapper is no longer running.", true, true);
        }

        std::string hint = "Hint: perf-record wrapper has returned exit "
          "code " + std::to_string(code) + ", suggesting something bad "
          "happened when ";

        switch (code) {
        case ERROR_STDOUT:
          print(hint + "creating stdout log file.", true, true);
          break;

        case ERROR_STDERR:
          print(hint + "creating stderr log file.", true, true);
          break;

        case ERROR_STDOUT_DUP2:
          print(hint + "redirecting stdout to perf-script.", true, true);
          break;

        case ERROR_STDERR_DUP2:
          print(hint + "redirecting stderr to file.", true, true);
          break;
        }

        return code;
      }

      int status_forked;
      result = waitpid(forked_script, &status_forked, 0);

      if (result != forked_script) {
        print("Could not wait properly for the perf-script process of "
              "\"" + this->get_name() + "\"! Terminating "
              "the profiled command wrapper.", true, true);
        kill(pid, SIGTERM);
        return 2;
      }

      code = WEXITSTATUS(status_forked);

      if (code != 0) {
        int status = waitpid(pid, nullptr, WNOHANG);

        if (status == 0) {
          print("Profiler \"" + this->get_name() + "\" (perf-script) "
                "has returned non-zero exit code " + std::to_string(code) + ". "
                "Terminating the profiled command wrapper.", true, true);
          kill(pid, SIGTERM);
        } else {
          print("Profiler \"" + this->get_name() + "\" (perf-script) "
                "has returned non-zero exit code " + std::to_string(code) + " "
                "and the profiled command "
                "wrapper is no longer running.", true, true);
        }

        std::string hint = "Hint: perf-script wrapper has returned exit "
          "code " + std::to_string(code) + ", suggesting something bad "
          "happened when ";

        switch (code) {
        case ERROR_STDOUT:
          print(hint + "creating stdout log file.", true, true);
          break;

        case ERROR_STDERR:
          print(hint + "creating stderr log file.", true, true);
          break;

        case ERROR_STDOUT_DUP2:
          print(hint + "redirecting stdout to file.", true, true);
          break;

        case ERROR_STDERR_DUP2:
          print(hint + "redirecting stderr to file.", true, true);
          break;

        case ERROR_STDIN_DUP2:
          print(hint + "replacing stdin with perf-record pipe output.",
                true, true);
          break;
        }
      }

      return code;
    });
  }

  unsigned int Perf::get_thread_count() {
    if (this->perf_event.name == "<thread_tree>") {
      return 1;
    } else {
      return this->cpu_config.get_profiler_thread_count();
    }
  }

  void Perf::resume() {
    // TODO
  }

  void Perf::pause() {
    // TODO
  }

  int Perf::wait() {
    return this->process.get();
  }

  std::vector<std::unique_ptr<Requirement> > &Perf::get_requirements() {
    return this->requirements;
  }
};
