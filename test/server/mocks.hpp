// AdaptivePerf: comprehensive profiling tool based on Linux perf
// Copyright (C) CERN. See LICENSE for details.

#ifndef MOCKS_HPP_
#define MOCKS_HPP_

#include "server.hpp"
#include <gmock/gmock.h>

using testing::StrictMock;

namespace test {
  class MockSubclient : public aperf::Subclient {
  private:
    aperf::Notifiable &context;

  protected:
    MockSubclient(aperf::Notifiable &context) : context(context) {}

  public:
    class Factory : public aperf::Subclient::Factory {
    private:
      std::function<void(MockSubclient &)> init;
      bool call_constructor;

    public:
      Factory(std::function<void(MockSubclient &)> init,
              bool call_constructor) {
        this->init = init;
        this->call_constructor = call_constructor;
      }

      std::unique_ptr<Subclient> make_subclient(aperf::Notifiable &context,
                                                std::string profiled_filename,
                                                unsigned int buf_size) {
        std::unique_ptr<StrictMock<MockSubclient> > subclient(new StrictMock<MockSubclient>(context));
        this->init(*subclient);

        if (call_constructor) {
          subclient->construct(context, profiled_filename, buf_size);
        }

        return subclient;
      }
    };

    MOCK_METHOD(void, construct, (aperf::Notifiable &,
                                  std::string,
                                  unsigned int));
    MOCK_METHOD(void, process, (), (override));
    MOCK_METHOD(nlohmann::json &, get_result, (), (override));
    MOCK_METHOD(std::string, get_connection_instructions, (), (override));
  };

  class MockClient : public aperf::Client {
  private:
    volatile bool *interrupted = nullptr;

  protected:
    MockClient() { }

  public:
    class Factory : public aperf::Client::Factory {
    private:
      std::function<void(MockClient &)> init;
      bool call_constructor;

    public:
      Factory(std::function<void(MockClient &)> init,
              bool call_constructor) {
        this->init = init;
        this->call_constructor = call_constructor;
      }

      std::unique_ptr<Client> make_client(std::unique_ptr<aperf::Connection> &connection,
                                          unsigned long long file_timeout_speed) {
        std::unique_ptr<StrictMock<MockClient> > client(new StrictMock<MockClient>());
        this->init(*client);

        if (call_constructor) {
          client->construct(connection.get(),
                            file_timeout_speed);
        }

        return client;
      }
    };

    MOCK_METHOD(void, construct, (aperf::Connection *,
                                  unsigned long long));
    MOCK_METHOD(void, real_process, ());
    MOCK_METHOD(void, notify, (), (override));

    void set_interrupt_ptr(volatile bool *interrupted) {
      this->interrupted = interrupted;
    }
    void process() {
      this->real_process();

      if (this->interrupted) {
        while (!(*this->interrupted)) { }
      }
    }
  };

  class MockConnection : public aperf::Connection {
  public:
    ~MockConnection() { this->close(); }

    MOCK_METHOD(unsigned int, get_buf_size, (), (override));
    MOCK_METHOD(void, close, (), (override));
    MOCK_METHOD(int, read, (char *, unsigned int, long), (override));
    MOCK_METHOD(std::string, read, (), (override));
    MOCK_METHOD(void, write, (std::string, bool), (override));
  };

  class MockAcceptor : public aperf::Acceptor {
  private:
    std::function<void(MockConnection &)> connection_init;

  protected:
    MockAcceptor(std::function<void(MockConnection &)> connection_init,
                 int max_accepted) : Acceptor(max_accepted) {
      this->connection_init = connection_init;
    }

    std::unique_ptr<aperf::Connection> accept_connection(unsigned int buf_size) {
      this->real_accept(buf_size);

      std::unique_ptr<MockConnection> connection = std::make_unique<MockConnection>();
      this->connection_init(*connection);
      return connection;
    }

  public:
    class Factory : public aperf::Acceptor::Factory {
    private:
      std::function<void(MockAcceptor &)> acceptor_init;
      std::function<void(MockConnection &)> connection_init;
      bool call_constructor;

    public:
      Factory(std::function<void(MockAcceptor &)> acceptor_init,
              std::function<void(MockConnection &)> connection_init,
              bool call_constructor) {
        this->acceptor_init = acceptor_init;
        this->connection_init = connection_init;
        this->call_constructor = call_constructor;
      }

      std::unique_ptr<Acceptor> make_acceptor(int max_accepted) {
        std::unique_ptr<StrictMock<MockAcceptor> > acceptor(new StrictMock<MockAcceptor>(connection_init,
                                                                                         max_accepted));
        this->acceptor_init(*acceptor);

        if (this->call_constructor) {
          acceptor->construct(max_accepted);
        }

        return acceptor;
      }
    };

    ~MockAcceptor() { this->close(); }

    MOCK_METHOD(void, construct, (int));
    MOCK_METHOD(std::unique_ptr<aperf::Connection>, real_accept, (unsigned int));
    MOCK_METHOD(std::string, get_connection_instructions, (), (override));
    MOCK_METHOD(void, close, (), (override));
  };
};

#endif
