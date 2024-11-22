// AdaptivePerf: comprehensive profiling tool based on Linux perf
// Copyright (C) CERN. See LICENSE for details.

#ifndef PROCESS_HPP_
#define PROCESS_HPP_

#include "profiling.hpp"
#include <vector>
#include <string>
#include <filesystem>
#include <boost/predef.h>

namespace aperf {
  namespace fs = std::filesystem;

  class Process {
  private:
    std::vector<std::string> command;
    std::vector<std::pair<std::string, std::string> > env;
    bool stdout_redirect;
    fs::path stdout_path;
    bool stderr_redirect;
    fs::path stderr_path;
    bool notifiable;
    bool writable;
#ifdef BOOST_OS_UNIX
    int notify_pipe[2];
    int stdin_pipe[2];
    int stdout_fd;
#endif
    bool started;
    int id;

  public:
    const int ERROR_START_PROFILE = 200;
    const int ERROR_STDOUT = 201;
    const int ERROR_STDERR = 202;
    const int ERROR_STDOUT_DUP2 = 203;
    const int ERROR_STDERR_DUP2 = 204;
    const int ERROR_AFFINITY = 205;
    const int ERROR_STDIN_DUP2 = 206;

    Process(std::vector<std::string> &command);
    void add_env(std::string &key, std::string &value);
    void set_redirect_stdout(fs::path &path);
    void set_redirect_stdout(Process &process);
    void set_redirect_stderr(fs::path &path);
    int start(bool wait_for_notify,
              CPUConfig &cpu_config,
              fs::path working_path = fs::current_path());
    void notify();
    void write_stdin(char *buf, unsigned int size);
    int join();

    class NotWritableException : public std::exception { };
    class WriteException : public std::exception { };
    class StartException : public std::exception { };
    class EmptyCommandException : public std::exception { };
    class WaitException : public std::exception { };
    class NotStartedException : public std::exception { };
    class NotNotifiableException : public std::exception { };
    class NotImplementedException : public std::exception { };
  };
};

#endif
