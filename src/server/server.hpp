#ifndef SERVER_HPP_
#define SERVER_HPP_

#include "socket.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <future>
#include <condition_variable>
#include <mutex>
#include <vector>

namespace aperf {
  class Client {
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
    void notify_accepted();
  };

  class Subclient {
  private:
    Client &context;
    Acceptor init_socket;
    nlohmann::json json_result;
    unsigned int buf_size;
    std::string profiled_filename;

    void recurse(nlohmann::json &cur_elem,
                 std::vector<std::string> &callchain_parts,
                 int callchain_index,
                 unsigned long long period,
                 bool time_ordered, bool offcpu);

  public:
    Subclient(Client &context,
              std::string address,
              unsigned short port,
              std::string profiled_filename,
              unsigned int buf_size);
    void process();
    nlohmann::json & get_result();
    unsigned short get_port();
  };

  class Server {
  private:
    Acceptor acceptor;
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
