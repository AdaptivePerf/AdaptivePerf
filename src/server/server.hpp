// AdaptivePerf: comprehensive profiling tool based on Linux perf
// Copyright (C) CERN. See LICENSE for details.

#ifndef SERVER_HPP_
#define SERVER_HPP_

#include "socket.hpp"
#include <nlohmann/json.hpp>
#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>

namespace aperf {
  class Notifiable {
  public:
    virtual void notify() = 0;
  };

  class Subclient {
  private:
    Notifiable &context;
    std::unique_ptr<Acceptor> acceptor;
    nlohmann::json json_result;
    unsigned int buf_size;
    std::string profiled_filename;

    void recurse(nlohmann::json &cur_elem,
                 std::vector<std::string> &callchain_parts,
                 int callchain_index,
                 unsigned long long period,
                 bool time_ordered, bool offcpu);

  public:
    Subclient(Notifiable &context,
              std::unique_ptr<Acceptor> &acceptor,
              std::string profiled_filename,
              unsigned int buf_size);
    void process();
    nlohmann::json &get_result();
    unsigned short get_port();
  };

  class Client : public Notifiable {
  private:
    std::unique_ptr<Socket> socket;
    unsigned int accepted;
    std::mutex accepted_mutex;
    std::condition_variable accepted_cond;
    unsigned long long file_timeout_speed;
    std::shared_ptr<void> mock_client_ptr = nullptr; // Only for testing

  public:
    Client(std::unique_ptr<Socket> &socket,
           unsigned long long file_timeout_speed);
    void process();
    void notify();
  };

  class Server {
  private:
    std::unique_ptr<Acceptor> acceptor;
    unsigned int max_connections;
    unsigned int buf_size;
    unsigned long long file_timeout_speed;
    bool interrupted;

  public:
    Server(std::unique_ptr<Acceptor> &acceptor,
           unsigned int max_connections,
           unsigned int buf_size,
           unsigned long long file_timeout_speed);
    void run();
    void interrupt();
  };
};

#endif
