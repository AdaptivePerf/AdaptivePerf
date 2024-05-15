// AdaptivePerf: comprehensive profiling tool based on Linux perf
// Copyright (C) CERN. See LICENSE for details.

#ifndef MOCKS_HPP_
#define MOCKS_HPP_

#include "server.hpp"
#include <gmock/gmock.h>

namespace test {

  class MockClient : public aperf::Notifiable {
  private:
    volatile bool *interrupted = nullptr;

  public:
    MOCK_METHOD(void, construct, (aperf::Socket *,
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

  class MockSocket : public aperf::Socket {
  public:
    ~MockSocket() { this->close(); }

    MOCK_METHOD(std::string, get_address, (), (override));
    MOCK_METHOD(unsigned short, get_port, (), (override));
    MOCK_METHOD(unsigned int, get_buf_size, (), (override));
    MOCK_METHOD(void, close, (), (override));
    MOCK_METHOD(int, read, (char *, unsigned int, long), (override));
    MOCK_METHOD(std::string, read, (), (override));
    MOCK_METHOD(void, write, (std::string, bool), (override));
  };

  std::unique_ptr<aperf::Socket> make_mock_socket(std::function<void(MockSocket &)> init) {
    std::unique_ptr<MockSocket> socket = std::make_unique<MockSocket>();
    init(*socket);
    return socket;
  }

  class MockAcceptor : public aperf::Acceptor {
  private:
    std::function<void(MockSocket &)> socket_init;
    std::string address;
    unsigned short port;

  public:
    MockAcceptor(std::string address, unsigned short port,
                 bool try_subsequent_ports,
                 std::function<void(MockSocket &)> socket_init) : Acceptor(address,
                                                                           port,
                                                                           try_subsequent_ports) {
      this->socket_init = socket_init;
      this->address = address;
      this->port = port;
    }
    ~MockAcceptor() { this->close(); }

    MOCK_METHOD(void, construct, (std::string, unsigned short, bool));
    MOCK_METHOD(std::unique_ptr<aperf::Socket>, real_accept, (unsigned int));
    MOCK_METHOD(unsigned short, get_port, (), (override));
    MOCK_METHOD(void, close, (), (override));

    std::unique_ptr<aperf::Socket> accept(unsigned int buf_size) {
      this->real_accept(buf_size);
      return make_mock_socket(this->socket_init);
    }
  };

  std::unique_ptr<MockClient> make_mock_client(std::function<void(MockClient &)> init,
                                               std::unique_ptr<aperf::Socket> &socket,
                                               unsigned long long file_timeout_speed,
                                               bool call_constructor) {
    std::unique_ptr<MockClient> client = std::make_unique<MockClient>();
    init(*client);

    if (call_constructor) {
      client->construct(socket.get(), file_timeout_speed);
    }

    return client;
  }

  std::unique_ptr<aperf::Acceptor> make_mock_acceptor(std::function<void(MockAcceptor &)> init,
                                                      std::function<void(MockSocket &)> socket_init,
                                                      std::string address,
                                                      unsigned short port,
                                                      bool try_subsequent_ports,
                                                      bool call_constructor) {
    std::unique_ptr<MockAcceptor> acceptor = std::make_unique<MockAcceptor>(address,
                                                                            port,
                                                                            try_subsequent_ports,
                                                                            socket_init);
    init(*acceptor);

    if (call_constructor) {
      acceptor->construct(address, port, try_subsequent_ports);
    }

    return acceptor;
  }
};

#endif
