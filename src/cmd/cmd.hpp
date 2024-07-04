#ifndef CMD_HPP_
#define CMD_HPP_

#include <CLI/CLI.hpp>
#include <vector>
#include <boost/algorithm/string.hpp>

#define COLUMN_WIDTH 35

namespace aperf {
  class PrettyFormatter : public CLI::Formatter {
  public:
    PrettyFormatter() {
      this->column_width(COLUMN_WIDTH);
    }

    std::string make_option_desc(const CLI::Option *opt) const override {
      std::vector<std::string> parts;
      boost::split(parts, opt->get_description(), boost::is_any_of(" "));

      int characters_printed_in_line = 0;
      std::string result = "";

      bool finish_with_new_line = false;

      for (int i = 0; i < parts.size(); i++) {
        std::string to_print = parts[i];

        if (i < parts.size() - 1) {
          to_print += " ";
        }

        if (characters_printed_in_line > 0 &&
            characters_printed_in_line + to_print.length() >= 80 - COLUMN_WIDTH) {
          result += "\n";
          characters_printed_in_line = 0;
          finish_with_new_line = true;
        }

        result += to_print;
        characters_printed_in_line += to_print.length();
      }

      return result + (finish_with_new_line ? "\n" : "");
    }
  };
};

#endif
