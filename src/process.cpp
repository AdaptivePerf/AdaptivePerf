// AdaptivePerf: comprehensive profiling tool based on Linux perf
// Copyright (C) CERN. See LICENSE for details.

#include "process.hpp"
#include <boost/algorithm/string.hpp>

#ifdef BOOST_OS_UNIX
#include <sys/wait.h>
#endif

namespace aperf {
  Process::Process(std::vector<std::string> &command) {
    if (command.empty()) {
      throw Process::EmptyCommandException();
    }

    this->command = command;
    this->stdout_redirect = false;
    this->stderr_redirect = false;
    this->notifiable = false;
    this->started = false;
    this->writable = true;

#ifdef BOOST_OS_UNIX
    this->stdout_fd = -1;
#endif
  }

  void Process::add_env(std::string &key, std::string &value) {
    this->env.push_back(std::make_pair(key, value));
  }

  void Process::set_redirect_stdout(fs::path &path) {
    this->stdout_redirect = true;
    this->stdout_path = path;
  }

  void Process::set_redirect_stdout(Process &process) {
    this->stdout_redirect = true;

#ifdef BOOST_OS_UNIX
    this->stdout_fd = process.stdin_pipe[1];
    process.writable = false;
#else
    this->stdout_redirect = false;
    throw Process::NotImplementedException();
#endif
  }

  void Process::set_redirect_stderr(fs::path &path) {
    this->stderr_redirect = true;
    this->stderr_path = path;
  }

  int Process::start(bool wait_for_notify,
                     CPUConfig &cpu_config,
                     fs::path working_path) {
    if (wait_for_notify) {
      this->notifiable = true;
    }

#ifdef BOOST_OS_UNIX
    std::vector<std::string> env_entries;

    for (int i = 0; i < this->env.size(); i++) {
      env_entries.push_back(this->env[i].first + "=\"" +
                            boost::replace_all_copy(this->env[i].second,
                                                    "\"", "\\\""));
    }

    if (this->notifiable && pipe(this->notify_pipe) == -1) {
      throw Process::StartException();
    }

    if (pipe(this->stdin_pipe) == -1) {
      if (this->notifiable) {
        close(this->notify_pipe[0]);
        close(this->notify_pipe[1]);
        this->notifiable = false;
      }

      throw Process::StartException();
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
        close(this->notify_pipe[1]);

        if (received <= 0 || buf != 0x03) {
          std::exit(Process::ERROR_START_PROFILE);
        }
      }

      close(this->stdin_pipe[1]);

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

        if (this->stdout_fd == -1) {
          stdout_fd = creat(this->stdout_path.c_str(),
                                S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

          if (stdout_fd == -1) {
            std::exit(Process::ERROR_STDOUT);
          }
        } else {
          stdout_fd = this->stdout_fd;
        }

        if (dup2(stdout_fd, STDOUT_FILENO) == -1) {
          std::exit(Process::ERROR_STDOUT_DUP2);
        }

        close(stdout_fd);
      }

      if (dup2(this->stdin_pipe[0], STDIN_FILENO) == -1) {
        std::exit(Process::ERROR_STDIN_DUP2);
      }

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

      cpu_set_t &cpu_set = cpu_config.get_cpu_command_set();

      if (sched_setaffinity(0, sizeof(cpu_set), &cpu_set) == -1) {
        std::exit(Process::ERROR_AFFINITY);
      }

      execvpe(this->command[0].c_str(), argv, env);

      // This is reached only if execvpe fails
      std::exit(errno);
    }

    if (this->notifiable) {
      close(this->notify_pipe[0]);
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
        char to_send = 0x03;
        int written = 0;
        bool all_written = false;
        int retries = 0;

        while (!all_written &&
               (written = ::write(this->notify_pipe[1], &to_send, 1)) != -1) {
          if (written == 0) {
            if (retries >= 5) {
              throw Process::WriteException();
            }

            retries++;
          }

          all_written = true;
        }

        close(this->notify_pipe[1]);
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

  void Process::write_stdin(char *buf, unsigned int size) {
    if (this->started) {
      if (this->writable) {
#ifdef BOOST_OS_UNIX
        int written = 0;
        int total_written = 0;
        int retries = 0;

        while (total_written < size &&
               (written = ::write(this->stdin_pipe[1], buf, size)) != -1) {
          if (written == 0) {
            if (retries >= 5) {
              throw Process::WriteException();
            }

            retries++;
          } else {
            retries = 0;
          }

          total_written += written;
        }

        if (written == -1) {
          throw Process::WriteException();
        }
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
};
