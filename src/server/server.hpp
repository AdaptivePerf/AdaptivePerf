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
  public:
    class Factory {
    public:
      virtual std::unique_ptr<Subclient> make_subclient(Notifiable &context,
                                                        std::string profiled_filename,
                                                        unsigned int buf_size) = 0;
      virtual std::string get_type() = 0;
    };

    virtual ~Subclient() { }
    virtual void process() = 0;
    virtual nlohmann::json &get_result() = 0;
    virtual std::string get_connection_instructions() = 0;
  };

  class InitSubclient : public Subclient {
  protected:
    Notifiable &context;
    std::unique_ptr<Acceptor> acceptor;
    std::string profiled_filename;
    unsigned int buf_size;

    InitSubclient(Notifiable &context,
                  std::unique_ptr<Acceptor> &acceptor,
                  std::string profiled_filename,
                  unsigned int buf_size) : context(context) {
      this->acceptor = std::move(acceptor);
      this->profiled_filename = profiled_filename;
      this->buf_size = buf_size;
    }

  public:
    virtual void process() = 0;
    virtual nlohmann::json &get_result() = 0;
    std::string get_connection_instructions() {
      return this->acceptor->get_connection_instructions();
    }
  };

  class Client : public Notifiable {
  public:
    class Factory {
    public:
      virtual std::unique_ptr<Client> make_client(std::unique_ptr<Connection> &connection,
                                                  unsigned long long file_timeout_seconds) = 0;
    };

    virtual ~Client() { }
    virtual void process(fs::path working_dir = fs::current_path()) = 0;
    virtual void notify() = 0;
  };

  class InitClient : public Client {
  protected:
    std::shared_ptr<Subclient::Factory> subclient_factory;
    std::unique_ptr<Connection> connection;
    unsigned long long file_timeout_seconds;

    InitClient(std::shared_ptr<Subclient::Factory> &subclient_factory,
               std::unique_ptr<Connection> &connection,
               unsigned long long file_timeout_seconds) {
      this->subclient_factory = subclient_factory;
      this->connection = std::move(connection);
      this->file_timeout_seconds = file_timeout_seconds;
    }

  public:
    virtual void process(fs::path working_dir) = 0;
    virtual void notify() = 0;
  };

  class StdSubclient : public InitSubclient {
  private:
    nlohmann::json json_result;

    StdSubclient(Notifiable &context,
                 std::unique_ptr<Acceptor> &acceptor,
                 std::string profiled_filename,
                 unsigned int buf_size);
    void recurse(nlohmann::json &cur_elem,
                 std::vector<std::string> &callchain_parts,
                 int callchain_index,
                 unsigned long long period,
                 bool time_ordered, bool offcpu);

  public:
    class Factory : public Subclient::Factory {
    private:
      std::shared_ptr<Acceptor::Factory> factory;

    public:
      Factory(std::unique_ptr<Acceptor::Factory> &factory) {
        this->factory = std::move(factory);
      }

      std::unique_ptr<Subclient> make_subclient(Notifiable &context,
                                                std::string profiled_filename,
                                                unsigned int buf_size) {
        std::unique_ptr<Acceptor> acceptor = this->factory->make_acceptor(1);
        return std::unique_ptr<
          StdSubclient>(new StdSubclient(context, acceptor,
                                         profiled_filename, buf_size));
      }

      std::string get_type() {
        return this->factory->get_type();
      }
    };

    void process();
    nlohmann::json &get_result();
  };

  class StdClient : public InitClient {
  private:
    unsigned int accepted;
    std::mutex accepted_mutex;
    std::condition_variable accepted_cond;

    StdClient(std::shared_ptr<Subclient::Factory> &subclient_factory,
              std::unique_ptr<Connection> &connection,
              unsigned long long file_timeout_seconds);

  public:
    class Factory : public Client::Factory {
    private:
      std::shared_ptr<Subclient::Factory> factory;

    public:
      Factory(std::unique_ptr<Subclient::Factory> &factory) {
        this->factory = std::move(factory);
      }

      std::unique_ptr<Client> make_client(std::unique_ptr<Connection> &connection,
                                          unsigned long long file_timeout_seconds) {
        return std::unique_ptr<
          StdClient>(new StdClient(this->factory,
                                   connection,
                                   file_timeout_seconds));
      }
    };

    void process(fs::path working_dir);
    void notify();
  };

  class Server {
  private:
    std::unique_ptr<Acceptor> acceptor;
    unsigned int max_connections;
    unsigned int buf_size;
    unsigned long long file_timeout_seconds;
    bool interrupted;

  public:
    Server(std::unique_ptr<Acceptor> &acceptor,
           unsigned int max_connections,
           unsigned int buf_size,
           unsigned long long file_timeout_seconds);
    void run(std::unique_ptr<Client::Factory> &client_factory);
    void interrupt();
  };
};

#endif
