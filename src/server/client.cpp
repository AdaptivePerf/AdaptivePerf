// AdaptivePerf: comprehensive profiling tool based on Linux perf
// Copyright (C) CERN. See LICENSE for details.

#include "server.hpp"
#include <future>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>

namespace aperf {
  namespace fs = std::filesystem;

  Client::Client(std::unique_ptr<Socket> &socket,
                 unsigned long long file_timeout_speed) {
    this->socket = std::move(socket);
    this->accepted = 0;
    this->file_timeout_speed = file_timeout_speed;
  }

  void Client::process() {
    try {
      std::string msg = this->socket->read();
      std::regex start_regex("^start(\\d+) (.+)$");
      std::smatch match;

      if (!std::regex_search(msg, match, start_regex)) {
        this->socket->write("error_wrong_command");
        this->socket->close();
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
        this->socket->write("error_result_dir");
        this->socket->close();
        return;
      }

      std::string profiled_filename = this->socket->read();
      std::unique_ptr<Subclient> subclients[ports];
      std::shared_future<void> threads[ports];

      unsigned short port = this->socket->get_port();

      for (int i = 0; i < ports; i++) {
        unsigned short p = port + i + 1;

        std::unique_ptr<Acceptor> acceptor = std::make_unique<TCPAcceptor>(this->socket->get_address(),
                                                                           p, true);

        subclients[i] = std::make_unique<Subclient>(*this, acceptor, profiled_filename,
                                                    this->socket->get_buf_size());
        threads[i] = std::async(&Subclient::process, subclients[i].get());
      }

      std::string port_msg = "";

      for (int i = 0; i < ports; i++) {
        port_msg += std::to_string(subclients[i]->get_port());

        if (i < ports - 1) {
          port_msg += " ";
        }
      }

      this->socket->write(port_msg);

      std::unique_lock lock(this->accepted_mutex);
      while (this->accepted < ports) {
        this->accepted_cond.wait(lock);
      }

      this->socket->write("start_profile");

      nlohmann::json final_output;
      nlohmann::json metadata;

      unsigned long long start_time = 0;

      for (int i = 0; i < ports; i++) {
        threads[i].get();
        nlohmann::json &thread_result = subclients[i]->get_result();
        for (auto &elem : thread_result.items()) {
          if (elem.key() == "<SYSCALL_TREE>") {
            start_time = elem.value()[0];
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

      this->socket->write("out_files");

      std::vector<std::pair<std::string, unsigned long long> > out_files;
      std::vector<std::pair<std::string, unsigned long long> > processed_files;

      while (true) {
        std::string x = this->socket->read();

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
              this->socket->write("error_wrong_file_format");
              error = true;
            } else {
              this->socket->write("ok");
            }

            break;
          }
        }

        if (index >= x.size() - 2) {
          this->socket->write("error_wrong_file_format");
          error = true;
        }

        bool processed;

        if (x[index] == 'p') {
          processed = true;
        } else if (x[index] == 'o') {
          processed = false;
        } else {
          this->socket->write("error_wrong_file_format");
          error = true;
        }

        if (x[index + 1] != ' ') {
          this->socket->write("error_wrong_file_format");
          error = true;
        }

        if (!error) {
          if (processed) {
            processed_files.push_back(std::make_pair(x.substr(index + 2),
                                                     std::stoll(len_str)));
          } else {
            out_files.push_back(std::make_pair(x.substr(index + 2),
                                               std::stoll(len_str)));
          }
        }
      }

      bool error = false;

      auto process_file = [&processed_path, &out_path, &error, this]
        (std::string &name,
         unsigned long long len,
         bool processed) {
        fs::path path = (processed ? processed_path : out_path) / name;
        unsigned long long file_timeout_seconds = len / this->file_timeout_speed;

        try {
          char buf[len];
          int bytes_received = this->socket->read(buf, len, file_timeout_seconds);

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
        this->socket->write("out_file_error");
      } else {
        this->socket->write("finished");
      }

      this->socket->close();
    } catch (...) {
      std::rethrow_exception(std::current_exception());
    }
  }

  void Client::notify() {
    {
      std::lock_guard lock(this->accepted_mutex);
      this->accepted++;
    }
    this->accepted_cond.notify_all();
  }
};
