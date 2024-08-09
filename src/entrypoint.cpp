// AdaptivePerf: comprehensive profiling tool based on Linux perf
// Copyright (C) CERN. See LICENSE for details.

#include "entrypoint.hpp"
#include "print.hpp"
#include "profiling.hpp"
#include "profilers.hpp"
#include "server/socket.hpp"
#include "cmd.hpp"
#include <boost/algorithm/string.hpp>
#include <boost/program_options/parsers.hpp>
#include <regex>
#include <sys/wait.h>

#ifndef APERF_CONFIG_FILE
#define APERF_CONFIG_FILE ""
#endif

namespace aperf {
  namespace ch = std::chrono;
  using namespace std::chrono_literals;

  bool quiet;

  /**
     A class validating whether a supplied command-line
     option is equal to or larger than a given value.
  */
  class OnlyMinRange : public CLI::Validator {
  public:
    OnlyMinRange(int min) {
      func_ = [=](const std::string &arg) {
        if (!std::regex_match(arg, std::regex("^[0-9]+$")) ||
            std::stoi(arg) < min) {
          return "The value must be a number equal to or greater than " +
            std::to_string(min);
        }

        return std::string();
      };
    }
  };

  std::vector<std::string> tokenize(const std::string& input, char delim,
                                    bool keep_quotes) {
    std::vector<std::string> tokens;
    std::string current_token;
    bool inside_quotes = false;

    for (char c : input) {
      if (c == '"' && (current_token.empty() || current_token.back() != '\\')) {
        inside_quotes = !inside_quotes;

        if (keep_quotes) {
          current_token += c;
        }
      } else if (c == delim && !inside_quotes) {
        tokens.push_back(current_token);
        current_token.clear();
      } else {
        current_token += c;
      }
    }

    tokens.push_back(current_token);
    return tokens;
  }

  /**
     Entry point to the AdaptivePerf frontend when it is run from
     the command line.
  */
  int main_entrypoint(int argc, char **argv) {
    CLI::App app("Comprehensive profiling tool based on Linux perf");

    app.formatter(std::make_shared<PrettyFormatter>());

    bool print_version = false;
    app.add_flag("-v,--version", print_version, "Print version and exit");

    unsigned int freq = 10;
    app.add_option("-F,--freq", freq, "Sampling frequency per second for "
                   "on-CPU time profiling")
      ->check(OnlyMinRange(1))
      ->option_text("UINT>0");

    unsigned int buffer = 1;
    app.add_option("-B,--buffer", buffer, "Buffer up to this number of "
                   "events before sending data for post-processing "
                   "(1 effectively disables buffering)")
      ->check(OnlyMinRange(1))
      ->option_text("UINT>0");

    int off_cpu_freq = 1000;
    app.add_option("-f,--off-cpu-freq", off_cpu_freq, "Sampling frequency "
                   "per second for off-CPU time profiling "
                   "(0 disables off-CPU profiling, -1 makes AdaptivePerf "
                   "capture *all* off-CPU events)")
      ->check(OnlyMinRange(-1))
      ->option_text("UINT or -1");

    unsigned int off_cpu_buffer = 0;
    app.add_option("-b,--off-cpu-buffer", off_cpu_buffer, "Buffer up to "
                   "this number of off-CPU events before sending data "
                   "for post-processing (0 leaves the default "
                   "adaptive buffering, 1 effectively disables buffering)")
      ->check(OnlyMinRange(0))
      ->option_text("UINT");

    unsigned int post_process = 1;
    int max_allowed = std::thread::hardware_concurrency() - 3;

    if (max_allowed < 1) {
      max_allowed = 1;
    }

    app.add_option("-p,--post-process", post_process, "Number of threads "
                   "isolated from profiled command to use for profilers "
                   "and post-processing (must not be greater than " +
                   std::to_string(max_allowed) + "). Use 0 to not "
                   "isolate profiler and post-processing threads "
                   "from profiled command threads (NOT RECOMMENDED).")
      ->check(CLI::Range(0, max_allowed))
      ->option_text("UINT");

    std::string address = "";
    app.add_option("-a,--address", address, "Delegate post-processing to "
                   "another machine running adaptiveperf-server. All results "
                   "will be stored on that machine.")
      ->check([](const std::string &arg) {
        if (!std::regex_match(arg, std::regex("^.+\\:[0-9]+$"))) {
          return "The value must be in form of \"<address>:<port>\".";
        }

        return "";
      })
      ->option_text("ADDRESS:PORT");

    unsigned int server_buffer = 1024;
    app.add_option("-s,--server-buffer", server_buffer, "Communication "
                   "buffer size in bytes for internal adaptiveperf-server. "
                   "Not to be used with -a.")
      ->check(OnlyMinRange(1))
      ->option_text("UINT>0")
      ->excludes("-a");

    unsigned int warmup = 1;
    app.add_option("-w,--warmup", warmup, "Warmup time in seconds between "
                   "adaptiveperf-server signalling readiness for receiving "
                   "data and starting the profiled program. Increase this "
                   "value if you see missing information after profiling "
                   "(note that adaptiveperf-server is also used internally "
                   "if no -a option is specified).")
      ->check(OnlyMinRange(1))
      ->option_text("UINT>0");

    std::vector<std::string> event_strs;
    app.add_option("-e,--event", event_strs, "Extra perf event to be used "
                   "for sampling with a given period (i.e. do a sample on "
                   "every PERIOD occurrences of an event and display the "
                   "results under the title TITLE in a website). Run "
                   "\"perf list\" for the list of possible events. You "
                   "can specify multiple events by specifying this flag "
                   "more than once. Use quotes if you need to use spaces.")
      ->check([](const std::string &arg) {
        if (!std::regex_match(arg, std::regex("^.+,[0-9\\.]+,.+$"))) {
          return "The value must be in form of EVENT,PERIOD,TITLE (PERIOD must "
            "be a number).";
        }

        return "";
      })
      ->option_text("EVENT,PERIOD,TITLE")
      ->take_all();

    quiet = false;
    app.add_flag("-q,--quiet", quiet, "Do not print anything (if set, check "
                 "exit code for any errors)");

    std::string command = "";
    app.add_option("COMMAND", command, "Command to be profiled (required)")
      ->check([](const std::string &arg) {
        if (!arg.empty()) {
          std::vector<std::string> parts = boost::program_options::split_unix(arg);

          if (parts.empty()) {
            return "The command you have provided is not a valid one!";
          }
        }

        return "";
      })
      ->option_text(" ");

    struct SubcommandData {
    std::string metric_command;
    std::string metric_name;
    unsigned int frequency;
    std::string regular_expression;
    };

    std::vector<std::string> metric_strs;
    std::vector<SubcommandData> metric_profilers_data;
    std::regex pattern_metric("");
    app.add_option("-m,--metric", metric_strs, 
        "Extra metric to sample based on an external profiler/metric command. Usage: -m "
        "c:\"METRIC_COMMAND\",n:\"METRIC_NAME\",f:SAMPLING_FREQUENCY (must be a number), r:\"REGULAR_EXPRESSION\"\n"
        "Argument description:\n"
        "  METRIC_COMMAND - required : Any command that outputs a number (float) to stdout.\n"
        "  METRIC_NAME - required : Alias for the metric which will be displayed in Adaptiveperf HTML.\n"
        "  SAMPLING_FREQUENCY - optional, default_value 10 : An integer which represents the sampling rate of the METRIC_COMMAND.\n"
        "  REGULAR_EXPRESSION - optional : A regular expression (EcmaScript flavor) to parse the output of the METRIC_COMMAND."
    )
      ->option_text("CONFIG")
      ->check([&metric_profilers_data](const std::string &arg) {
        std::vector<std::string> matches_metric = tokenize(arg, ',', true);

        SubcommandData metric_command_data = {"", "", 0, ""};

        for (std::string &option : matches_metric) {
          std::vector<std::string> parsed_option = tokenize(option, ':', false);
          if (parsed_option.size() == 2) {
            if (parsed_option[0] == "c")
              if (metric_command_data.metric_command.empty()) {
                metric_command_data.metric_command = parsed_option[1];
              } else {
                return "Cannot specify multiple metric commands";

              } 
            else if (parsed_option[0] == "n")
              if (metric_command_data.metric_name.empty()) {
                metric_command_data.metric_name = parsed_option[1];
              } else {
                return "Cannot specify multiple metric names";

              }
            else if (parsed_option[0] == "f")
              if (metric_command_data.frequency == 0){
                try
                {
                  metric_command_data.frequency = std::stoi(parsed_option[1]);
                  if (metric_command_data.frequency < 0) {
                    return "Cannot have negative frequency";

                  }
                } catch (const std::invalid_argument &e) {
                  return "Error: Invalid argument. Could not convert string to integer.";
                } catch (const std::out_of_range &e) {
                  return "Error: Value out of range. ";
                }
                if (metric_command_data.frequency == 0) {
                  return "Cannot set frequency value to 0.";
                }
              } else {
                return "Cannot specify multiple frequencies for the metric";

              }
            else if (parsed_option[0] == "r")
              if (metric_command_data.regular_expression.empty()) {
                metric_command_data.regular_expression = parsed_option[1];
              } else {
                return "Cannot specify multiple regular expressions for one metric command";
              } else {
              return "Metric command option not recognised.";
            }
          } else {
            return "Cannot parse option for metric command (too many or too little agruments were given)";
          }
        }
        if (metric_command_data.metric_command.empty()) {
          return "Metric command needs to be specified";
        }
        if (metric_command_data.metric_name.empty()) {
          return "Metric name needs to be specified";
        }
        if (metric_command_data.frequency == 0) {
          metric_command_data.frequency = 10;
        }

        metric_profilers_data.push_back(metric_command_data);
        return "";
      })
      ->take_all();
    
    CLI11_PARSE(app, argc, argv);

    if (print_version) {
      std::cout << version << std::endl;
      return 0;
    } else if (command == "") {
      std::cerr << "You need to provide the command to be profiled!" << std::endl;
      return 3;
    } else {
      auto start_time =
        ch::duration_cast<ch::milliseconds>(ch::system_clock::now().time_since_epoch()).count();

      print_notice();

      print("Reading config file...", false, false);
      std::ifstream config_f(APERF_CONFIG_FILE);

      if (!config_f) {
        print("Cannot open " APERF_CONFIG_FILE "!", true, true);
        return 2;
      }

      std::unordered_map<std::string, std::string> config;
      int cur_line = 1;

      while (config_f) {
        std::string line;
        std::getline(config_f, line);

        if (line.empty() || line[0] == '#') {
          cur_line++;
          continue;
        }

        std::smatch match;

        if (!std::regex_match(line, match,
                              std::regex("^(\\S+)\\s*\\=\\s*(.+)$"))) {
          print("Syntax error in line " + std::to_string(cur_line) + " of "
                APERF_CONFIG_FILE "!", true, true);
          return 2;
        }

        config[match[1]] = match[2];
        cur_line++;
      }

      if (config.find("perf_path") == config.end()) {
        print("You must specify the path to your patched \"perf\" installation "
              "(perf_path) in " APERF_CONFIG_FILE "!", true, true);
        return 2;
      }

      fs::path perf_path(config["perf_path"]);
      perf_path = perf_path / "bin" / "perf";

      if (!fs::exists(perf_path)) {
        print(perf_path.string() + " does not exist!", true, true);
        print("Hint: You may want to verify the contents of " APERF_CONFIG_FILE ".",
              false, true);
        return 2;
      }

      if (!fs::is_regular_file(perf_path)) {
        print(perf_path.string() + " is not a regular file!", true, true);
        print("Hint: You may want to verify the contents of " APERF_CONFIG_FILE ".",
              false, true);
        return 2;
      }

      print("Checking CPU specification...", false, false);

      CPUConfig cpu_config = get_cpu_config(post_process,
                                            address != "");

      if (!cpu_config.is_valid()) {
        return 1;
      }

      cpu_set_t &cpu_set = cpu_config.get_cpu_profiler_set();
      sched_setaffinity(0, sizeof(cpu_set), &cpu_set);

      std::vector<std::unique_ptr<Profiler> > profilers;

      PerfEvent main(freq, off_cpu_freq, buffer, off_cpu_buffer);
      PerfEvent syscall_tree;

      profilers.push_back(std::make_unique<Perf>(perf_path, syscall_tree, cpu_config,
                                                 "Thread tree profiler"));
      profilers.push_back(std::make_unique<Perf>(perf_path, main, cpu_config,
                                                 "On-CPU/Off-CPU profiler"));

      for (std::string &event_str : event_strs) {
        std::vector<std::string> parts;
        boost::split(parts, event_str, boost::is_any_of(","));

        std::string event_name = parts[0];
        int period = std::stoi(parts[1]);

        PerfEvent event(event_name, period, buffer);
        profilers.push_back(std::make_unique<Perf>(perf_path, event, cpu_config,
                                                   event_name));
      }

      for (SubcommandData metric_command_data : metric_profilers_data) {
        
        profilers.push_back(std::make_unique<MetricReader>(metric_command_data.metric_command, metric_command_data.metric_name, metric_command_data.frequency, metric_command_data.regular_expression, server_buffer));
      }

      pid_t current_pid = getpid();
      fs::path tmp_dir = fs::temp_directory_path() /
        ("adaptiveperf.pid." + std::to_string(current_pid));

      if (fs::exists(tmp_dir)) {
        fs::remove_all(tmp_dir);
      }

      std::vector<pid_t> spawned_children;
      int to_return = 0;

      try {
        int code = start_profiling_session(profilers, command, address, server_buffer, warmup, cpu_config,
                                           tmp_dir, spawned_children);

        auto end_time =
          ch::duration_cast<ch::milliseconds>(ch::system_clock::now().time_since_epoch()).count();

        if (code == 0) {
          fs::remove_all(tmp_dir);

          print("Done in " + std::to_string(end_time - start_time) + " ms in total! "
                "You can check the results directory now.", false, false);
        } else if (code != 1) {
          print("For investigating what has gone wrong, you can check the files created in " +
                tmp_dir.string() + ".", false, true);
        }

        to_return = code;
      } catch (ConnectionException &e) {
        print("I/O error has occurred in the communication with adaptiveperf-server! Exiting.",
              false, true);
        print("For investigating what has gone wrong, you can check the files created in " +
              tmp_dir.string() + ".", false, true);

        to_return = 2;
      } catch (std::exception &e) {
        print("A fatal error has occurred! If the issue persits, "
              "please contact the AdaptivePerf developers, citing \"" +
              std::string(e.what()) + "\".", false, true);
        print("For investigating what has gone wrong, you can check the files created in " +
              tmp_dir.string() + ".", false, true);

        to_return = 2;
      }

      for (auto &pid : spawned_children) {
        int status;
        waitpid(pid, &status, WNOHANG);

        if (status == 0) {
          kill(pid, SIGTERM);
        }
      }

      return to_return;
    }
  }
};
