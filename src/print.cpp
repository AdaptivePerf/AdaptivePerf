// SPDX-FileCopyrightText: 2026 CERN
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "print.hpp"
#include <iostream>
#include <mutex>
#include <sstream>
#include <unistd.h>

namespace adaptyst {
  std::unique_ptr<Terminal> Terminal::instance = nullptr;

  void Terminal::init(bool batch, bool formatted, std::string version,
                      fs::path log_dir, int log_source_fd) {
    if (Terminal::instance) {
      throw std::runtime_error("Only one instance of Terminal can be constructed!");
    }

    Terminal::instance = std::unique_ptr<Terminal>(new Terminal(batch, formatted,
                                                                version, log_dir,
                                                                log_source_fd));
  }

  Terminal::Terminal(bool batch, bool formatted, std::string version,
                     fs::path log_dir, int log_source_fd) {
    this->batch = batch;
    this->formatted = formatted;
    this->version = version;
    this->last_line_len = 0;

    if (log_source_fd != -1) {
      int write_fd[2] = {-1, log_source_fd};
      this->log_source_fd = std::make_unique<FileDescriptor>(nullptr, write_fd, 0);
    }

    if (!fs::exists(log_dir)) {
      try {
        fs::create_directories(log_dir);
      } catch (std::exception &e) {
        throw std::runtime_error("Could not create " + log_dir.string() + "!");
      }
    }

    this->log_dir = fs::canonical(log_dir);
    if (!this->batch) {
      this->main_log.open(this->log_dir / "Adaptyst.log");
      if (!this->main_log) {
        throw std::runtime_error("Could not create the main Adaptyst log!");
      }
    }
  }

  /**
     Prints the version and licensing notice.
  */
  void Terminal::print_notice() {
    if (!this->batch) {
      return;
    }

    {
      std::unique_lock lock(this->mutex);

      std::cout << "Adaptyst " << this->version << std::endl;
      std::cout << "Copyright (C) CERN. Core licensed under GNU LGPL v3+." << std::endl;
      std::cout << std::endl;
    }
  }

  void Terminal::log(std::string message, Identifiable *source,
                     std::string log_type) {
    std::ofstream *stream = nullptr;
    std::shared_ptr<std::mutex> stream_mutex;

    {
      std::unique_lock lock(this->log_mutex);

      if (log_streams.find(source) == log_streams.end()) {
        log_streams[source] = std::unordered_map<std::string,
                                                 std::pair<std::shared_ptr<std::mutex>,
                                                           std::ofstream> >();
      }

      if (log_streams[source].find(log_type) == log_streams[source].end()) {
        fs::path path = source->get_path(this->log_dir);

        if (!fs::exists(path) && !fs::create_directories(path)) {
          throw std::runtime_error("Could not create " + path.string());
        }

        log_streams[source][log_type] = std::make_pair(std::make_shared<std::mutex>(),
                                                       std::ofstream(path / (log_type + ".log")));
      }

      stream_mutex = log_streams[source][log_type].first;
      stream = &log_streams[source][log_type].second;
    }

    if (!stream) {
      throw std::runtime_error("Logging " + log_type + " of "
                               + source->get_name() +
                               ": no output stream found");
    }

    {
      std::unique_lock lock(*stream_mutex);

      if (!*stream) {
        throw std::runtime_error("Logging " + log_type + " of "
                                 + source->get_name() +
                                 ": I/O error");
      }

      *stream << message << std::endl;
      stream->flush();
    }

  }

  /**
     Prints a message.

     @param message   A string to be printed.
     @param sub       Indicates whether this message belongs to
                      a subsection (i.e. whether it should be printed
                      with the "->" prefix instead of "==>").
     @param error     Indicates whether this message is an error.
     @param same_line Indicates whether this message should be printed
                      in the current terminal line rather than in a
                      new line. Ignored if the batch mode is enabled.
  */
  void Terminal::print(std::string message, bool sub, bool error,
                       bool same_line) {
    std::unique_lock lock(this->mutex);

    if (!this->batch) {
      std::stringstream stream(message);
      std::string line;
      bool first = true;

      while (std::getline(stream, line)) {
        if (first) {
          this->main_log << (error ? "[ERROR] " : "")
                         << (sub ? "-> " : "==> ");
        }
        this->main_log << line << '\n';
        first = false;
      }

      if (first) {
        this->main_log << (error ? "[ERROR] " : "")
                       << (sub ? "-> " : "==> ") << '\n';
      }

      this->main_log.flush();
      return;
    }

    int new_len = 0;

    if (same_line && !this->batch) {
      std::cout << "\r";
    }

    if (sub) {
      std::cout << (this->formatted ? (error ? "\033[0;31m" : "\033[0;36m") : "") << "-> ";
      new_len += 3;
    } else {
      std::cout << (this->formatted ? (error ? "\033[1;31m" : "\033[1;32m") : "") << "==> ";
      new_len += 4;
    }

    std::cout << message << (this->formatted ? "\033[0m" : "");
    new_len += message.length();

    if (same_line && !this->batch) {
      for (int i = 0; i < this->last_line_len - new_len; i++) {
        std::cout << " ";
      }

      std::cout.flush();
    } else {
      std::cout << std::endl;
    }

    this->last_line_len = new_len;
  }

  void Terminal::print(std::string message, bool sub, bool error,
                       Identifiable *source, std::string log_type) {
    this->log((error ? "[ERROR] " : std::string()) + (sub ? "-> " : "==> ") + message,
              source, log_type);
  }

  const char *Terminal::get_log_dir() {
    return this->log_dir.c_str();
  }

  bool Terminal::is_formatted() {
    return this->formatted;
  }

  void Terminal::export_log_types(Identifiable &source) {
    if (this->batch || !this->log_source_fd) {
      return;
    }

    for (auto &log_type : source.get_log_types()) {
      fs::path path = source.get_path(this->log_dir) / (log_type + ".log");
      std::string path_str = path.string();

      this->log_source_fd->write(path_str, true);
    }
  }

  void Terminal::close_log_source_export() {
    if (this->log_source_fd) {
      this->log_source_fd->close();
      this->log_source_fd.reset(nullptr);
    }
  }

  void Terminal::set_log_dir(fs::path log_dir) {
    this->log_dir = log_dir;
  }
};
