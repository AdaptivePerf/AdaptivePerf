// AdaptivePerf: comprehensive profiling tool based on Linux perf
// Copyright (C) CERN. See LICENSE for details.

#include "server.hpp"
#include <future>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <cmath>
#include <unordered_set>

namespace aperf {
  namespace fs = std::filesystem;

  StdClient::StdClient(std::unique_ptr<Subclient::Factory> &subclient_factory,
                       std::unique_ptr<Connection> &connection,
                       unsigned long long file_timeout_seconds) : InitClient(subclient_factory,
                                                                             connection,
                                                                             file_timeout_seconds) {
    this->accepted = 0;
  }

  void StdClient::process(fs::path working_dir) {
    try {
      fs::path result_path, processed_path, out_path;

      std::string msg = this->connection->read();

      std::regex start_regex("^start([1-9]\\d*) (.+)$");
      std::smatch match;

      if (!std::regex_match(msg, match, start_regex)) {
        this->connection->write("error_wrong_command", true);
        return;
      }

      int subclient_cnt = std::stoi(match[1]);
      std::string result_dir = match[2];

      result_path = working_dir / result_dir;
      processed_path = result_path / "processed";
      out_path = result_path / "out";

      try {
        fs::create_directory(result_path);
        fs::create_directory(processed_path);
        fs::create_directory(out_path);
      } catch (std::exception &e) {
        std::cerr << "Could not create " << result_dir << "! Error details:";
        std::cerr << std::endl;
        std::cerr << e.what() << std::endl;
        this->connection->write("error_result_dir", true);
        return;
      }

      std::string profiled_filename = this->connection->read();
      std::unique_ptr<Subclient> subclients[subclient_cnt];
      std::shared_future<void> threads[subclient_cnt];

      for (int i = 0; i < subclient_cnt; i++) {
        subclients[i] = this->subclient_factory->make_subclient(*this, profiled_filename,
                                                                this->connection->get_buf_size());
        threads[i] = std::async(&Subclient::process, subclients[i].get());
      }

      std::string instr_msg = subclient_factory->get_type();

      for (int i = 0; i < subclient_cnt; i++) {
        instr_msg += " " + subclients[i]->get_connection_instructions();
      }

      this->connection->write(instr_msg, true);

      std::unique_lock lock(this->accepted_mutex);
      while (this->accepted < subclient_cnt) {
        this->accepted_cond.wait(lock);
      }

      this->connection->write("start_profile", true);

      nlohmann::json final_output;
      nlohmann::json metadata;

      std::unordered_set<std::string> tids;

      metadata["thread_tree"] = nlohmann::json::array();

      unsigned long long start_time = 0;

      for (int i = 0; i < subclient_cnt; i++) {
        threads[i].get();
        nlohmann::json &thread_result = subclients[i]->get_result();
        for (auto &elem : thread_result.items()) {
          if (elem.key() == "<SYSCALL_TREE>") {
            start_time = elem.value()[0];

            for (auto &tid : elem.value()[1]) {
              metadata["thread_tree"].push_back(nlohmann::json::object());
              nlohmann::json &new_object = metadata["thread_tree"].back();
              new_object.swap(elem.value()[2][tid]);
              new_object["identifier"] = tid;

              tids.insert(tid);
            }
          } else if (elem.key() == "<SYSCALL>") {
            for (auto &elem2 : elem.value().items()) {
              metadata["callchains"][elem2.key()].swap(elem2.value());
            }
          }
        }

        for (auto &elem : thread_result.items()) {
          if (elem.key().rfind("<SAMPLE>", 0) == 0) {
            for (auto &elem2 : elem.value().items()) {
              if (elem2.value()["first_time"] >= start_time) {
                std::regex pid_tid_regex("^(\\d+)_(\\d+)$");
                std::smatch pid_tid_match;

                if (!std::regex_search(elem2.key(), pid_tid_match, pid_tid_regex)) {
                  std::cerr << "Could not process PID/TID key " << elem2.key() << ", this should not happen!";
                  std::cerr << std::endl;
                  continue;
                }

                if (tids.find(pid_tid_match[2]) == tids.end()) {
                  nlohmann::json new_elem;
                  new_elem["identifier"] = pid_tid_match[2];
                  new_elem["parent"] = nullptr;
                  new_elem["tag"] = {
                    "?", std::string(pid_tid_match[1]) + "/" + std::string(pid_tid_match[2]), -1, -1};

                  metadata["thread_tree"].push_back(new_elem);
                }

                for (auto &elem3 : elem2.value().items()) {
                  if (elem3.key() == "sampled_time") {
                    metadata["sampled_times"][elem2.key()].swap(elem3.value());
                  } else if (elem3.key() == "offcpu_regions") {
                    metadata["offcpu_regions"][elem2.key()].swap(elem3.value());
                  } else if (elem3.key() != "first_time") {
                    final_output[elem2.key()][elem3.key()].swap(elem3.value());
                  }
                }
              }
            }
          }
        }
      }

      for (auto &regions : metadata["offcpu_regions"].items()) {
        for (int i = 0; i < regions.value().size(); i++) {
          regions.value()[i][0] = (unsigned long long)regions.value()[i][0] - start_time;
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

      this->connection->write("out_files", true);

      std::vector<std::pair<std::string, unsigned long long> > out_files;
      std::vector<std::pair<std::string, unsigned long long> > processed_files;

      bool error_in_any = false;

      while (true) {
        std::string x = this->connection->read();

        if (x == "<STOP>") {
          break;
        }

        if (x.length() < 3) {
          this->connection->write("error_wrong_file_format", true);
          continue;
        }

        bool processed;
        bool error = false;

        if (x[0] == 'p') {
          processed = true;
        } else if (x[0] == 'o') {
          processed = false;
        } else {
          this->connection->write("error_wrong_file_format", true);
          continue;
        }

        if (x[1] != ' ') {
          this->connection->write("error_wrong_file_format", true);
          continue;
        }

        std::string name = x.substr(2);
        fs::path path = (processed ? processed_path : out_path) / name;
        std::string type = processed ? "processed" : "out";

        try {
          std::ofstream f(path, std::ios_base::out | std::ios_base::binary);

          if (!f) {
            std::cerr << "Error for " << type << " file " << path << ": ";
            std::cerr << "Could not open the output stream." << std::endl;
            continue;
          }

          std::unique_ptr<char> buf(new char[1024 * this->connection->get_buf_size()]);

          int bytes_received = 0;
          bool stop = false;

          while (!stop && !error) {
            bytes_received = this->connection->read(buf.get(),
                                                    1024 * this->connection->get_buf_size(),
                                                    this->file_timeout_seconds);

            if (bytes_received == 0) {
              std::cerr << "Error for " << type << " file " << path << ": ";
              std::cerr << "Received 0 bytes without reaching end-of-text (0x03)." << std::endl;
              error = true;
            } else {
              if (buf.get()[bytes_received - 1] == 0x03) {
                f.write(buf.get(), bytes_received - 1);
                stop = true;
              } else {
                f.write(buf.get(), bytes_received);
              }

              if (!f) {
                std::cerr << "Error for " << type << " file " << path << ": ";
                std::cerr << "Could not write to the output stream." << std::endl;
                error = true;
              }
            }
          }

          f.close();

          if (error) {
            error_in_any = true;
            continue;
          }
        } catch (TimeoutException &e) {
          std::cerr << "Warning for " << type << " file " << path << ": ";
          std::cerr << "Timeout of " << this->file_timeout_seconds << " s has been reached, no data saved.";
          std::cerr << std::endl;
        }
      }

      if (error_in_any) {
        this->connection->write("out_file_error", true);
      } else {
        this->connection->write("finished", true);
      }
    } catch (...) {
      std::rethrow_exception(std::current_exception());
    }
  }

  void StdClient::notify() {
    {
      std::lock_guard lock(this->accepted_mutex);
      this->accepted++;
    }
    this->accepted_cond.notify_all();
  }
};
