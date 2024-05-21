// AdaptivePerf: comprehensive profiling tool based on Linux perf
// Copyright (C) CERN. See LICENSE for details.

#include "server.hpp"
#include <iostream>
#include <unordered_set>
#include <unordered_map>

namespace aperf {
  void StdSubclient::recurse(nlohmann::json &cur_elem,
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

  StdSubclient::StdSubclient(Notifiable &context,
                             std::unique_ptr<Acceptor> &acceptor,
                             std::string profiled_filename,
                             unsigned int buf_size) : InitSubclient(context,
                                                                    acceptor,
                                                                    profiled_filename,
                                                                    buf_size) {

  }

  void StdSubclient::process() {
    struct offcpu_region {
      unsigned long long timestamp;
      unsigned long long period;
    };

    struct sample_result {
      std::string event_type;
      nlohmann::json output;
      nlohmann::json output_time_ordered;
      unsigned long long total_period = 0;
      std::vector<struct offcpu_region> offcpu_regions;
    };

    try {
      std::shared_ptr<Connection> connection = this->acceptor->accept(this->buf_size);
      this->context.notify();

      std::unordered_set<std::string> messages_received;
      std::unordered_map<std::string, std::vector<std::string> > tid_dict;
      std::unordered_map<
        std::string,
        std::unordered_map<
          std::string,
          struct sample_result > > subprocesses;
      std::unordered_map<std::string, std::string> combo_dict;
      std::unordered_map<std::string, unsigned long long> time_dict, exit_time_dict;
      std::unordered_map<std::string, std::string> name_dict;
      std::unordered_map<std::string, std::string> tree;

      std::string extra_event_name = "";
      std::vector<std::pair<unsigned long long, std::string> > added_list;

      std::unordered_map<std::string, std::string> last_clone_flags;

      while (true) {
        std::string line = connection->read();

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

          std::string pid_tid = pid + "/" + tid;

          if (syscall_type == "new_proc") {
            if (tree.find(tid) == tree.end()) {
              tree[tid] = "";
              added_list.push_back(std::make_pair(time, tid));

              combo_dict[tid] = pid + "/" + tid;

              if (name_dict.find(tid) == name_dict.end()) {
                name_dict[tid] = comm_name;
              }
            }

            if (tree.find(ret_value) == tree.end()) {
              added_list.push_back(std::make_pair(time, ret_value));
            }

            tree[ret_value] = tid;

            if (time_dict.find(ret_value) == time_dict.end()) {
              time_dict[ret_value] = time;
            }

            if (name_dict.find(ret_value) == name_dict.end()) {
              name_dict[ret_value] = comm_name;
            }
          } else if (syscall_type == "execve") {
            time_dict[tid] = time;
            name_dict[tid] = comm_name;
          } else if (syscall_type == "exit") {
            exit_time_dict[tid] = time;
            combo_dict[tid] = pid + "/" + tid;
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
            reg.timestamp = timestamp - period;
            reg.period = period;
            res.offcpu_regions.push_back(reg);
          }

          recurse(res.output, callchain, 0, period, false,
                  event_type == "offcpu-time");
          recurse(res.output_time_ordered, callchain, 0, period,
                  true, event_type == "offcpu-time");

          res.total_period += period;
        }
      }

      connection->close();

      std::sort(added_list.begin(), added_list.end(),
                [] (auto &a, auto &b) { return a.first < b.first; });

      for (auto &msg : messages_received) {
        std::string msg_key;
        if (msg == "<SAMPLE>" && extra_event_name != "") {
          msg_key = "<SAMPLE> " + extra_event_name;
        } else {
          msg_key = msg;
        }

        if (msg == "<SYSCALL>") {
          this->json_result[msg_key] = tid_dict;
        } else if (msg == "<SYSCALL_TREE>") {
          unsigned long long start_time = 0;
          this->json_result[msg_key] = nlohmann::json::array();
          this->json_result[msg_key].push_back(start_time);
          this->json_result[msg_key].push_back(nlohmann::json::array());

          nlohmann::json &result_list = this->json_result[msg_key][1];
          std::unordered_set<std::string> added_identifiers;
          bool profile_start = false;

          for (int i = 0; i < added_list.size(); i++) {
            std::string k = added_list[i].second;
            std::string p = tree[k];

            if (!profile_start) {
              if (name_dict[k] == this->profiled_filename.substr(0, 15)) {
                profile_start = true;
                start_time = time_dict[k];
                this->json_result[msg_key][0] = start_time;
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

              nlohmann::json &pid_tid_result = this->json_result[msg_key][elem.first + "_" + elem2.first];
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
    } catch (...) {
      std::rethrow_exception(std::current_exception());
    }
  }

  nlohmann::json & StdSubclient::get_result() {
    return this->json_result;
  }
};
