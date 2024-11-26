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
    unsigned int buf_size;
#ifdef BOOST_OS_UNIX
    int notify_pipe[2];
    int stdin_pipe[2];
    int stdout_pipe[2];
    int *stdout_fd;
    std::unique_ptr<FileDescriptor> stdout_reader;
#endif
    bool started;
    int id;

  public:
    static const int ERROR_START_PROFILE = 200;
    static const int ERROR_STDOUT = 201;
    static const int ERROR_STDERR = 202;
    static const int ERROR_STDOUT_DUP2 = 203;
    static const int ERROR_STDERR_DUP2 = 204;
    static const int ERROR_AFFINITY = 205;
    static const int ERROR_STDIN_DUP2 = 206;
    static const int ERROR_NOT_FOUND = 207;
    static const int ERROR_NO_ACCESS = 208;

    Process(std::vector<std::string> &command,
            unsigned int buf_size = 1024);
    void add_env(std::string key, std::string value);
    void set_redirect_stdout(fs::path path);
    void set_redirect_stdout(Process &process);
    void set_redirect_stderr(fs::path path);
    int start(bool wait_for_notify,
              CPUConfig &cpu_config,
              bool is_profiler,
              fs::path working_path = fs::current_path());
    void notify();
    std::string read_line();
    void write_stdin(char *buf, unsigned int size);
    int join();
    bool is_running();
    void close_stdin();

    class ReadException : public std::exception { };
    class NotReadableException : public std::exception { };
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
