// AdaptivePerf: comprehensive profiling tool based on Linux perf
// Copyright (C) CERN. See LICENSE for details.

#ifndef CMD_HPP_
#define CMD_HPP_

#include <CLI/CLI.hpp>
#include <vector>
#include <boost/algorithm/string.hpp>

#define COLUMN_WIDTH 35

namespace aperf {
  /**
     A class ensuring that the help message is formatted in
     an eye-friendly way complying with the 80-character limit.
  */
  class PrettyFormatter : public CLI::Formatter {
  public:
    PrettyFormatter() {
      this->column_width(COLUMN_WIDTH);
    }

    std::string make_option_desc(const CLI::Option *opt) const override {
      std::string result = "";

      std::vector<std::string> lines;
      boost::split(lines, opt->get_description(), boost::is_any_of("\n"));

      for (int i = 0; i < lines.size(); i++) {
        bool finish_with_new_line = false;

        if (!lines[i].empty()) {
          std::vector<std::string> parts;
          boost::split(parts, lines[i], boost::is_any_of(" "));

          int characters_printed_in_line = 0;

          for (int j = 0; j < parts.size(); j++) {
            std::string to_print = parts[j];

            if (j < parts.size() - 1) {
              to_print += " ";
            }

            if (characters_printed_in_line > 0 &&
                characters_printed_in_line + to_print.length() >=
                80 - COLUMN_WIDTH) {
              result += "\n";
              characters_printed_in_line = 0;
              finish_with_new_line = true;
            }

            result += to_print;
            characters_printed_in_line += to_print.length();
          }
        }

        if (i < lines.size() - 1 || finish_with_new_line) {
          result += "\n";
        }
      }

      return result;
    }
  };
};

#endif
