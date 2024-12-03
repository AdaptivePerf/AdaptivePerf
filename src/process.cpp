// AdaptivePerf: comprehensive profiling tool based on Linux perf
// Copyright (C) CERN. See LICENSE for details.

#include "process.hpp"
#include <boost/algorithm/string.hpp>

#ifdef BOOST_OS_UNIX
#include <sys/wait.h>
#endif

namespace aperf {
  Process::Process(std::vector<std::string> &command,
                   unsigned int buf_size) {
    if (command.empty()) {
      throw Process::EmptyCommandException();
    }

    this->command = command;
    this->stdout_redirect = false;
    this->stderr_redirect = false;
    this->notifiable = false;
    this->started = false;
    this->writable = true;
    this->buf_size = buf_size;

#ifdef BOOST_OS_UNIX
    this->stdout_fd = nullptr;
    this->notify_pipe[0] = -1;
    this->notify_pipe[1] = -1;
    this->stdin_pipe[0] = -1;
    this->stdin_pipe[1] = -1;
    this->stdout_pipe[0] = -1;
    this->stdout_pipe[1] = -1;
#endif
  }

  Process::~Process() {
    if (this->started) {
#ifdef BOOST_OS_UNIX
      if (this->writable &&
          this->stdin_writer.get() != nullptr) {
        this->stdin_writer->close();
      }

      if (this->notifiable) {
        close(this->notify_pipe[1]);
      }

      waitpid(this->id, nullptr, 0);
#endif
    }
  }

  void Process::add_env(std::string key, std::string value) {
    this->env.push_back(std::make_pair(key, value));
  }

  void Process::set_redirect_stdout(fs::path path) {
    this->stdout_redirect = true;
    this->stdout_path = path;
  }

  void Process::set_redirect_stdout(Process &process) {
    this->stdout_redirect = true;

#ifdef BOOST_OS_UNIX
    this->stdout_fd = &process.stdin_pipe[1];
    process.writable = false;
#else
    this->stdout_redirect = false;
    throw Process::NotImplementedException();
#endif
  }

  void Process::set_redirect_stderr(fs::path path) {
    this->stderr_redirect = true;
    this->stderr_path = path;
  }

  int Process::start(bool wait_for_notify,
                     const CPUConfig &cpu_config,
                     bool is_profiler,
                     fs::path working_path) {
    if (wait_for_notify) {
      this->notifiable = true;
    }

#ifdef BOOST_OS_UNIX
    if (this->stdout_redirect && this->stdout_fd != nullptr &&
        *(this->stdout_fd) == -1) {
      throw Process::StartException();
    }

    std::vector<std::string> env_entries;
    char **cur_existing_env_entry = environ;

    while (*cur_existing_env_entry != nullptr) {
      env_entries.push_back(std::string(*cur_existing_env_entry));
      cur_existing_env_entry++;
    }

    for (int i = 0; i < this->env.size(); i++) {
      env_entries.push_back(this->env[i].first + "=" + this->env[i].second);
    }

    if (this->notifiable && pipe(this->notify_pipe) == -1) {
      throw Process::StartException();
    }

    if (!this->stdout_redirect) {
      if (pipe(this->stdout_pipe) == -1) {
        if (this->notifiable) {
          close(this->notify_pipe[0]);
          close(this->notify_pipe[1]);
          this->notifiable = false;
        }

        throw Process::StartException();
      }

      this->stdout_reader = std::make_unique<FileDescriptor>(this->stdout_pipe,
                                                             nullptr,
                                                             this->buf_size);
    }

    if (pipe(this->stdin_pipe) == -1) {
      if (this->notifiable) {
        close(this->notify_pipe[0]);
        close(this->notify_pipe[1]);
        this->notifiable = false;
      }

      if (!this->stdout_redirect) {
        close(this->stdout_pipe[0]);
        close(this->stdout_pipe[1]);
      }

      throw Process::StartException();
    }

    if (this->writable) {
      this->stdin_writer = std::make_unique<FileDescriptor>(nullptr,
                                                            this->stdin_pipe,
                                                            this->buf_size);
    }

    pid_t forked = fork();

    if (forked == 0) {
      // This executed in a separate process with everything effectively
      // copied (NOT shared!)

      if (this->notifiable) {
        close(this->notify_pipe[1]);
        char buf;
        int bytes_read = 0;
        int received = ::read(this->notify_pipe[0], &buf, 1);
        close(this->notify_pipe[0]);

        if (received <= 0 || buf != 0x03) {
          std::exit(Process::ERROR_START_PROFILE);
        }
      }

      close(this->stdin_pipe[1]);
      close(this->stdout_pipe[0]);

      fs::current_path(working_path);

      if (this->stderr_redirect) {
        int stderr_fd = creat(this->stderr_path.c_str(),
                              S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

        if (stderr_fd == -1) {
          std::exit(Process::ERROR_STDERR);
        }

        if (dup2(stderr_fd, STDERR_FILENO) == -1) {
          std::exit(Process::ERROR_STDERR_DUP2);
        }

        close(stderr_fd);
      }

      if (this->stdout_redirect) {
        int stdout_fd;

        if (this->stdout_fd == nullptr) {
          stdout_fd = creat(this->stdout_path.c_str(),
                                S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

          if (stdout_fd == -1) {
            std::exit(Process::ERROR_STDOUT);
          }
        } else {
          stdout_fd = *(this->stdout_fd);
        }

        if (dup2(stdout_fd, STDOUT_FILENO) == -1) {
          std::exit(Process::ERROR_STDOUT_DUP2);
        }

        close(stdout_fd);
      } else {
        if (dup2(this->stdout_pipe[1], STDOUT_FILENO) == -1) {
          std::exit(Process::ERROR_STDOUT_DUP2);
        }

        close(this->stdout_pipe[1]);
      }

      if (dup2(this->stdin_pipe[0], STDIN_FILENO) == -1) {
        std::exit(Process::ERROR_STDIN_DUP2);
      }

      close(this->stdin_pipe[0]);

      char *argv[this->command.size() + 1];

      for (int i = 0; i < this->command.size(); i++) {
        argv[i] = (char *)this->command[i].c_str();
      }

      argv[this->command.size()] = nullptr;

      char *env[env_entries.size() + 1];

      for (int i = 0; i < env_entries.size(); i++) {
        env[i] = (char *)env_entries[i].c_str();
      }

      env[env_entries.size()] = nullptr;

      cpu_set_t affinity = is_profiler ? cpu_config.get_cpu_profiler_set() :
        cpu_config.get_cpu_command_set();

      if (sched_setaffinity(0, sizeof(affinity), &affinity) == -1) {
        std::exit(Process::ERROR_AFFINITY);
      }

      execvpe(this->command[0].c_str(), argv, env);

      // This is reached only if execvpe fails
      switch (errno) {
      case ENOENT:
        std::exit(Process::ERROR_NOT_FOUND);

      case EACCES:
        std::exit(Process::ERROR_NO_ACCESS);

      default:
        std::exit(errno);
      }
    }

    if (this->notifiable) {
      close(this->notify_pipe[0]);
    }

    close(this->stdin_pipe[0]);

    if (this->stdout_redirect && this->stdout_fd != nullptr) {
      close(*(this->stdout_fd));
    } else if (!this->stdout_redirect) {
      close(this->stdout_pipe[1]);
    }

    if (forked == -1) {
      if (this->notifiable) {
        close(this->notify_pipe[1]);
        this->notifiable = false;
      }

      throw Process::StartException();
    }

    this->started = true;
    this->id = forked;
    return forked;
#else
    this->notifiable = false;
    throw Process::NotImplementedException();
#endif
  }

  void Process::notify() {
    if (this->started) {
      if (this->notifiable) {
#ifdef BOOST_OS_UNIX
        FileDescriptor notify_writer(nullptr, this->notify_pipe,
                                     this->buf_size);
        char to_send = 0x03;
        notify_writer.write(1, &to_send);
        this->notifiable = false;
#else
        throw Process::NotImplementedException();
#endif
      } else {
        throw Process::NotNotifiableException();
      }
    } else {
      throw Process::NotStartedException();
    }
  }

  std::string Process::read_line() {
    if (this->stdout_redirect) {
      throw Process::NotReadableException();
    }

#ifdef BOOST_OS_UNIX
    return this->stdout_reader->read();
#else
    throw Process::NotImplementedException();
#endif
  }

  void Process::write_stdin(char *buf, unsigned int size) {
    if (this->started) {
      if (this->writable) {
#ifdef BOOST_OS_UNIX
        this->stdin_writer->write(size, buf);
#else
        throw Process::NotImplementedException();
#endif
      } else {
        throw Process::NotWritableException();
      }
    } else {
      throw Process::NotStartedException();
    }
  }

  int Process::join() {
    if (this->started) {
#ifdef BOOST_OS_UNIX
      int status;
      int result = waitpid(this->id, &status, 0);

      if (result != this->id) {
        throw Process::WaitException();
      }

      this->started = false;
      this->notifiable = false;

      return WEXITSTATUS(status);
#else
      throw Process::NotImplementedException();
#endif
    } else {
      throw Process::NotStartedException();
    }
  }

  bool Process::is_running() {
    if (!this->started) {
      return false;
    }

#ifdef BOOST_OS_UNIX
    return waitpid(this->id, nullptr, WNOHANG) == 0;
#else
    throw Process::NotImplementedException();
#endif
  }

  void Process::close_stdin() {
    if (!this->writable) {
      throw Process::NotWritableException();
    }

#ifdef BOOST_OS_UNIX
    this->stdin_writer->close();
    this->writable = false;
#else
    throw Process:NotImplementedException();
#endif
  }
};
