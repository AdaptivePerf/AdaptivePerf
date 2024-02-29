#include "CLI11.hpp"
#include "json.hpp"
#include "socket.hpp"
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

  struct line {
    std::string event_type;
    unsigned long long timestamp;
    unsigned long long period;
    std::vector<std::string> callchain_parts;
    bool stop = false;
  };

  struct line_queue {
    std::queue<struct line> q;
    std::condition_variable cond;
    std::mutex m;
  };

  struct offcpu_region {
    unsigned long long timestamp;
    unsigned long long period;
  };

  struct sample_result {
    std::string event_type;
    nlohmann::json output;
    nlohmann::json output_time_ordered;
    unsigned long long total_period;
    std::vector<struct offcpu_region> offcpu_regions;
  };

  struct accept_notify {
    unsigned int accepted = 0;
    std::mutex m;
    std::condition_variable cond;
  };

  template<class T>
  inline bool queue_pop(std::queue<T> &q, T &res) {
    if (q.empty()) {
      return false;
    }

    res = q.front();
    q.pop();

    return true;
  }

  void recurse(nlohmann::json &cur_elem,
               std::vector<std::string> &callchain_parts,
               int callchain_index,
               unsigned long long period,
               bool time_ordered) {
    std::string p = callchain_parts[callchain_index];
    nlohmann::json &arr = cur_elem["children"];
    nlohmann::json *elem;

    if (time_ordered) {
       if (arr.empty() || arr.back()["name"] != p ||
           (callchain_index == callchain_parts.size() - 1 &&
            !arr.back()["children"].empty()) ||
           (callchain_index < callchain_parts.size() - 1 &&
            arr.back()["children"].empty())) {
         nlohmann::json new_elem;
         new_elem["name"] = p;
         new_elem["value"] = 0;
         new_elem["children"] = nlohmann::json::array();
         arr.push_back(new_elem);
       }

       elem = &arr.back();
    } else {
      bool found = false;
      for (int i = 0; i < arr.size(); i++) {
        if (arr[i]["name"] == p) {
          elem = &arr[i];
          found = true;
          break;
        }
      }

      if (!found) {
        nlohmann::json new_elem;
        new_elem["name"] = p;
        new_elem["value"] = 0;
        new_elem["children"] = nlohmann::json::array();

        arr.push_back(new_elem);
        elem = &arr.back();
      }
    }

    (*elem)["value"] = (unsigned long long)(*elem)["value"] + period;

    if (callchain_index < callchain_parts.size() - 1) {
      recurse(*elem, callchain_parts, callchain_index + 1, period,
              time_ordered);
    }
  }

  struct sample_result process_sample(std::shared_ptr<struct line_queue> q_ptr,
                                      std::string pid, std::string tid,
                                      std::string extra_event_name) {
    try {
      nlohmann::json output;

      output["name"] = "all";
      output["value"] = 0;
      output["children"] = nlohmann::json::array();

      nlohmann::json output_time_ordered;

      output_time_ordered["name"] = "all";
      output_time_ordered["value"] = 0;
      output_time_ordered["children"] = nlohmann::json::array();

      struct out {
        unsigned long long timestamp;
        std::vector<std::string> callchain_parts;
        unsigned long long period;
      };

      std::vector<struct out> to_output_time_ordered;
      std::vector<struct offcpu_region> offcpu_regions;
      unsigned long long total_period = 0;

      std::string pid_tid_name = pid + "_" + tid;

      while (true) {
        std::unique_lock lock(q_ptr->m);
        struct line l;

        while (!queue_pop(q_ptr->q, l)) {
          q_ptr->cond.wait(lock);
        }

        if (l.stop) {
          break;
        }

        if (l.event_type == "offcpu-time") {
          if (!l.callchain_parts.empty()) {
            l.callchain_parts[l.callchain_parts.size() - 1] =
              "[cold]_" + l.callchain_parts[l.callchain_parts.size() - 1];
          } else {
            l.callchain_parts[l.callchain_parts.size() - 1] =
              "[cold]_(just thread/process)";
          }

          struct offcpu_region reg;
          reg.timestamp = l.timestamp;
          reg.period = l.period;
          offcpu_regions.push_back(reg);
        }

        recurse(output, l.callchain_parts, 0, l.period, false);

        struct out new_elem;
        new_elem.timestamp = l.timestamp;
        new_elem.callchain_parts = l.callchain_parts;
        new_elem.period = l.period;
        to_output_time_ordered.push_back(new_elem);
        total_period += l.period;
      }

      output["value"] = total_period;
      output_time_ordered["value"] = total_period;

      sort(to_output_time_ordered.begin(),
           to_output_time_ordered.end(),
           [] (struct out &a, struct out &b) {
             return a.timestamp < b.timestamp;
           });

      for (int i = 0; i < to_output_time_ordered.size(); i++) {
        struct out out_elem = to_output_time_ordered[i];

        recurse(output_time_ordered, out_elem.callchain_parts,
                0, out_elem.period, true);
      }

      struct sample_result result;

      result.output = output;
      result.output_time_ordered = output_time_ordered;

      if (extra_event_name == "") {
        result.event_type = "standard";
        result.total_period = total_period;
        result.offcpu_regions = offcpu_regions;
      } else {
        result.event_type = extra_event_name;
      }

      return result;
    } catch (...) {
      std::rethrow_exception(std::current_exception());
    }
  }

  nlohmann::json run_post_processing(std::shared_ptr<Acceptor> init_socket,
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
          std::pair<
            std::shared_future<struct sample_result>,
            std::shared_ptr<struct line_queue> > > > subprocesses;
      std::unordered_map<std::string, std::string> combo_dict;
      std::unordered_map<std::string, std::string> process_group_dict;
      std::unordered_map<std::string, unsigned long long> time_dict, exit_time_dict, exit_group_time_dict;
      std::unordered_map<std::string, std::string> name_dict;
      std::unordered_map<std::string, std::string> tree;

      std::string extra_event_name = "";
      std::vector<std::string> added_list;

      while (true) {
        std::string line = socket->read();

        if (line == "<STOP>") {
          break;
        }

        nlohmann::json arr = nlohmann::json::parse(line);
        messages_received.insert(arr[0]);

        if (arr[0] == "<SYSCALL>") {
          std::string ret_value = arr[1];
          std::vector<std::string> callchain = arr[2];

          tid_dict[ret_value] = callchain;
        } else if (arr[0] == "<SYSCALL_TREE>") {
          std::string syscall_type = arr[1];
          std::string comm_name = arr[2];
          std::string pid = arr[3];
          std::string tid = arr[4];
          unsigned long long time = arr[5];
          std::string ret_value = arr[6];

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
                added_list.push_back(tid);

                combo_dict[tid] =
                  pid + "/" + tid;
                process_group_dict[tid] = pid;

                if (name_dict.find(tid) == name_dict.end()) {
                  name_dict[tid] = comm_name;
                }
              }

              if (tree.find(ret_value) == tree.end()) {
                added_list.push_back(ret_value);
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
          std::string event_type = arr[1];
          std::string pid = arr[2];
          std::string tid = arr[3];
          unsigned long long timestamp = arr[4];
          unsigned long long period = arr[5];
          std::vector<std::string> callchain = arr[6];

          if (event_type == "offcpu-time" || event_type == "task-clock") {
            extra_event_name = "";
          } else {
            extra_event_name = event_type;
          }

          if (subprocesses.find(pid) == subprocesses.end()) {
            std::unordered_map<
              std::string,
              std::pair<std::shared_future<struct sample_result>,
                        std::shared_ptr<struct line_queue> > > new_map;
            subprocesses[pid] = new_map;
          }

          if (subprocesses[pid].find(tid) == subprocesses[pid].end()) {
            std::shared_ptr<struct line_queue> q_ptr =
              std::make_shared<struct line_queue>();
            std::shared_future<struct sample_result> fut =
              std::async(process_sample,
                         q_ptr, pid, tid, extra_event_name);

            subprocesses[pid][tid] = std::make_pair(fut, q_ptr);
          }

          struct line l;
          l.event_type = event_type;
          l.timestamp = timestamp;
          l.period = period;
          l.callchain_parts = callchain;

          {
            std::lock_guard lock(subprocesses[pid][tid].second->m);
            subprocesses[pid][tid].second->q.push(l);
          }
          subprocesses[pid][tid].second->cond.notify_all();
        }
      }

      socket->close();

      nlohmann::json result;

      for (auto &msg : messages_received) {
        std::string msg_key;
        if (msg == "<SAMPLE>" && extra_event_name != "") {
          msg_key = "<SAMPLE> " + extra_event_name;
        } else {
          msg_key = msg;
        }

        if (msg == "<SYSCALL>") {
          result[msg_key] = tid_dict;
        } else if (msg == "<SYSCALL_TREE>") {
          unsigned long long start_time;
          bool uninitialised = true;

          for (auto &time_elem : time_dict) {
            if (uninitialised || time_elem.second < start_time) {
              start_time = time_elem.second;
              uninitialised = false;
            }
          }

          nlohmann::json result_list;

          for (int i = 0; i < added_list.size(); i++) {
            std::string k = added_list[i];
            std::string p = tree[k];

            nlohmann::json elem;
            elem["identifier"] = added_list[i];
            elem["tag"] = nlohmann::json::array();
            elem["tag"][0] = name_dict[k];
            elem["tag"][1] = combo_dict[k];
            elem["tag"][2] = time_dict[k] - start_time;

            if (process_group_dict.find(k) != process_group_dict.end()) {
              std::string k2 = process_group_dict[k];
              if (exit_group_time_dict.find(k2) != exit_group_time_dict.end()) {
                elem["tag"][3] = exit_group_time_dict[k2] - time_dict[k];
              } else if (exit_time_dict.find(k) != exit_time_dict.end()) {
                elem["tag"][3] = exit_time_dict[k] - time_dict[k];
              } else {
                elem["tag"][3] = -1;
              }
            } else if (exit_time_dict.find(k) != exit_time_dict.end()) {
              elem["tag"][3] = exit_time_dict[k] - time_dict[k];
            } else {
              elem["tag"][3] = -1;
            }

            elem["parent"] = p;

            result_list.push_back(elem);
          }

          result[msg_key] = result_list;
        } else if (msg == "<SAMPLE>") {
          nlohmann::json result_dict;

          for (auto &elem : subprocesses) {
            for (auto &elem2 : elem.second) {
              {
                std::lock_guard lock(elem2.second.second->m);
                struct line l;
                l.stop = true;
                elem2.second.second->q.push(l);
              }
              elem2.second.second->cond.notify_all();

              struct sample_result res = elem2.second.first.get();

              nlohmann::json pid_tid_result;

              nlohmann::json arr = {
                res.output,
                res.output_time_ordered
              };

              pid_tid_result[res.event_type] = arr;

              if (res.event_type == "standard") {
                pid_tid_result["total_period"] = res.total_period;
                pid_tid_result["offcpu_regions"] = nlohmann::json::array();

                for (int i = 0; i < res.offcpu_regions.size(); i++) {
                  nlohmann::json offcpu_arr = {
                    res.offcpu_regions[i].timestamp,
                    res.offcpu_regions[i].period
                  };

                  pid_tid_result["offcpu_regions"].push_back(offcpu_arr);
                }
              }

              result_dict[elem.first + "_" + elem2.first] = pid_tid_result;
            }
          }

          result[msg_key] = result_dict;
        }
      }

      return result;
    } catch (...) {
      std::rethrow_exception(std::current_exception());
    }
  }

  int process_client(std::shared_ptr<Socket> socket, std::string address,
                     unsigned short port, unsigned int buf_size) {
    try {
      std::string msg = socket->read();
      std::regex start_regex("^start(\\d+) (.+)$");
      std::smatch match;

      if (!std::regex_search(msg, match, start_regex)) {
        socket->write("error_wrong_command");
        socket->close();
        return -1;
      }

      int ports = std::stoi(match[1]);
      std::string result_dir = match[2];

      fs::path result_path(result_dir);
      fs::path processed_path = result_path / "processed";

      try {
        fs::create_directory(result_dir);
        fs::create_directory(processed_path);
      } catch (std::exception &e) {
        std::cerr << e.what() << std::endl;
        socket->write("error_result_dir");
        socket->close();
        return -2;
      }

      std::string profiled_filename = socket->read();
      std::shared_future<nlohmann::json> threads[ports];
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

      nlohmann::json final_result;

      for (int i = 0; i < ports; i++) {
        nlohmann::json thread_result = threads[i].get();

        if (!thread_result.is_null()) {
          final_result.update(threads[i].get(), true);
        }
      }

      nlohmann::json final_output;
      nlohmann::json metadata;

      for (auto &elem : final_result.items()) {
        if (elem.key() == "<SYSCALL_TREE>") {
          metadata["thread_tree"] = elem.value();
        } else if (elem.key() == "<SYSCALL>") {
          for (auto &elem2 : elem.value().items()) {
            metadata["callchains"][elem2.key()] = elem2.value();
          }
        } else if (elem.key().rfind("<SAMPLE>", 0) == 0) {
          for (auto &elem2 : elem.value().items()) {
            for (auto &elem3 : elem2.value().items()) {
              if (elem3.key() == "sampled_time") {
                metadata["sampled_times"][elem2.key()] = elem3.value();
              } else if (elem3.key() == "offcpu_regions") {
                metadata["offcpu_regions"][elem2.key()] = elem3.value();
              } else {
                final_output[elem2.key()][elem3.key()] = elem3.value();
              }
            }
          }
        }
      }

      std::ofstream metadata_f;
      metadata_f.open(processed_path / "metadata.json");
      metadata_f << metadata << std::endl;
      metadata_f.close();

      for (auto &elem : final_output.items()) {
        std::ofstream f;
        f.open(processed_path / (elem.key() + ".json"));
        f << elem.value() << std::endl;
        f.close();
      }

      socket->write("finished");
      socket->close();
      return 0;
    } catch (...) {
      std::rethrow_exception(std::current_exception());
    }
  }

  void run_server(std::string address, unsigned short port,
                 bool quiet, unsigned int max_connections,
                 unsigned int buf_size) {
    try {
      Acceptor acceptor(address, port, false);
      std::vector<std::future<int> > threads;

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
                                       address, port, buf_size));

          if (max_connections == 0) {
            break;
          }
        }
      }

      for (int i = 0; i < threads.size(); i++) {
        threads[i].get();
      }

      acceptor.close();
    } catch (...) {
      std::rethrow_exception(std::current_exception());
    }
  }
}

int main(int argc, char **argv) {
  CLI::App app("Post-processing server for AdaptivePerf");

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

  bool quiet = false;
  app.add_flag("-q", quiet, "Do not print anything, including errors");

  CLI11_PARSE(app, argc, argv);

  try {
    aperf::run_server(address, port, quiet, max_connections, buf_size);
    return 0;
  } catch (aperf::AlreadyInUseException &e) {
    if (!quiet) {
      std::cerr << address << ":" << port << " is in use! Please use a ";
      std::cerr << "different address and/or port." << std::endl;
    }

    return 100;
  }
}
