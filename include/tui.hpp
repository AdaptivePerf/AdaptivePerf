// SPDX-FileCopyrightText: 2026 CERN
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef TUI_HPP_
#define TUI_HPP_

#include <memory>
#include <string>

namespace adaptyst {
  class Terminal;

  class Tui {
  private:
    class Impl;
    std::unique_ptr<Impl> impl;

  public:
    Tui(Terminal &terminal, std::string version, int log_source_fd,
        unsigned int buf_size, unsigned int max_read_size,
        int max_lines);
    ~Tui();
    bool run();
    void finish(bool successful);
  };
};

#endif
