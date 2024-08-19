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

  StdClient::StdClient(std::shared_ptr<Subclient::Factory> &subclient_factory,
                       std::unique_ptr<Connection> &connection,
                       std::unique_ptr<Acceptor> &file_acceptor,
                       unsigned long long file_timeout_seconds) : InitClient(subclient_factory,
                                                                             connection,
                                                                             file_acceptor,
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
      metadata["callchains"] = nlohmann::json::object();
      metadata["offcpu_regions"] = nlohmann::json::object();
      metadata["sampled_times"] = nlohmann::json::object();
      metadata["external_metrics"] = nlohmann::json::object();
      std::string external_metrics = "MetricName,Timestamp,Value\n";

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
          } else if (elem.key() == "<EXTERNAL_METRICS>") {
            for (auto &elem2 : elem.value().items()) {
              metadata["external_metrics"][elem2.key()].swap(elem2.value());
            }
          } else if (elem.key() == "<EXTERNAL_METRICS_DATA>") {
            for (int m = 0; m < elem.value()[0].size(); m++) {
              external_metrics += elem.value()[0][m].get<std::string>() + "," +
                std::to_string(static_cast<long>(elem.value()[1][m])) + "," +
                std::to_string(static_cast<float>(elem.value()[2][m])) + "\n";
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
                  } else {
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

      auto save_string = [](std::string path, const std::string &output) {
        std::ofstream f;
        f.open(path);
        f << output << std::endl;
        f.close();
      };

      std::shared_future<void> futures[final_output.size() + 2];

      futures[0] = std::async(save, processed_path / "metadata.json",
                              &metadata);
      futures[1] = std::async(save_string, processed_path / "external_metric_data.csv",
                              external_metrics);

      int future_index = 2;

      for (auto &elem : final_output.items()) {
        futures[future_index++] = std::async(save,
                                             processed_path / (elem.key() + ".json"),
                                             &elem.value());
      }

      for (int i = 0; i < final_output.size() + 2; i++) {
        futures[i].get();
      }

      if (this->file_acceptor == nullptr) {
        this->connection->write("profiling_finished", true);
      } else {
        this->connection->write("out_files", true);
        this->connection->write(this->file_acceptor->get_type() + " " +
                                this->file_acceptor->get_connection_instructions(),
                                true);

        std::vector<std::pair<std::string, unsigned long long> > out_files;
        std::vector<std::pair<std::string, unsigned long long> > processed_files;

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

          // buf_size = 1 because it is only for string read which is unused here
          std::unique_ptr<Connection> file_connection =
            this->file_acceptor->accept(1);

          try {
            std::ofstream f(path, std::ios_base::out | std::ios_base::binary);

            if (!f) {
              std::cerr << "Error for " << type << " file " << path.filename() << ": ";
              std::cerr << "Could not open the output stream." << std::endl;
              this->connection->write("error_out_file", true);
              continue;
            }

            std::unique_ptr<char> buf(new char[FILE_BUFFER_SIZE]);

            int bytes_received = 0;
            bool stop = false;

            while (!stop && !error) {
              bytes_received = file_connection->read(buf.get(),
                                                     FILE_BUFFER_SIZE,
                                                     this->file_timeout_seconds);

              if (bytes_received == 0) {
                stop = true;
              } else {
                f.write(buf.get(), bytes_received);

                if (!f) {
                  std::cerr << "Error for " << type << " file " << path.filename() << ": ";
                  std::cerr << "Could not write to the output stream." << std::endl;
                  error = true;
                }
              }
            }

            f.close();

            if (error) {
              this->connection->write("error_out_file", true);
            } else {
              this->connection->write("out_file_ok", true);
            }
          } catch (TimeoutException &e) {
            std::cerr << "Warning for " << type << " file " << path.filename() << ": ";
            std::cerr << "Timeout of " << this->file_timeout_seconds << " s has been reached, ";
            std::cerr << "some data may have been lost." << std::endl;

            this->connection->write("error_out_file_timeout", true);
          }
        }
      }

      this->connection->write("finished", true);
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
