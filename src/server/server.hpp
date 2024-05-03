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
    TCPAcceptor acceptor;
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
              std::string address,
              unsigned short port,
              std::string profiled_filename,
              unsigned int buf_size);
    void process();
    nlohmann::json &get_result();
    unsigned short get_port();
  };

  class Client : public Notifiable {
  private:
    std::shared_ptr<Socket> socket;
    unsigned int accepted;
    std::mutex accepted_mutex;
    std::condition_variable accepted_cond;
    unsigned long long file_timeout_speed;

  public:
    Client(std::shared_ptr<Socket> socket,
           unsigned long long file_timeout_speed);
    void process();
    void notify();
  };

  class Server {
  private:
    TCPAcceptor acceptor;
    unsigned int max_connections;
    unsigned int buf_size;
    unsigned long long file_timeout_speed;

  public:
    Server(std::string address,
           unsigned short port,
           unsigned int max_connections,
           unsigned int buf_size,
           unsigned long long file_timeout_speed);
    void run();
  };
};

#endif
