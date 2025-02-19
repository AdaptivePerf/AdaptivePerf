// AdaptivePerf: comprehensive profiling tool based on Linux perf
// Copyright (C) CERN. See LICENSE for details.

#include "server.hpp"
#include <iostream>
#include <unordered_set>
#include <unordered_map>

namespace aperf {
  void StdSubclient::recurse(nlohmann::json &cur_elem,
                             std::vector<std::pair<std::string, std::string> > &callchain_parts,
                             int callchain_index,
                             unsigned long long period,
                             bool time_ordered, bool offcpu) {
    std::pair<std::string, std::string> p = callchain_parts[callchain_index];
    nlohmann::json &arr = cur_elem["children"];
    nlohmann::json *elem;

    bool last_block = callchain_index == callchain_parts.size() - 1;

    if (!offcpu) {
      cur_elem["cold"] = false;
    }

    if (time_ordered) {
      if (arr.empty() || arr.back()["name"] != p.first ||
          (last_block &&
           arr.back()["cold"] != offcpu) ||
          (last_block &&
           !arr.back()["children"].empty()) ||
          (!last_block &&
           arr.back()["children"].empty())) {
        nlohmann::json new_elem;
        new_elem["name"] = p.first;
        new_elem["offsets"] = nlohmann::json::object();
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
        if (arr[i]["name"] == p.first &&
            (!last_block || arr[i]["cold"] == offcpu)) {
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
        new_elem["name"] = p.first;
        new_elem["offsets"] = nlohmann::json::object();
        new_elem["value"] = 0;
        new_elem["children"] = nlohmann::json::array();
        new_elem["cold"] = offcpu;

        arr.push_back(new_elem);
        elem = &arr.back();
      }
    }

    (*elem)["value"] = (unsigned long long)(*elem)["value"] + period;

    unsigned long long old_value = 0;

    if ((*elem)["offsets"].contains(p.second)) {
      old_value = (unsigned long long)(*elem)["offsets"][p.second];
    }

    (*elem)["offsets"][p.second] = old_value + period;

    if (!last_block) {
      recurse(*elem, callchain_parts, callchain_index + 1, period,
              time_ordered, offcpu);
    }
  }

  StdSubclient::StdSubclient(Client &context,
                             std::unique_ptr<Acceptor> &acceptor,
                             std::string profiled_filename,
                             unsigned int buf_size) : InitSubclient(context,
                                                                    acceptor,
                                                                    profiled_filename,
                                                                    buf_size) {
    this->json_result = nlohmann::json::object();
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
      std::unordered_set<std::string> messages_received;
      std::unordered_map<std::string, std::vector<std::pair<std::string, std::string> > > tid_dict;
      std::unordered_map<
        std::string,
        std::unordered_map<
          std::string,
          struct sample_result > > subprocesses;
      std::unordered_map<std::string, std::string> combo_dict;
      std::unordered_map<std::string, unsigned long long> exit_time_dict;
      std::unordered_map<std::string, std::vector<std::pair<std::string, unsigned long long> > > name_time_dict;
      std::unordered_map<std::string, std::string> tree;

      std::string extra_event_name = "";
      bool first_event_received = false;
      std::vector<std::pair<unsigned long long, std::string> > added_list;

      unsigned long long start_time = 0;
      bool start_time_set = false;

      {
        std::shared_ptr<Connection> connection = this->acceptor->accept(this->buf_size);
        this->context.notify();

        while (true) {
          std::string line = connection->read();

          if (line == "<STOP>") {
            break;
          }

          start_time_set = this->context.get_profile_start_tstamp(&start_time);

          nlohmann::json obj;

          try {
            obj = nlohmann::json::parse(line);
          } catch (...) {
            std::cerr << "Could not parse the recently-received line to JSON, ignoring." << std::endl;
            continue;
          }

          if (!obj.is_object() || !obj.contains("type")) {
            std::cerr << "The recently-received JSON is not an object of type "
                         "{\"type\": ..., ...}, ignoring."
                      << std::endl;
            continue;
          }

          std::string type = obj["type"].template get<std::string>();
          messages_received.insert(type);

          if (type == "syscall") {
            std::string ret_value;
            std::vector<std::pair<std::string, std::string> > callchain;

            try {
              ret_value = obj["ret_value"];
              callchain = obj["callchain"].template get<
                std::vector<std::pair<std::string, std::string> > >();
            } catch (...) {
              std::cerr << "The recently-received syscall JSON is invalid, ignoring." << std::endl;
              continue;
            }

            tid_dict[ret_value] = callchain;
          } else if (type == "syscall_meta") {
            std::string syscall_type, comm_name, pid, tid, ret_value;
            unsigned long long time;

            try {
              syscall_type = obj["subtype"];
              comm_name = obj["comm"];
              pid = obj["pid"];
              tid = obj["tid"];
              time = obj["time"];
              ret_value = obj["ret_value"];
            } catch (...) {
              std::cerr << "The recently-received syscall tree JSON is invalid, ignoring." << std::endl;
              continue;
            }

            std::string pid_tid = pid + "/" + tid;
            bool added_to_name_time_dict = false;

            if (tree.find(tid) == tree.end()) {
              tree[tid] = "";
              added_list.push_back(std::make_pair(time, tid));

              name_time_dict[tid].push_back(std::make_pair(comm_name, time));
              added_to_name_time_dict = true;
            }

            combo_dict[tid] = pid + "/" + tid;

            if (syscall_type == "new_proc") {
              if (tree.find(ret_value) == tree.end()) {
                added_list.push_back(std::make_pair(time, ret_value));
              }

              tree[ret_value] = tid;
              combo_dict[ret_value] = "?/" + ret_value;
              name_time_dict[ret_value].push_back(std::make_pair(comm_name, time));
            } else if (syscall_type == "execve" && !added_to_name_time_dict) {
              name_time_dict[tid].push_back(std::make_pair(comm_name, time));
            } else if (syscall_type == "exit") {
              exit_time_dict[tid] = time;
            }
          } else if (type == "sample" && start_time_set) {
            std::string event_type, pid, tid;
            unsigned long long timestamp, period;
            std::vector<std::pair<std::string, std::string> > callchain;
            try {
              event_type = obj["event_type"];
              pid = obj["pid"];
              tid = obj["tid"];
              timestamp = obj["time"];
              period = obj["period"];
              callchain = obj["callchain"].template get<
                std::vector<std::pair<std::string, std::string> > >();
            } catch (...) {
              std::cerr << "The recently received sample JSON is invalid, ignoring." << std::endl;
              continue;
            }

            if (!first_event_received) {
              first_event_received = true;

              if (event_type == "offcpu-time" || event_type == "task-clock") {
                extra_event_name = "";

                if (timestamp - period < start_time) {
                  period = timestamp - start_time;
                }
              } else {
                extra_event_name = event_type;
              }
            } else if ((extra_event_name != "" && event_type != extra_event_name) ||
                       (extra_event_name == "" && event_type != "offcpu-time" && event_type != "task-clock")) {
              std::cerr << "The recently received sample JSON is of different event type than expected ";
              std::cerr << "(received: " << event_type << ", expected: ";
              std::cerr << (extra_event_name == "" ? "task-clock or offcpu-time" : extra_event_name);
              std::cerr << "), ignoring." << std::endl;
              continue;
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
              // Will switch to false as soon as on-CPU activity is encountered
              res.output["cold"] = true;

              res.output_time_ordered["name"] = "all";
              res.output_time_ordered["value"] = 0;
              res.output_time_ordered["children"] = nlohmann::json::array();
              // Will switch to false as soon as on-CPU activity is encountered
              res.output_time_ordered["cold"] = true;
            }

            struct sample_result &res = subprocesses[pid][tid];

            if (callchain.empty()) {
              callchain.push_back(std::make_pair("(just thread/process)", ""));
            }

            if (event_type == "offcpu-time") {
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
      }

      if (!start_time_set) {
        return;
      }

      std::sort(added_list.begin(), added_list.end(),
                [] (auto &a, auto &b) { return a.first < b.first; });

      for (auto &msg : messages_received) {
        std::string msg_key;
        if (msg == "sample" && extra_event_name != "") {
          msg_key = "sample " + extra_event_name;
        } else {
          msg_key = msg;
        }

        if (msg == "syscall") {
          this->json_result[msg_key] = tid_dict;
        } else if (msg == "syscall_meta") {
          this->json_result[msg_key] = nlohmann::json::array();
          this->json_result[msg_key].push_back(nlohmann::json::array());
          this->json_result[msg_key].push_back(nlohmann::json::object());

          nlohmann::json &result_list = this->json_result[msg_key][0];
          nlohmann::json &result_map = this->json_result[msg_key][1];
          std::unordered_set<std::string> added_identifiers;

          for (int i = 0; i < added_list.size(); i++) {
            std::string k = added_list[i].second;
            std::string p = tree[k];

            if (!p.empty() && added_identifiers.find(p) == added_identifiers.end()) {
              continue;
            }

            added_identifiers.insert(k);

            nlohmann::json elem;
            elem["tag"] = nlohmann::json::array();

            int dominant_name_index = 0;
            int dominant_name_time = 0;
            for (int i = 1; i < name_time_dict[k].size(); i++) {
              if (name_time_dict[k][i].second - name_time_dict[k][i - 1].second > dominant_name_time) {
                dominant_name_index = i - 1;
                dominant_name_time = name_time_dict[k][i].second - name_time_dict[k][i - 1].second;
              }
            }

            if (exit_time_dict.find(k) == exit_time_dict.end() ||
                exit_time_dict[k] - name_time_dict[k][name_time_dict[k].size() - 1].second > dominant_name_time) {
              dominant_name_index = name_time_dict[k].size() - 1;
            }

            elem["tag"][0] = name_time_dict[k][dominant_name_index].first;
            elem["tag"][1] = combo_dict[k];
            elem["tag"][2] = name_time_dict[k][0].second;

            if (exit_time_dict.find(k) != exit_time_dict.end()) {
              elem["tag"][3] = exit_time_dict[k] - name_time_dict[k][0].second;
            } else {
              elem["tag"][3] = -1;
            }

            if (p.empty()) {
              elem["parent"] = nullptr;
            } else {
              elem["parent"] = p;
            }

            result_list.push_back(k);
            result_map[k] = elem;
          }

          for (auto &pair : result_map.items()) {
            auto &elem = pair.value();
            if (start_time >= elem["tag"][2]) {
              elem["tag"][3] = (unsigned long long)elem["tag"][3] - (start_time - (unsigned long long)elem["tag"][2]);
              elem["tag"][2] = 0;
            } else {
              elem["tag"][2] = (unsigned long long)elem["tag"][2] - start_time;
            }
          }
        } else if (msg == "sample") {
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
