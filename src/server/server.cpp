// AdaptivePerf: comprehensive profiling tool based on Linux perf
// Copyright (C) CERN. See LICENSE for details.

#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>
#include "socket.hpp"
#include "version.hpp"
#include <string>
#include <iostream>
#include <thread>
#include <future>
#include <regex>
#include <filesystem>
#include <condition_variable>
#include <mutex>
#include <fstream>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <chrono>
#include <queue>
#include <functional>

namespace aperf {
  using namespace std::chrono_literals;
  namespace fs = std::filesystem;

  struct offcpu_region {
    unsigned long long timestamp;
    unsigned long long period;
  };

  struct out_time_ordered {
    unsigned long long timestamp;
    std::vector<std::string> callchain_parts;
    unsigned long long period;
    bool offcpu;
  };

  struct sample_result {
    std::string event_type;
    nlohmann::json output;
    nlohmann::json output_time_ordered;
    std::vector<struct out_time_ordered> to_output_time_ordered;
    unsigned long long total_period = 0;
    std::vector<struct offcpu_region> offcpu_regions;
  };

  struct accept_notify {
    unsigned int accepted = 0;
    std::mutex m;
    std::condition_variable cond;
  };

  void recurse(nlohmann::json &cur_elem,
               std::vector<std::string> &callchain_parts,
               int callchain_index,
               unsigned long long period,
               bool time_ordered, bool offcpu) {
    std::string p = callchain_parts[callchain_index];
    nlohmann::json &arr = cur_elem["children"];
    nlohmann::json *elem;

    bool last_block = callchain_index == callchain_parts.size() - 1;

    if (!offcpu) {
      cur_elem["cold"] = false;
    }

    if (time_ordered) {
       if (arr.empty() || arr.back()["name"] != p ||
           (last_block &&
            arr.back()["cold"] != offcpu) ||
           (last_block &&
            !arr.back()["children"].empty()) ||
           (!last_block &&
            arr.back()["children"].empty())) {
         nlohmann::json new_elem;
         new_elem["name"] = p;
         new_elem["value"] = 0;
         new_elem["children"] = nlohmann::json::array();
         new_elem["cold"] = offcpu;
         arr.push_back(new_elem);
       }

       elem = &arr.back();
    } else {
      bool found = false;
      int cold_index = -1;
      int hot_index = -1;

      for (int i = 0; i < arr.size(); i++) {
        if (arr[i]["name"] == p && (!last_block || arr[i]["cold"] == offcpu)) {
          found = true;

          if (arr[i]["cold"]) {
            cold_index = i;
          } else {
            hot_index = i;
          }
        }
      }

      if (found) {
        if (cold_index == -1) {
          elem = &arr[hot_index];
        } else if (hot_index == -1) {
          elem = &arr[cold_index];
        } else if (offcpu) {
          elem = &arr[cold_index];
        } else {
          elem = &arr[hot_index];
        }
      } else {
        nlohmann::json new_elem;
        new_elem["name"] = p;
        new_elem["value"] = 0;
        new_elem["children"] = nlohmann::json::array();
        new_elem["cold"] = offcpu;

        arr.push_back(new_elem);
        elem = &arr.back();
      }
    }

    (*elem)["value"] = (unsigned long long)(*elem)["value"] + period;

    if (!last_block) {
      recurse(*elem, callchain_parts, callchain_index + 1, period,
              time_ordered, offcpu);
    }
  }

  std::shared_ptr<nlohmann::json> run_post_processing(std::shared_ptr<Acceptor> init_socket,
                                                      std::string profiled_filename,
                                                      unsigned int buf_size,
                                                      std::shared_ptr<struct accept_notify> notifier) {
    try {
      std::shared_ptr<Socket> socket = init_socket->accept(buf_size);
      {
        std::lock_guard lock(notifier->m);
        notifier->accepted++;
      }
      notifier->cond.notify_all();

      std::unordered_set<std::string> messages_received;
      std::unordered_map<std::string, std::vector<std::string> > tid_dict;
      std::unordered_map<
        std::string,
        std::unordered_map<
          std::string,
          struct sample_result > > subprocesses;
      std::unordered_map<std::string, std::string> combo_dict;
      std::unordered_map<std::string, std::string> process_group_dict;
      std::unordered_map<std::string, unsigned long long> time_dict, exit_time_dict, exit_group_time_dict;
      std::unordered_map<std::string, std::string> name_dict;
      std::unordered_map<std::string, std::string> tree;

      std::string extra_event_name = "";
      std::vector<std::pair<unsigned long long, std::string> > added_list;

      while (true) {
        std::string line = socket->read();

        if (line == "<STOP>") {
          break;
        }

        nlohmann::json arr;

        try {
          arr = nlohmann::json::parse(line);
        } catch (...) {
          std::cerr << "Could not parse the recently-received line to JSON, ignoring." << std::endl;
          continue;
        }

        if (!arr.is_array() || arr.empty()) {
          std::cerr << "The recently-received JSON is not a non-empty array, ignoring." << std::endl;
          continue;
        }

        messages_received.insert(arr[0]);

        if (arr[0] == "<SYSCALL>") {
          std::string ret_value;
          std::vector<std::string> callchain;

          try {
            ret_value = arr[1];
            callchain = arr[2];
          } catch (...) {
            std::cerr << "The recently-received syscall JSON is invalid, ignoring." << std::endl;
            continue;
          }

          tid_dict[ret_value] = callchain;
        } else if (arr[0] == "<SYSCALL_TREE>") {
          std::string syscall_type, comm_name, pid, tid, ret_value;
          unsigned long long time;

          try {
            syscall_type = arr[1];
            comm_name = arr[2];
            pid = arr[3];
            tid = arr[4];
            time = arr[5];
            ret_value = arr[6];
          } catch (...) {
            std::cerr << "The recently-received syscall tree JSON is invalid, ignoring." << std::endl;
            continue;
          }

          if (syscall_type == "clone3" ||
              syscall_type == "clone" ||
              syscall_type == "vfork" ||
              syscall_type == "fork") {
            if (ret_value == "0") {
              combo_dict[tid] = pid + "/" + tid;
              process_group_dict[tid] = pid;

              if (time_dict.find(tid) == time_dict.end()) {
                time_dict[tid] = time;
              }

              if (name_dict.find(tid) == name_dict.end()) {
                name_dict[tid] = comm_name;
              }
            } else {
              if (tree.find(tid) == tree.end()) {
                tree[tid] = "";
                added_list.push_back(std::make_pair(time, tid));

                combo_dict[tid] = pid + "/" + tid;
                process_group_dict[tid] = pid;

                if (name_dict.find(tid) == name_dict.end()) {
                  name_dict[tid] = comm_name;
                }
              }

              if (tree.find(ret_value) == tree.end()) {
                added_list.push_back(std::make_pair(time, ret_value));
              }

              tree[ret_value] = tid;
            }
          } else if (syscall_type == "execve") {
            time_dict[tid] = time;
            name_dict[tid] = comm_name;
          } else if (syscall_type == "exit_group") {
            exit_group_time_dict[pid] = time;
          } else if (syscall_type == "exit") {
            exit_time_dict[tid] = time;
          }
        } else if (arr[0] == "<SAMPLE>") {
          std::string event_type, pid, tid;
          unsigned long long timestamp, period;
          std::vector<std::string> callchain;
          try {
            event_type = arr[1];
            pid = arr[2];
            tid = arr[3];
            timestamp = arr[4];
            period = arr[5];
            callchain = arr[6];
          } catch (...) {
            std::cerr << "The recently received sample JSON is invalid, ignoring." << std::endl;
            continue;
          }

          if (event_type == "offcpu-time" || event_type == "task-clock") {
            extra_event_name = "";
          } else {
            extra_event_name = event_type;
          }

          if (subprocesses.find(pid) == subprocesses.end()) {
            std::unordered_map<std::string, struct sample_result > new_map;
            subprocesses[pid] = new_map;
          }

          if (subprocesses[pid].find(tid) == subprocesses[pid].end()) {
            subprocesses[pid][tid] = {};
            struct sample_result &res = subprocesses[pid][tid];

            res.output["name"] = "all";
            res.output["value"] = 0;
            res.output["children"] = nlohmann::json::array();

            res.output_time_ordered["name"] = "all";
            res.output_time_ordered["value"] = 0;
            res.output_time_ordered["children"] = nlohmann::json::array();
          }

          struct sample_result &res = subprocesses[pid][tid];

          if (event_type == "offcpu-time") {
            if (callchain.size() <= 1) {
              callchain.push_back("(just thread/process)");
            }

            struct offcpu_region reg;
            reg.timestamp = timestamp;
            reg.period = period;
            res.offcpu_regions.push_back(reg);
          }

          recurse(res.output, callchain, 0, period, false,
                  event_type == "offcpu-time");

          struct out_time_ordered new_elem;
          new_elem.timestamp = timestamp;
          new_elem.callchain_parts = callchain;
          new_elem.period = period;
          new_elem.offcpu = event_type == "offcpu-time";
          res.to_output_time_ordered.push_back(new_elem);
          res.total_period += period;
        }
      }

      socket->close();

      std::sort(added_list.begin(), added_list.end(),
                [] (auto &a, auto &b) { return a.first < b.first; });

      std::shared_ptr<nlohmann::json> result = std::make_shared<nlohmann::json>();

      for (auto &msg : messages_received) {
        std::string msg_key;
        if (msg == "<SAMPLE>" && extra_event_name != "") {
          msg_key = "<SAMPLE> " + extra_event_name;
        } else {
          msg_key = msg;
        }

        if (msg == "<SYSCALL>") {
          (*result)[msg_key] = tid_dict;
        } else if (msg == "<SYSCALL_TREE>") {
          unsigned long long start_time = 0;
          bool start_time_uninitialised = true;

          for (auto &time_elem : time_dict) {
            if (start_time_uninitialised || time_elem.second < start_time) {
              start_time = time_elem.second;
              start_time_uninitialised = false;
            }
          }

          (*result)[msg_key] = nlohmann::json::array();
          (*result)[msg_key].push_back(start_time);
          (*result)[msg_key].push_back(nlohmann::json::array());

          nlohmann::json &result_list = (*result)[msg_key][1];
          std::unordered_set<std::string> added_identifiers;
          bool profile_start = false;

          for (int i = 0; i < added_list.size(); i++) {
            std::string k = added_list[i].second;
            std::string p = tree[k];

            if (!profile_start) {
              if (name_dict[k] == profiled_filename.substr(0, 15)) {
                profile_start = true;
                p = "";
              } else {
                if (p.empty()) {
                  tree[k] = "<INVALID>";
                }

                continue;
              }
            }

            if (!p.empty() && added_identifiers.find(p) == added_identifiers.end()) {
              continue;
            }

            added_identifiers.insert(k);

            nlohmann::json elem;
            elem["identifier"] = k;
            elem["tag"] = nlohmann::json::array();
            elem["tag"][0] = name_dict[k];
            elem["tag"][1] = combo_dict[k];
            elem["tag"][2] = time_dict[k] - start_time;

            if (exit_time_dict.find(k) != exit_time_dict.end()) {
              elem["tag"][3] = exit_time_dict[k] - time_dict[k];
            } else if (process_group_dict.find(k) != process_group_dict.end() &&
                       exit_group_time_dict.find(process_group_dict[k]) != exit_group_time_dict.end()) {
              elem["tag"][3] = exit_group_time_dict[process_group_dict[k]] - time_dict[k];
            } else {
              elem["tag"][3] = -1;
            }

            if (p.empty()) {
              elem["parent"] = nullptr;
            } else {
              elem["parent"] = p;
            }

            result_list.push_back(elem);
          }
        } else if (msg == "<SAMPLE>") {
          for (auto &elem : subprocesses) {
            for (auto &elem2 : elem.second) {
              struct sample_result &res = elem2.second;
              res.output["value"] = res.total_period;
              res.output_time_ordered["value"] = res.total_period;

              std::sort(res.to_output_time_ordered.begin(),
                        res.to_output_time_ordered.end(),
                        [] (struct out_time_ordered &a, struct out_time_ordered &b) {
                          return a.timestamp < b.timestamp;
                        });

              for (int i = 0; i < res.to_output_time_ordered.size(); i++) {
                struct out_time_ordered &out_elem = res.to_output_time_ordered[i];

                recurse(res.output_time_ordered, out_elem.callchain_parts, 0,
                        out_elem.period, true, out_elem.offcpu);
              }

              nlohmann::json &pid_tid_result = (*result)[msg_key][elem.first + "_" + elem2.first];
              std::string event_name;

              if (extra_event_name == "") {
                event_name = "walltime";
                pid_tid_result["sampled_time"] = res.total_period;
                pid_tid_result["offcpu_regions"] = nlohmann::json::array();

                for (int i = 0; i < res.offcpu_regions.size(); i++) {
                  nlohmann::json offcpu_arr = {
                    res.offcpu_regions[i].timestamp,
                    res.offcpu_regions[i].period
                  };

                  pid_tid_result["offcpu_regions"].push_back(offcpu_arr);
                }
              } else {
                event_name = extra_event_name;
              }

              pid_tid_result[event_name] = nlohmann::json::array();
              pid_tid_result[event_name].push_back(res.output);
              pid_tid_result[event_name].push_back(res.output_time_ordered);
            }
          }
        }
      }

      return result;
    } catch (...) {
      std::rethrow_exception(std::current_exception());
    }
  }

  void process_client(std::shared_ptr<Socket> socket, std::string address,
                      unsigned short port, unsigned int buf_size,
                      long file_timeout_seconds) {
    try {
      std::string msg = socket->read();
      std::regex start_regex("^start(\\d+) (.+)$");
      std::smatch match;

      if (!std::regex_search(msg, match, start_regex)) {
        socket->write("error_wrong_command");
        socket->close();
        return;
      }

      int ports = std::stoi(match[1]);
      std::string result_dir = match[2];

      fs::path result_path(result_dir);
      fs::path processed_path = result_path / "processed";
      fs::path out_path = result_path / "out";

      try {
        fs::create_directory(result_dir);
        fs::create_directory(processed_path);
        fs::create_directory(out_path);
      } catch (std::exception &e) {
        std::cerr << "Could not create " << result_dir << "! Error details:";
        std::cerr << std::endl;
        std::cerr << e.what() << std::endl;
        socket->write("error_result_dir");
        socket->close();
        return;
      }

      std::string profiled_filename = socket->read();
      std::shared_future<std::shared_ptr<nlohmann::json> > threads[ports];
      unsigned short final_ports[ports];
      std::shared_ptr<struct accept_notify> notifier =
        std::make_shared<struct accept_notify>();

      for (int i = 0; i < ports; i++) {
        unsigned short p = port + i + 1;

        std::shared_ptr<Acceptor> init_socket =
          std::make_shared<Acceptor>(address, p, true);

        final_ports[i] = init_socket->get_port();
        threads[i] = std::async(run_post_processing, init_socket,
                                profiled_filename, buf_size,
                                notifier);
      }

      std::string port_msg = "";

      for (int i = 0; i < ports; i++) {
        port_msg += std::to_string(final_ports[i]);

        if (i < ports - 1) {
          port_msg += " ";
        }
      }

      socket->write(port_msg);

      std::unique_lock lock(notifier->m);
      while (notifier->accepted < ports) {
        notifier->cond.wait(lock);
      }

      socket->write("start_profile");

      nlohmann::json final_output;
      nlohmann::json metadata;

      for (int i = 0; i < ports; i++) {
        std::shared_ptr<nlohmann::json> thread_result = threads[i].get();
        for (auto &elem : thread_result->items()) {
          if (elem.key() == "<SYSCALL_TREE>") {
            metadata["start_time"].swap(elem.value()[0]);
            metadata["thread_tree"].swap(elem.value()[1]);
          } else if (elem.key() == "<SYSCALL>") {
            for (auto &elem2 : elem.value().items()) {
              metadata["callchains"][elem2.key()].swap(elem2.value());
            }
          } else if (elem.key().rfind("<SAMPLE>", 0) == 0) {
            for (auto &elem2 : elem.value().items()) {
              for (auto &elem3 : elem2.value().items()) {
                if (elem3.key() == "sampled_time") {
                  metadata["sampled_times"][elem2.key()].swap(elem3.value());
                } else if (elem3.key() == "offcpu_regions") {
                  metadata["offcpu_regions"][elem2.key()].swap(elem3.value());
                } else {
                  final_output[elem2.key()][elem3.key()].swap(elem3.value());
                }
              }
            }
          }
        }
      }

      auto save = [](std::string path, nlohmann::json *output) {
        std::ofstream f;
        f.open(path);
        f << *output << std::endl;
        f.close();
      };

      std::shared_future<void> futures[final_output.size() + 1];

      futures[0] = std::async(save, processed_path / "metadata.json",
                              &metadata);

      int future_index = 1;

      for (auto &elem : final_output.items()) {
        futures[future_index++] = std::async(save,
                                             processed_path / (elem.key() + ".json"),
                                             &elem.value());
      }

      for (int i = 0; i < final_output.size() + 1; i++) {
        futures[i].get();
      }

      socket->write("out_files");

      std::vector<std::pair<std::string, unsigned int> > out_files;
      std::vector<std::pair<std::string, unsigned int> > processed_files;

      while (true) {
        std::string x = socket->read();

        if (x == "<STOP>") {
          break;
        }

        std::string len_str = "";
        int index = 0;
        bool error = false;

        for (; index < x.size(); index++) {
          if (x[index] >= '0' && x[index] <= '9') {
            len_str += x[index];
          } else {
            if (x[index++] != ' ') {
              socket->write("error_wrong_file_format");
              error = true;
            } else {
              socket->write("ok");
            }

            break;
          }
        }

        if (index >= x.size() - 2) {
          socket->write("error_wrong_file_format");
          error = true;
        }

        bool processed;

        if (x[index] == 'p') {
          processed = true;
        } else if (x[index] == 'o') {
          processed = false;
        } else {
          socket->write("error_wrong_file_format");
          error = true;
        }

        if (x[index + 1] != ' ') {
          socket->write("error_wrong_file_format");
          error = true;
        }

        if (!error) {
          if (processed) {
            processed_files.push_back(std::make_pair(x.substr(index + 2),
                                                     std::stoi(len_str)));
          } else {
            out_files.push_back(std::make_pair(x.substr(index + 2),
                                               std::stoi(len_str)));
          }
        }
      }

      bool error = false;

      auto process_file = [&processed_path, &out_path, &error, &socket,
                           &file_timeout_seconds]
        (std::string &name,
         unsigned int len,
         bool processed) {
        fs::path path = (processed ? processed_path : out_path) / name;

        try {
          char buf[len];
          int bytes_received = socket->read(buf, len, file_timeout_seconds);

          if (bytes_received != len) {
            std::cerr << "Warning for out/processed file " << path << ": ";
            std::cerr << "Expected " << len << " byte(s), got " << bytes_received << ".";
            std::cerr << std::endl;
          }

          std::ofstream f(path, std::ios_base::out | std::ios_base::binary);

          if (!f) {
            std::cerr << "Error for out/processed file " << path << ": ";
            std::cerr << "Could not open the output stream." << std::endl;
            error = true;
            return;
          }

          f.write(buf, bytes_received);

          if (!f) {
            std::cerr << "Error for out/processed file " << path << ": ";
            std::cerr << "Could not write to the output stream." << std::endl;
            error = true;
            return;
          }

          f.close();
        } catch (TimeoutException &e) {
          std::cerr << "Warning for out/processed file " << path << ": ";
          std::cerr << "Timeout of " << file_timeout_seconds << " s has been reached, no data saved.";
          std::cerr << std::endl;
        }
      };

      for (int i = 0; i < processed_files.size(); i++) {
        process_file(processed_files[i].first,
                     processed_files[i].second, true);
      }

      for (int i = 0; i < out_files.size(); i++) {
        process_file(out_files[i].first,
                     out_files[i].second, false);
      }

      if (error) {
        socket->write("out_file_error");
      } else {
        socket->write("finished");
      }

      socket->close();
    } catch (...) {
      std::rethrow_exception(std::current_exception());
    }
  }

  void run_server(std::string address, unsigned short port,
                  bool quiet, unsigned int max_connections,
                  unsigned int buf_size, long file_timeout_seconds) {
    try {
      Acceptor acceptor(address, port, false);
      std::vector<std::future<void> > threads;

      if (!quiet) {
        std::cout << "Listening on " << address << ", port " << port;
        std::cout << " (TCP)..." << std::endl;
      }

      while (true) {
        std::shared_ptr<Socket> accepted_socket = acceptor.accept(buf_size);

        int working_count = 0;
        for (int i = 0; i < threads.size(); i++) {
          if (threads[i].wait_for(0ms) != std::future_status::ready) {
            working_count++;
          }
        }

        if (working_count >= std::max(1U, max_connections)) {
          accepted_socket->write("try_again");
          accepted_socket->close();
        } else {
          threads.push_back(std::async(process_client, accepted_socket,
                                       address, port, buf_size,
                                       file_timeout_seconds));

          if (max_connections == 0) {
            break;
          }
        }
      }

      acceptor.close();

      for (int i = 0; i < threads.size(); i++) {
        try {
          threads[i].get();
        } catch (aperf::SocketException &e) {
          std::cerr << "Warning: Socket error in client " << i << ", you will not ";
          std::cerr << "get reliable results from them!" << std::endl;

          std::cerr << "Error details: " << e.what() << std::endl;
        }
      }
    } catch (aperf::AlreadyInUseException &e) {
      throw e;
    } catch (aperf::SocketException &e) {
      std::cerr << "A socket error has occurred and adaptiveperf-server has to exit!" << std::endl;
      std::cerr << "You may want to check the address/port settings and the stability of " << std::endl;
      std::cerr << "your connection between the server and the client(s)." << std::endl;
      std::cerr << std::endl;
      std::cerr << "The error details are printed below." << std::endl;
      std::cerr << "----------" << std::endl;
      std::cerr << e.what() << std::endl;

      throw e;
    } catch (...) {
      std::cerr << "A fatal error has occurred and adaptiveperf-server has to exit!" << std::endl;
      std::cerr << "The exception will be rethrown to aid debugging." << std::endl;
      std::cerr << std::endl;
      std::cerr << "If this issue persists, please get in touch with the AdaptivePerf developers." << std::endl;
      std::cerr << "----------" << std::endl;

      std::rethrow_exception(std::current_exception());
    }
  }
}

int main(int argc, char **argv) {
  CLI::App app("Post-processing server for AdaptivePerf");

  bool version = false;
  app.add_flag("-v,--version", version, "Print version and exit");

  std::string address = "127.0.0.1";
  app.add_option("-a", address, "Address to bind to (default: 127.0.0.1)");

  unsigned short port = 5000;
  app.add_option("-p", port, "Port to bind to (default: 5000)");

  unsigned int max_connections = 1;
  app.add_option("-m", max_connections,
                 "Max simultaneous connections to accept "
                 "(default: 1, use 0 to exit after the first client)");

  unsigned int buf_size = 1024;
  app.add_option("-b", buf_size,
                 "Buffer size for communication with clients in bytes "
                 "(default: 1024)");

  long file_timeout_seconds = 120;
  app.add_option("-t", file_timeout_seconds,
                 "Timeout for receiving out and processed data from clients "
                 "in seconds (default: 120)");

  bool quiet = false;
  app.add_flag("-q", quiet, "Do not print anything except non-port-in-use errors");

  CLI11_PARSE(app, argc, argv);

  if (version) {
    std::cout << aperf::version << std::endl;
    return 0;
  } else {
    try {
      aperf::run_server(address, port, quiet, max_connections, buf_size,
                        file_timeout_seconds);
      return 0;
    } catch (aperf::AlreadyInUseException &e) {
      if (!quiet) {
        std::cerr << address << ":" << port << " is in use! Please use a ";
        std::cerr << "different address and/or port." << std::endl;
      }

      return 100;
    } catch (aperf::SocketException &e) {
      return 1;
    } catch (...) {
      std::rethrow_exception(std::current_exception());
    }
  }
}
