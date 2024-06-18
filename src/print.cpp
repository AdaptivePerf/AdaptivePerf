// AdaptivePerf: comprehensive profiling tool based on Linux perf
// Copyright (C) CERN. See LICENSE for details.

#include "print.hpp"
#include <iostream>
#include <boost/algorithm/string.hpp>
#include <mutex>

namespace aperf {
  std::mutex print_mutex;

  void print_notice() {
    if (quiet) {
      return;
    }

    std::unique_lock lock(print_mutex);

    std::cout << "AdaptivePerf: comprehensive profiling tool based on Linux perf" << std::endl;
    std::cout << "Copyright (C) CERN." << std::endl;
    std::cout << std::endl;
    std::cout << "This program is free software; you can redistribute it and/or" << std::endl;
    std::cout << "modify it under the terms of the GNU General Public License" << std::endl;
    std::cout << "as published by the Free Software Foundation; only version 2." << std::endl;
    std::cout << std::endl;
    std::cout << "This program is distributed in the hope that it will be useful," << std::endl;
    std::cout << "but WITHOUT ANY WARRANTY; without even the implied warranty of" << std::endl;
    std::cout << "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the" << std::endl;
    std::cout << "GNU General Public License for more details." << std::endl;
    std::cout << std::endl;
    std::cout << "You should have received a copy of the GNU General Public License" << std::endl;
    std::cout << "along with this program; if not, write to the Free Software" << std::endl;
    std::cout << "Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston," << std::endl;
    std::cout << "MA 02110-1301, USA." << std::endl;
    std::cout << std::endl;
  }

  void print(std::string message, bool sub, bool error) {
    if (quiet) {
      return;
    }

    std::unique_lock lock(print_mutex);

    if (sub) {
      std::cout << (error ? "\033[0;31m" : "\033[0;34m") << "->";
    } else {
      std::cout << (error ? "\033[1;31m" : "\033[1;32m") << "==>";
    }

    std::vector<std::string> parts;
    boost::split(parts, message, boost::is_any_of(" "));

    int characters_printed_in_line = 0;

    for (auto &part : parts) {
      if (characters_printed_in_line > 0 &&
          characters_printed_in_line + part.length() + 1 >= 75) {
        std::cout << std::endl << (sub ? "  " : "   ");
        characters_printed_in_line = 0;
      }

      std::cout << " " << part;
      characters_printed_in_line += part.length() + 1;
    }

    std::cout << "\033[0m" << std::endl;
  }

  void print(char *message, int len) {
    if (quiet) {
      return;
    }

    std::unique_lock lock(print_mutex);
    std::cout.write(message, len);
  }
};
