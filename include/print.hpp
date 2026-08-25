// SPDX-FileCopyrightText: 2026 CERN
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef PRINT_HPP_
#define PRINT_HPP_

#include "system.hpp"
#include "adaptyst/socket.hpp"
#include <fstream>
#include <mutex>
#include <string>
#include <utility>

namespace adaptyst {
  class Terminal {
  private:
    std::mutex mutex;
    std::mutex log_mutex;
    bool batch;
    bool formatted;
    int last_line_len;
    std::string version;
    std::ofstream main_log;
    std::unique_ptr<FileDescriptor> log_source_fd;
    std::unordered_map<Identifiable *,
                       std::unordered_map<std::string, std::pair<std::shared_ptr<std::mutex>,
                                                                 std::ofstream> > > log_streams;
    fs::path log_dir;
    Terminal(bool batch, bool formatted, std::string version, fs::path log_dir,
             int log_source_fd);

  public:
    static std::unique_ptr<Terminal> instance;
    static void init(bool batch, bool formatted, std::string version,
                     fs::path log_dir, int log_source_fd = -1);

    void print_notice();
    void log(std::string message, Identifiable *source,
             std::string log_type);
    void print(std::string message, bool sub, bool error,
               bool same_line = false);
    void print(std::string message, bool sub, bool error,
               Identifiable *source, std::string log_type);
    const char *get_log_dir();
    bool is_formatted();
    void export_log_types(Identifiable &source);
    void close_log_source_export();
    void set_log_dir(fs::path log_dir);
  };
};

#endif
