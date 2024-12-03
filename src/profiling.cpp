// AdaptivePerf: comprehensive profiling tool based on Linux perf
// Copyright (C) CERN. See LICENSE for details.

#define __USE_POSIX

#include "profiling.hpp"
#include "print.hpp"
#include "server/server.hpp"
#include "archive.hpp"
#include "process.hpp"
#include <filesystem>
#include <iomanip>
#include <queue>
#include <future>
#include <regex>
#include <mutex>
#include <thread>
#include <fstream>
#include <unordered_set>
#include <sys/types.h>
#include <sys/wait.h>
#include <Poco/Net/StreamSocket.h>
#include <boost/core/demangle.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/program_options/parsers.hpp>
#include <nlohmann/json.hpp>
#include <boost/asio.hpp>

#define NOTIFY_TIMEOUT 5
#define FILE_TIMEOUT 30

namespace aperf {
  namespace fs = std::filesystem;
  namespace ch = std::chrono;
  using namespace std::chrono_literals;

  /**
     Constructs a CPUConfig object.

     @param mask A CPU mask string, where the i-th character
                 defines the purpose of the i-th core as follows:
                 ' ' means "not used",
                 'p' means "used for post-processing and profilers",
                 'c' means "used for the profiled command", and
                 'b' means "used for both the profiled command and post-processing + profilers".
  */
  CPUConfig::CPUConfig(std::string mask) {
    this->valid = false;
    this->profiler_thread_count = 0;

    CPU_ZERO(&this->cpu_profiler_set);
    CPU_ZERO(&this->cpu_command_set);

    if (!mask.empty()) {
      this->valid = true;

      for (int i = 0; i < mask.length(); i++) {
        if (mask[i] == 'p') {
          this->profiler_thread_count++;
          CPU_SET(i, &this->cpu_profiler_set);
        } else if (mask[i] == 'c') {
          CPU_SET(i, &this->cpu_command_set);
        } else if (mask[i] == 'b') {
          this->profiler_thread_count++;
          CPU_SET(i, &this->cpu_profiler_set);
          CPU_SET(i, &this->cpu_command_set);
        } else if (mask[i] != ' ') {
          this->valid = false;
          this->profiler_thread_count = 0;
          CPU_ZERO(&this->cpu_profiler_set);
          CPU_ZERO(&this->cpu_command_set);
          return;
        }
      }
    }
  }

  /**
     Returns whether a CPUConfig object is valid.

     A CPUConfig object can be invalid only if the string mask used
     for its construction is invalid.
  */
  bool CPUConfig::is_valid() const {
    return this->valid;
  }

  /**
     Returns the number of profiler threads that can be spawned
     based on how many cores are allowed for doing the profiling.
  */
  int CPUConfig::get_profiler_thread_count() const {
    return this->profiler_thread_count;
  }

  /**
     Returns the sched_setaffinity-compatible CPU set for doing
     the profiling.
  */
  cpu_set_t CPUConfig::get_cpu_profiler_set() const {
    return this->cpu_profiler_set;
  }

  /**
     Returns the sched_setaffinity-compatible CPU set for running
     the profiled command.
  */
  cpu_set_t CPUConfig::get_cpu_command_set() const {
    return this->cpu_command_set;
  }

  /**
     Constructs a ServerConnInstrs object.

     @param all_connection_instrs An adaptiveperf-server connection
                                  instructions string sent
                                  by adaptiveperf-server during the initial
                                  setup phase. It is in form of
                                  "<method> <connection details>", where
                                  \<connection details\> is provided once or
                                  more than once per profiler, separated by
                                  a space character. \<connection details\>
                                  takes form of "<field1>_<field2>_..._<fieldX>"
                                  where the number of fields and their content
                                  are implementation-dependent.
  */
  ServerConnInstrs::ServerConnInstrs(std::string all_connection_instrs) {
    std::vector<std::string> parts;
    boost::split(parts, all_connection_instrs, boost::is_any_of(" "));

    if (parts.size() > 0) {
      this->type = parts[0];

      for (int i = 1; i < parts.size(); i++) {
        this->methods.push(parts[i]);
      }
    }
  }

  /**
     Gets a connection instructions string relevant to the profiler
     requesting these instructions.

     @param thread_count A number of threads expected to connect
                         to adaptiveperf-server from the current
                         profiler.

     @throw std::runtime_error When the sum of thread_count amongst
                               all get_instruction() calls within a
                               single ServerConnInstrs object
                               exceeds the number of \<connection details\>
                               sent by adaptiveperf-server.
  */
  std::string ServerConnInstrs::get_instructions(int thread_count) {
    std::string result = this->type;

    for (int i = 0; i < thread_count; i++) {
      if (this->methods.empty()) {
        throw std::runtime_error("Could not obtain server connection "
                                 "instructions for thread_count = " +
                                 std::to_string(thread_count) + ".");
      }

      result += " " + this->methods.front();
      this->methods.pop();
    }

    return result;
  }

  /**
     Analyses the current machine configuration and returns the most
     appropriate CPUConfig object, taking into account user considerations.

     @param post_processing_threads A number of cores that should be used
                                    for post-processing and profiling. This
                                    should not be larger than the number of
                                    available cores minus 3. 0 marks all
                                    cores as available for all activities.
     @param external_server         Indicates if post-processing is delegated to
                                    an external instance of adaptiveperf-server.
                                    If only 1 core is available, this must be
                                    set to true.

     If the user-provided parameters are invalid or the current machine
     configuration is considered unsuitable for profiling, an invalid CPUConfig
     object is returned (i.e. CPUConfig::is_valid() returns false).
  */
  CPUConfig get_cpu_config(int post_processing_threads,
                           bool external_server) {
    int num_proc = std::thread::hardware_concurrency();

    if (num_proc == 0) {
      print("Could not determine the number of available logical cores!",
            true, true);
      return CPUConfig("");
    }

    if (post_processing_threads == 0) {
      print("AdaptivePerf called with -p 0, proceeding...",
            true, false);

      char mask[num_proc];

      for (int i = 0; i < num_proc; i++) {
        mask[i] = 'b';
      }

      return CPUConfig(std::string(mask, num_proc));
    } else if (post_processing_threads > num_proc - 3) {
      print("The value of -p must be less than or equal to the number of "
            "logical cores minus 3 (i.e. " + std::to_string(num_proc - 3) +
            ")!", true, true);
      return CPUConfig("");
    } else {
      if (num_proc < 4) {
        print("Because there are fewer than 4 logical cores, "
              "the value of -p will be ignored for the profiled "
              "program unless it is 0.", true, false);
      }

      switch (num_proc) {
      case 1:
        if (external_server) {
          print("1 logical core detected, running everything on core #0 "
                "thanks to delegation to an external instance of "
                "adaptiveperf-server (you may still get inconsistent "
                "results, but it's less likely due to lighter on-site "
                "processing).", true, false);
          return CPUConfig("b");
        } else {
          print("Running profiling along with post-processing is *NOT* "
                "recommended on a machine with only one logical core! "
                "You are very likely to get inconsistent results due "
                "to profiling threads interfering with the profiled "
                "program.", true, true);
          print("Please delegate post-processing to another machine by "
                "using the -a flag. If you want to proceed anyway, "
                "run AdaptivePerf with -p 0.", true, true);
          return CPUConfig("");
        }

      case 2:
        print("2 logical cores detected, running post-processing and "
              "profilers on core #0 and the command on core #1.", true,
              false);
        return CPUConfig("pc");

      case 3:
        print("3 logical cores detected, running post-processing and "
              "profilers on cores #0 and #1 and the command on core #2.",
              true, false);
        return CPUConfig("ppc");

      default:
        char mask[num_proc];
        mask[0] = ' ';
        mask[1] = ' ';
        for (int i = 2; i < 2 + post_processing_threads; i++) {
          mask[i] = 'p';
        }
        for (int i = 2 + post_processing_threads; i < num_proc; i++) {
          mask[i] = 'c';
        }
        return CPUConfig(std::string(mask, num_proc));
      }
    }
  }

  /**
     Starts a profiling session.

     @param profilers        A list of profilers used to profile the command.
     @param command_elements A command to be profiled, in form of a vector of string parts
                             (e.g. "adaptiveperf -f 100 test" becomes ["adaptiveperf",
                             "-f", "100", "test"]).
     @param server_address   The address and port of an external instance of adaptiveperf-server.
                             If the external instance usage is not planned, server_address
                             should be an empty string.
     @param buf_size         A size of buffer for communication with adaptiveperf-server,
                             in bytes.
     @param warmup           A number of seconds between the profilers indicating their
                             readiness and the actual execution of the command. This may have to
                             be high on machines with weaker configurations.
     @param cpu_config       A CPUConfig object describing how available cores should be used
                             for profiling. It's recommended to call get_cpu_config() for this.
     @param tmp_dir          A temporary directory where profiling-related files will be stored.
     @param spawned_children A list of PIDs of children spawned during the profiling session.
                             This will be populated as the function executes and it's mostly
                             important in the context of cleaning up after the session finishes
                             or terminates with an error.
     @param event_dict       A dictionary mapping custom "perf" event names to their website
                             titles (e.g. "page-faults" -> "Page faults"). This dictionary will
                             be saved to event_dict.data in the "processed" directory.
  */
  int start_profiling_session(std::vector<std::unique_ptr<Profiler> > &profilers,
                              std::vector<std::string> &command_elements,
                              std::string server_address,
                              unsigned int buf_size, unsigned int warmup,
                              CPUConfig &cpu_config, fs::path tmp_dir,
                              std::vector<pid_t> &spawned_children,
                              std::unordered_map<std::string, std::string> &event_dict) {
    print("Verifying profiler requirements...", false, false);

    bool requirements_fulfilled = true;
    std::string last_requirement = "";

    for (int i = 0; i < profilers.size() && requirements_fulfilled; i++) {
      std::vector<std::unique_ptr<Requirement> > &requirements = profilers[i]->get_requirements();

      for (int j = 0; j < requirements.size() && requirements_fulfilled; j++) {
        last_requirement = requirements[j]->get_name();
        requirements_fulfilled = requirements_fulfilled && requirements[j]->check();
      }
    }

    if (!requirements_fulfilled) {
      print("Requirement \"" + last_requirement + "\" is not met! Exiting.",
            true, true);
      return 1;
    }

    print("Preparing for profiling...", false, false);

    std::string profiled_filename = fs::path(command_elements[0]).filename();

    const time_t t = std::time(nullptr);
    struct tm *tm = std::gmtime(&t);

    std::ostringstream stream;
    stream << std::put_time(tm, "%Y_%m_%d_%H_%M_%S");

    char hostname[HOST_NAME_MAX];
    gethostname(hostname, HOST_NAME_MAX);

    std::string result_name = stream.str() + "_" + hostname + "__" + profiled_filename;

    fs::path results_dir = fs::absolute(tmp_dir / "results");
    fs::path result_dir = results_dir / result_name;
    fs::path result_out = result_dir / "out";
    fs::path result_processed = result_dir / "processed";

    try {
      fs::create_directories(result_dir);
      fs::create_directories(result_out);
      fs::create_directories(result_processed);
    } catch (fs::filesystem_error) {
      print("Could not create one or more of these directories: " +
            result_dir.string() + ", " +
            result_out.string() + ", " +
            result_processed.string() + "! Exiting.", true, true);
      return 2;
    }

    {
      std::ofstream event_dict_stream(result_processed / "event_dict.data");

      if (!event_dict_stream) {
        print("Could not open " +
              fs::absolute(result_processed / "event_dict.data").string() +
              " for writing!", true, true);
        return 2;
      }

      for (auto &elem : event_dict) {
        event_dict_stream << elem.first << " " << elem.second << std::endl;
      }
    }

    print("Starting profiled program wrapper...", true, false);

    Process wrapper(command_elements);

    wrapper.set_redirect_stdout(result_out / "stdout.log");
    wrapper.set_redirect_stderr(result_out / "stderr.log");

    int wrapper_id = wrapper.start(true, cpu_config, false);
    spawned_children.push_back(wrapper_id);

    if (server_address == "") {
      print("Starting adaptiveperf-server and profilers...", true, false);
    } else {
      print("Connecting to adaptiveperf-server and starting profilers...", true, false);
    }

    std::unique_ptr<Connection> connection;

    if (server_address == "") {
      int read_fd[2];
      int write_fd[2];

      if (pipe(read_fd) != 0) {
        print("Could not open read pipe for FileDescriptor, "
              "code " + std::to_string(errno) + ". Exiting.", true, true);
        return 2;
      }

      if (pipe(write_fd) != 0) {
        print("Could not open write pipe for FileDescriptor, "
              "code " + std::to_string(errno) + ". Exiting.", true, true);
        return 2;
      }

      connection = std::make_unique<FileDescriptor>(write_fd, read_fd, buf_size);
      std::unique_ptr<Connection> server_connection =
        std::make_unique<FileDescriptor>(read_fd, write_fd, buf_size);
      std::unique_ptr<Acceptor::Factory> acceptor_factory =
        std::make_unique<PipeAcceptor::Factory>();
      std::unique_ptr<Subclient::Factory> subclient_factory =
        std::make_unique<StdSubclient::Factory>(acceptor_factory);

      std::unique_ptr<Acceptor> file_acceptor = nullptr;

      StdClient::Factory factory(subclient_factory);
      std::shared_ptr<Client> client = factory.make_client(server_connection,
                                                           file_acceptor,
                                                           FILE_TIMEOUT);

      std::thread client_thread([client, results_dir, tmp_dir]() {
        try {
          client->process(results_dir);
        } catch (std::exception &e) {
          print("An unknown error has occurred in adaptiveperf-server! If the issue persists, "
                "please contact the AdaptivePerf developers, citing \"" +
                std::string(e.what()) + "\".", true, true);
          print("For investigating what has gone wrong, you can check the files created in " +
                tmp_dir.string() + ".", false, true);
          std::exit(2);
        }
      });

      client_thread.detach();
    } else {
      Poco::Net::SocketAddress address(server_address);
      Poco::Net::StreamSocket socket(address);

      connection = std::make_unique<TCPSocket>(socket, buf_size);
    }

    unsigned int pipe_triggers = 0;

    for (int i = 0; i < profilers.size(); i++) {
      pipe_triggers += profilers[i]->get_thread_count();
    }

    connection->write("start" + std::to_string(pipe_triggers) + " " + result_name);
    connection->write(profiled_filename);

    std::string all_connection_instrs = connection->read();

    if (std::regex_match(all_connection_instrs, std::regex("^error.*$"))) {
      print("adaptiveperf-server has encountered an error (start)! Exiting.", true, true);
      return 2;
    }

    ServerConnInstrs connection_instrs(all_connection_instrs);

    for (int i = 0; i < profilers.size(); i++) {
      profilers[i]->start(wrapper_id, connection_instrs, result_out,
                          result_processed, true);
    }

    print("Waiting for profilers to signal their readiness. If AdaptivePerf "
          "hangs here, you may want to check the files in " +
          tmp_dir.string() + ".", true, false);

    auto profiler_and_wrapper_handler =
      [&](int code, long start_time, long end_time) {
        bool error = false;
        for (int i = 0; i < profilers.size(); i++) {
          int code = profilers[i]->wait();

          if (code != 0) {
            error = true;
            break;
          }
        }

        if (error) {
          print("One or more profilers have encountered an error.", true, true);
        }

        if (code == 0) {
          if (start_time != -1 && end_time != -1) {
            print("Command execution completed in ~" +
                      std::to_string(end_time - start_time) + " ms!",
                  true, false);
          }
        } else if (code == Process::ERROR_NOT_FOUND) {
          print("Provided command does not exist!", true, true);
          error = true;
        } else if (code == Process::ERROR_NO_ACCESS) {
          print("Cannot access the provided command!", true, true);
          print("Hint: You may want to mark your file as executable by running \"chmod +x <file>\".", true, true);
          error = true;
        } else {
          print("Profiled program wrapper has finished with non-zero exit code " +
                std::to_string(code) + ".", true, true);

          switch (code) {
          case Process::ERROR_START_PROFILE:
            print("Hint: Code " + std::to_string(code) + " suggests something "
                  "bad happened when instructing the wrapper to execute the "
                  "profiled command.", true, true);
            break;

          case Process::ERROR_STDOUT:
            print("Hint: Code " + std::to_string(code) + " suggests something "
                  "bad happened when opening the stdout log file for writing.", true, true);
            break;

          case Process::ERROR_STDERR:
            print("Hint: Code " + std::to_string(code) + " suggests something "
                  "bad happened when opening the stderr log file for writing.", true, true);
            break;

          case Process::ERROR_STDOUT_DUP2:
            print("Hint: Code " + std::to_string(code) + " suggests something "
                  "bad happened when redirecting stdout of the profiled command "
                  "wrapper to the stdout log file.", true, true);
            break;

          case Process::ERROR_STDERR_DUP2:
            print("Hint: Code " + std::to_string(code) + " suggests something "
                  "bad happened when redirecting stderr of the profiled command "
                  "wrapper to the stderr log file.", true, true);
            break;

          case Process::ERROR_AFFINITY:
            print("Hint: Code " + std::to_string(code) + " suggests something "
                  "bad happened when isolating the profiled command wrapper "
                  "CPU-wise from the profilers.", true, true);
            break;
          }

          error = true;
        }

        if (error) {
          print("Errors have occurred! Exiting.", true, true);
          return 2;
        } else {
          return 0;
        }
      };

    std::string notification_msg;

    while (true) {
      try {
        notification_msg = connection->read(NOTIFY_TIMEOUT);
        break;
      } catch (TimeoutException &e) {
        if (!wrapper.is_running()) {
          int code = profiler_and_wrapper_handler(wrapper.join(), -1, -1);
          if (code != 0) {
            return code;
          }
        }
      }
    }

    if (notification_msg != "start_profile") {
      print("adaptiveperf-server has sent something else than a notification "
            "of the profiler readiness! Exiting.", true, true);
      return 2;
    }

    print("All profilers have signalled their readiness, waiting " +
          std::to_string(warmup) + " second(s)...", true, false);
    std::this_thread::sleep_for(warmup * 1s);

    print("Profiling...", false, false);

    std::string command_list_str = "[";

    for (auto command_element : command_elements) {
      boost::algorithm::replace_all(command_element, "\"", "\\\"");
      command_list_str += "\"" + command_element + "\", ";
    }

    command_list_str = command_list_str.substr(0, command_list_str.size() - 2) + "]";

    print("Executing the following command (as passed to the exec syscall): " +
          command_list_str, true, false);

    auto start_time =
      ch::duration_cast<ch::milliseconds>(ch::system_clock::now().time_since_epoch()).count();

    wrapper.notify();
    wrapper.close_stdin();
    int exit_code = wrapper.join();

    auto end_time =
      ch::duration_cast<ch::milliseconds>(ch::system_clock::now().time_since_epoch()).count();

    int code = profiler_and_wrapper_handler(exit_code, start_time, end_time);

    if (code != 0) {
      return code;
    }

    std::string msg = connection->read();

    if (msg != "out_files" && msg != "profiling_finished") {
      print("adaptiveperf-server has not indicated its successful completion! Exiting.",
            true, true);
      return 2;
    }

    print("Processing results...", false, false);

    std::unordered_set<fs::path> perf_map_paths;
    std::unordered_map<std::string, std::unordered_set<std::string> > dso_offsets;
    bool perf_maps_expected = false;

    for (int i = 0; i < profilers.size(); i++) {
      std::unique_ptr<Connection> &generic_connection = profilers[i]->get_connection();

      if (generic_connection.get() == nullptr) {
        continue;
      }

      std::string line;

      while ((line = generic_connection->read()) != "<STOP>") {
        if (line.empty()) {
          continue;
        }

        try {
          nlohmann::json parsed = nlohmann::json::parse(line);

          if (!parsed.is_object()) {
            print("Message received from profiler \"" +
                  profilers[i]->get_name() + "\" "
                  "is not a JSON object, ignoring.", true, false);
            continue;
          }

          if (parsed.size() != 2 || !parsed.contains("type") ||
              !parsed.contains("data")) {
            print("Message received from profiler \"" +
                  profilers[i]->get_name() + "\" "
                  "is not a JSON object with exactly 2 elements (\"type\" and "
                  "\"data\"), ignoring.", true, false);
          }

          if (parsed["type"] == "symbol_maps") {
            if (!parsed["data"].is_array()) {
              print("Message received from profiler \"" +
                    profilers[i]->get_name() + "\" "
                    "is a JSON object of type \"symbol_maps\", but its \"data\" "
                    "element is not a JSON array, ignoring.", true, false);
              continue;
            }

            int index = -1;
            for (auto &elem : parsed["data"]) {
              index++;

              if (!elem.is_string()) {
                print("Element " + std::to_string(index) +
                      " in the array in the message "
                      "of type \"symbol_maps\" received from profiler \"" +
                      profilers[i]->get_name() +
                      "\" is not a string, ignoring this element.", true, false);
                continue;
              }

              fs::path perf_map_path(elem.get<std::string>());

              if (fs::exists(perf_map_path)) {
                perf_map_paths.insert(perf_map_path);
              } else {
                print("A symbol map is expected in " +
                      fs::absolute(perf_map_path).string() +
                      ", but it hasn't been found!",
                      true, false);
                perf_maps_expected = true;
              }
            }
          } else if (parsed["type"] == "sources") {
            if (!parsed["data"].is_object()) {
              print("Message received from profiler \"" +
                    profilers[i]->get_name() + "\" "
                    "is a JSON object of type \"sources\", but its \"data\" "
                    "element is not a JSON object, ignoring.", true, false);
              continue;
            }

            int index = -1;
            for (auto &elem : parsed["data"].items()) {
              index++;

              if (!elem.value().is_array()) {
                print("Element \"" + elem.key() + "\" in the data object of "
                      "type \"sources\" received from profiler \"" +
                      profilers[i]->get_name() + "\" is not a JSON array, "
                      "ignoring this element.", true, false);
                continue;
              }

              if (fs::exists(elem.key())) {
                if (dso_offsets.find(elem.key()) == dso_offsets.end()) {
                  dso_offsets[elem.key()] = std::unordered_set<std::string>();
                }

                for (auto &offset : elem.value()) {
                  dso_offsets[elem.key()].insert(offset);
                }
              }
            }
          }
        } catch (nlohmann::json::exception) {
          print("Message received from profiler \"" +
                profilers[i]->get_name() + "\" "
                "is not valid JSON, ignoring.", true, false);
        }
      }
    }

    nlohmann::json sources_json = nlohmann::json::object();

    // The number of threads needs to stay at 1 here because of a bug
    // (a race condition?) causing randomly addr2line not to terminate after
    // the stdin pipe is closed.
    //
    // TODO: fix this
    boost::asio::thread_pool pool(1);

    std::pair<std::string, nlohmann::json> sources[dso_offsets.size()];
    std::unordered_set<fs::path> source_files[dso_offsets.size()];

    int index = 0;

    for (auto &elem : dso_offsets) {
      auto process_func = [index, elem, cpu_config, &sources, &source_files]() {
        std::vector<std::string> cmd = {"addr2line", "-e", elem.first};
        Process process(cmd);
        process.start(false, cpu_config, true);

        nlohmann::json result;
        std::unordered_set<fs::path> files;

        for (auto &offset : elem.second) {
          std::string to_write = offset + '\n';
          process.write_stdin((char *)to_write.c_str(), to_write.size());
          std::vector<std::string> parts;
          boost::split(parts, process.read_line(), boost::is_any_of(":"));

          if (parts.size() == 2) {
            try {
              result[offset] = nlohmann::json::object();
              result[offset]["file"] = parts[0];
              result[offset]["line"] = std::stoi(parts[1]);
              files.insert(parts[0]);
            } catch (...) { }
          }
        }

        sources[index] = std::make_pair(elem.first, result);
        source_files[index] = files;
      };

      boost::asio::post(pool, process_func);
      index++;
    }

    pool.join();

    std::unordered_set<fs::path> src_paths;

    for (int i = 0; i < dso_offsets.size(); i++) {
      sources_json[sources[i].first].swap(sources[i].second);

      for (auto &elem : source_files[i]) {
        if (fs::exists(elem)) {
          src_paths.insert(elem);
        }
      }
    }

    if (perf_maps_expected) {
      print("One or more expected symbol maps haven't been found! "
            "This is not an error, but some symbol names will be unresolved and "
            "point to the name of an expected map file instead.", true, false);
      print("If it's not desired, make sure that your profiled "
            "program is configured to emit \"perf\" symbol maps.", true, false);
    }

    auto read_and_demangle_symbol_map =
      [](std::ifstream &stream, std::vector<std::string> &result) {
        while (stream) {
          std::string line;
          std::getline(stream, line);

          if (line.empty()) {
            continue;
          }

          std::vector<std::string> parts;
          boost::split(parts, line, boost::is_any_of(" "));

          if (parts.size() == 0) {
            continue;
          }

          std::string new_line = "";

          for (int i = 0; i < parts.size() - 1; i++) {
            new_line += parts[i] + " ";
          }

          new_line += boost::core::demangle(parts[parts.size() - 1].c_str());

          result.push_back(new_line);
        }
      };

    if (msg == "out_files") {
      bool transfer_error = false;

      std::string file_conn_instrs = connection->read();
      std::smatch general_match;

      if (!std::regex_match(file_conn_instrs,
                            general_match,
                            std::regex("^(\\S+) (.+)$"))) {
        print("Received incorrect connection "
              "instructions for file transfer from adaptiveperf-server! "
              "Exiting.", true, true);
        return 2;
      }

      std::function<std::unique_ptr<Connection>()> get_file_connection;

      if (general_match[1] == "tcp") {
        std::string tcp_instrs = general_match[2];
        std::smatch match;

        if (!std::regex_match(tcp_instrs,
                              match,
                              std::regex("^(\\S+)_(\\d+)$"))) {
          print("Received incorrect connection "
                "instructions for file transfer (tcp) from "
                "adaptiveperf-server! Exiting.", true, true);
          return 2;
        }

        std::string file_address = match[1];
        int file_port = std::stoi(match[2]);

        get_file_connection = [file_address, file_port]() {
          std::unique_ptr<Connection> file_connection;

          Poco::Net::SocketAddress address(file_address, file_port);
          Poco::Net::StreamSocket socket(address);

          // buf_size = 1 because it is only for string read which is unused here
          file_connection = std::make_unique<TCPSocket>(socket, 1);

          return file_connection;
        };
      } else {
        print("File transfer type \"" + general_match[1].str() + "\" suggested by "
              "adaptiveperf-server is not supported! Exiting.", true, true);
        return 2;
      }

      auto check_data_transfer = [&](std::string title) {
        std::string status = connection->read();

        if (status == "error_out_file") {
          print("Could not send " + title + "!", true, true);
          transfer_error = true;
        } else if (status == "error_out_file_timeout") {
          print("Could not send " + title + " due to timeout!",
                true, true);
          transfer_error = true;
        } else if (status != "out_file_ok") {
          print("Could not obtain confirmation of correct transfer of " +
                title + "!", true, true);
          transfer_error = true;
        }
      };

      for (const fs::path &path : perf_map_paths) {
        std::ifstream stream(path);
        connection->write("p " + path.filename().string());

        // A separate scope is needed for the file connection to close
        // automatically after the transfer is finished.
        {
          std::unique_ptr<Connection> file_connection = get_file_connection();
          std::vector<std::string> result;

          read_and_demangle_symbol_map(stream, result);

          for (std::string &new_line : result) {
            file_connection->write(new_line);
          }
        }

        check_data_transfer(path.filename().string());
      }

      if (!src_paths.empty()) {
        connection->write("p src.zip", true);
        nlohmann::json src_mapping = nlohmann::json::object();

        try {
          std::unique_ptr<Connection> file_connection = get_file_connection();
          Archive archive(file_connection, false, buf_size);

          for (const fs::path &path : src_paths) {
            std::string filename = std::to_string(src_mapping.size()) + path.extension().string();
            src_mapping[path.string()] = filename;
            archive.add_file(filename, path);
          }

          std::string src_mapping_str = nlohmann::to_string(src_mapping) + '\n';
          std::stringstream s;
          s << src_mapping_str;
          archive.add_file_stream("index.json", s, src_mapping_str.length());

          archive.close();
        } catch (nlohmann::json::exception &e) {
          print("A JSON error related to creating the source code archive has occurred! "
                "Details: " + std::string(e.what()), true, true);
        } catch (Archive::Exception &e) {
          print("A source code archive creation error has occurred! "
                "Details: " + std::string(e.what()), true, true);
        }

        check_data_transfer("the source code archive");
      }

      if (!sources_json.empty()) {
        connection->write("p sources.json", true);

        {
          std::unique_ptr<Connection> file_connection = get_file_connection();
          file_connection->write(nlohmann::to_string(sources_json));
        }

        check_data_transfer("the source code detail index");
      }

      for (auto &elem : fs::directory_iterator(result_processed)) {
        fs::path path = elem.path();

        if (!elem.is_regular_file()) {
          print(path.filename().string() + " is not a file, it will not be copied to the "
                "\"processed\" directory.",
                true, false);
          continue;
        }

        connection->write("p " + path.filename().string());

        // A separate scope is needed for the file connection to close
        // automatically after the transfer is finished.
        {
          std::unique_ptr<Connection> file_connection = get_file_connection();
          file_connection->write(path);
        }

        check_data_transfer(path.filename().string());
      }

      for (auto &elem : fs::directory_iterator(result_out)) {
        fs::path path = elem.path();

        if (!elem.is_regular_file()) {
          print(path.filename().string() + " is not a file, it will not be copied to the "
                "\"out\" directory.",
                true, false);
          continue;
        }

        connection->write("o " + path.filename().string());

        // A separate scope is needed for the file connection to close
        // automatically after the transfer is finished.
        {
          std::unique_ptr<Connection> file_connection = get_file_connection();
          file_connection->write(path);
        }

        check_data_transfer(path.filename().string());
      }

      connection->write("<STOP>", true);

      if (transfer_error) {
        print("One or more file transfer errors have occurred! Your profiling results may be "
              "incomplete.", true, true);
      }
    } else {
      for (const fs::path &path : perf_map_paths) {
        std::ifstream stream(path);
        std::ofstream ostream(result_processed / path.filename());
        std::vector<std::string> result;

        read_and_demangle_symbol_map(stream, result);

        for (std::string &new_line : result) {
          ostream << new_line << std::endl;
        }
      }

      if (!src_paths.empty()) {
        try {
          Archive archive(result_processed / "src.zip");
          nlohmann::json src_mapping = nlohmann::json::object();

          for (const fs::path &path : src_paths) {
            std::string filename = std::to_string(src_mapping.size()) + path.extension().string();
            src_mapping[path.string()] = filename;
            archive.add_file(filename, path);
          }

          std::string src_mapping_str = nlohmann::to_string(src_mapping) + '\n';
          std::stringstream s;
          s << src_mapping_str;
          archive.add_file_stream("index.json", s, src_mapping_str.length());

          archive.close();
        } catch (nlohmann::json::exception &e) {
          print("A JSON error related to creating the source code archive has occurred! "
                "Details: " + std::string(e.what()), true, true);
        } catch (Archive::Exception &e) {
          print("A source code archive creation error has occurred! "
                "Details: " + std::string(e.what()), true, true);
        }
      }

      if (!sources_json.empty()) {
        std::ofstream ostream(result_processed / "sources.json");
        ostream << sources_json << std::endl;
      }
    }

    msg = connection->read();

    if (msg != "finished") {
      print("adaptiveperf-server has not indicated its successful completion! Exiting.",
            true, true);
      return 2;
    }

    if (server_address == "") {
      fs::copy(results_dir, fs::current_path() / results_dir.filename(),
               fs::copy_options::recursive);
    }

    auto overall_end_time =
      ch::duration_cast<ch::milliseconds>(ch::system_clock::now().time_since_epoch()).count();

    print("Command execution and post-processing done in ~" +
          std::to_string(overall_end_time - start_time) + " ms!", false, false);

    return 0;
  }
};
