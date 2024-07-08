// AdaptivePerf: comprehensive profiling tool based on Linux perf
// Copyright (C) CERN. See LICENSE for details.

#define __USE_POSIX

#include "profiling.hpp"
#include "print.hpp"
#include "server/server.hpp"
#include <unistd.h>
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
  bool CPUConfig::is_valid() {
    return this->valid;
  }

  /**
     Returns the number of profiler threads that can be spawned
     based on how many cores are allowed for doing the profiling.
  */
  int CPUConfig::get_profiler_thread_count() {
    return this->profiler_thread_count;
  }

  /**
     Returns the sched_setaffinity-compatible CPU set for doing
     the profiling.
  */
  cpu_set_t & CPUConfig::get_cpu_profiler_set() {
    return this->cpu_profiler_set;
  }

  /**
     Returns the sched_setaffinity-compatible CPU set for running
     the profiled command.
  */
  cpu_set_t & CPUConfig::get_cpu_command_set() {
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
     @param command          A command to be profiled.
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
  */
  int start_profiling_session(std::vector<std::unique_ptr<Profiler> > &profilers,
                              std::string command, std::string server_address,
                              unsigned int buf_size, unsigned int warmup,
                              CPUConfig &cpu_config, fs::path tmp_dir,
                              std::vector<pid_t> &spawned_children) {
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

    std::vector<std::string> command_elements = boost::program_options::split_unix(command);
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

    print("Starting profiled program wrapper...", true, false);

    int parent_to_wrapper_pipe[2];

    if (pipe(parent_to_wrapper_pipe) == -1) {
      print("Could not create communication pipe for the profiled command wrapper! Exiting.",
            true, true);
      return 2;
    }

    const int ERROR_START_PROFILE = 200;
    const int ERROR_STDOUT = 201;
    const int ERROR_STDERR = 202;
    const int ERROR_STDOUT_DUP2 = 203;
    const int ERROR_STDERR_DUP2 = 204;
    const int ERROR_AFFINITY = 205;

    std::vector<std::string> parts = boost::program_options::split_unix(command);

    const char *path = parts[0].c_str();
    char *parts_c_str[parts.size() + 1];

    for (int i = 0; i < parts.size(); i++) {
      parts_c_str[i] = (char *)parts[i].c_str();
    }

    parts_c_str[parts.size()] = NULL;

    pid_t forked = fork();

    if (forked == 0) {
      // This executes in a separate process with everything effectively copied (NOT shared!)
      close(parent_to_wrapper_pipe[1]);
      char buf;
      int bytes_read = 0;
      int received = ::read(parent_to_wrapper_pipe[0], &buf, 1);
      close(parent_to_wrapper_pipe[0]);

      if (received <= 0 || buf != 0x03) {
        std::exit(ERROR_START_PROFILE);
      }

      int stdout_fd = creat((result_out / "stdout.log").c_str(), O_WRONLY);

      if (stdout_fd == -1) {
        std::exit(ERROR_STDOUT);
      }

      int stderr_fd = creat((result_out / "stderr.log").c_str(), O_WRONLY);

      if (stderr_fd == -1) {
        std::exit(ERROR_STDERR);
      }

      if (dup2(stdout_fd, STDOUT_FILENO) == -1) {
        std::exit(ERROR_STDOUT_DUP2);
      }

      if (dup2(stderr_fd, STDERR_FILENO) == -1) {
        std::exit(ERROR_STDERR_DUP2);
      }

      cpu_set_t &cpu_set = cpu_config.get_cpu_command_set();

      if (sched_setaffinity(0, sizeof(cpu_set), &cpu_set) == -1) {
        std::exit(ERROR_AFFINITY);
      }

      execvp(path, parts_c_str);

      // This is reached only if execvp fails
      std::exit(errno);
    }

    close(parent_to_wrapper_pipe[0]);
    spawned_children.push_back(forked);

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
      profilers[i]->start(forked, connection_instrs, result_out,
                          result_processed, true);
    }

    print("Waiting for profilers to signal their readiness. If AdaptivePerf "
          "hangs here, you may want to check the files in " +
          tmp_dir.string() + ".", true, false);

    auto profiler_and_wrapper_handler =
      [&](int status, long start_time, long end_time) {
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

        int code = WEXITSTATUS(status);

        if (code == 0) {
          if (start_time != -1 && end_time != -1) {
            print("Command execution completed in ~" +
                      std::to_string(end_time - start_time) + " ms!",
                  true, false);
          }
        } else if (code == ENOENT) {
          print("Provided command does not exist!", true, true);
          error = true;
        } else if (code == EACCES) {
          print("Cannot access the provided command!", true, true);
          print("Hint: You may want to mark your file as executable by running \"chmod +x <file>\".", true, true);
          error = true;
        } else {
          print("Profiled program wrapper has finished with non-zero exit code " +
                std::to_string(code) + ".", true, true);

          switch (code) {
          case ERROR_START_PROFILE:
            print("Hint: Code " + std::to_string(code) + " suggests something "
                  "bad happened when instructing the wrapper to execute the "
                  "profiled command.", true, true);
            break;

          case ERROR_STDOUT:
            print("Hint: Code " + std::to_string(code) + " suggests something "
                  "bad happened when opening the stdout log file for writing.", true, true);
            break;

          case ERROR_STDERR:
            print("Hint: Code " + std::to_string(code) + " suggests something "
                  "bad happened when opening the stderr log file for writing.", true, true);
            break;

          case ERROR_STDOUT_DUP2:
            print("Hint: Code " + std::to_string(code) + " suggests something "
                  "bad happened when redirecting stdout of the profiled command "
                  "wrapper to the stdout log file.", true, true);
            break;

          case ERROR_STDERR_DUP2:
            print("Hint: Code " + std::to_string(code) + " suggests something "
                  "bad happened when redirecting stderr of the profiled command "
                  "wrapper to the stderr log file.", true, true);
            break;

          case ERROR_AFFINITY:
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
        int status;
        waitpid(forked, &status, WNOHANG);

        if (status != 0) {
          int code = profiler_and_wrapper_handler(status, -1, -1);

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
    print("Executing the command...", true, false);

    auto start_time =
      ch::duration_cast<ch::milliseconds>(ch::system_clock::now().time_since_epoch()).count();

    char to_send = 0x03;
    ::write(parent_to_wrapper_pipe[1], &to_send, 1);

    int status;

    pid_t result = waitpid(forked, &status, 0);

    if (result != forked) {
      print("Could not properly wait for the profiled program or its wrapper to finish! Exiting.",
            true, true);
      return 2;
    }

    auto end_time =
      ch::duration_cast<ch::milliseconds>(ch::system_clock::now().time_since_epoch()).count();

    int code = profiler_and_wrapper_handler(status, start_time, end_time);

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
    std::vector<fs::path> to_remove;

    for (auto &elem : fs::directory_iterator(result_processed)) {
      if (!elem.is_regular_file()) {
        continue;
      }

      fs::path path = elem.path();

      if (std::regex_match(path.filename().string(),
                           std::regex("^perf_map_paths_.*\\.data$"))) {
        std::ifstream stream(path);

        while (stream) {
          std::string line;
          std::getline(stream, line);
          boost::trim(line);

          if (!line.empty()) {
            perf_map_paths.insert(fs::path(line));
          }
        }

        to_remove.push_back(path);
      }
    }

    for (auto &path : to_remove) {
      fs::remove(path);
    }

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

      auto check_file_transfer = [&](const fs::path &path) {
        std::string status = connection->read();

        if (status == "error_out_file") {
          print("Could not send " + path.filename().string() + "!", true, true);
          transfer_error = true;
        } else if (status == "error_out_file_timeout") {
          print("Could not send " + path.filename().string() + " due to timeout!",
                true, true);
          transfer_error = true;
        } else if (status != "out_file_ok") {
          print("Could not obtain confirmation of correct transfer of " +
                path.filename().string() + "!", true, true);
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

          while (stream) {
            std::string line;
            std::getline(stream, line);

            std::vector<std::string> parts;
            boost::split(parts, line, boost::is_any_of(" "));

            std::string new_line = "";

            for (int i = 0; i < parts.size(); i++) {
              new_line += boost::core::demangle(parts[i].c_str()) +
                (i < parts.size() - 1 ? " " : "");
            }

            file_connection->write(new_line);
          }
        }

        check_file_transfer(path);
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

        check_file_transfer(path);
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

        check_file_transfer(path);
      }

      connection->write("<STOP>", true);

      if (transfer_error) {
        print("One or more file transfer errors have occurred! Your profiling results may be "
              "incomplete.", true, true);
      }
    } else {
      for (const fs::path &path : perf_map_paths) {
        fs::copy(path, result_processed);
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
