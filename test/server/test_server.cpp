// AdaptivePerf: comprehensive profiling tool based on Linux perf
// Copyright (C) CERN. See LICENSE for details.

#include "mocks.hpp"
#include <thread>
#include <chrono>
#include <future>
#include <mutex>
#include <gtest/gtest.h>

#ifndef SERVER_TEST_TIMEOUT
#define SERVER_TEST_TIMEOUT 60s
#endif

// Repeating each test multiple times increases the chance of
// detecting race-condition-related bugs if all other
// analyses failed to spot them beforehand
#ifndef SERVER_TEST_REPEAT
#define SERVER_TEST_REPEAT 100000
#endif

namespace test {
  std::function<void(MockClient &)> mock_client_init = [](MockClient &c) { };
};

namespace aperf {
  Client::Client(std::unique_ptr<Socket> &socket,
                 unsigned long long file_timeout_speed) {
    this->mock_client_ptr = test::make_mock_client(test::mock_client_init,
                                                   socket, file_timeout_speed,
                                                   true);
  }

  void Client::process() {
    ((test::MockClient *)this->mock_client_ptr.get())->process();
  }

  void Client::notify() {
    ((test::MockClient *)this->mock_client_ptr.get())->notify();
  }
};

using namespace testing;
using namespace std::chrono_literals;

TEST(ServerTest, ZeroMaxConnections) {
  for (int i = 0; i < SERVER_TEST_REPEAT; i++) {
    std::string address = "test_address";
    unsigned short port = 59263;
    unsigned int buf_size = 21348;
    unsigned long long file_timeout_speed = 687211;
    volatile bool interrupted = false;

    int created_sockets = 0;
    int created_clients = 0;
    aperf::Socket *last_socket = nullptr;

    std::unique_ptr<aperf::Acceptor> acceptor =
      test::make_mock_acceptor([=](test::MockAcceptor &acceptor) {
        EXPECT_CALL(acceptor, real_accept(buf_size)).Times(1);
        EXPECT_CALL(acceptor, close).Times(1);
      }, [&](test::MockSocket &socket) {
        created_sockets++;
        last_socket = &socket;
        EXPECT_CALL(socket, write("try_again", true)).Times(0);
        EXPECT_CALL(socket, close).Times(1);
      }, address, port, false, false);

    test::mock_client_init = [&](test::MockClient &client) {
      created_clients++;
      EXPECT_CALL(client, construct(last_socket, file_timeout_speed)).Times(1);
      client.set_interrupt_ptr(&interrupted);
      EXPECT_CALL(client, real_process).Times(1).WillOnce([&]() {
        interrupted = true;
      });
    };

    // A separate scope is needed for ensuring the correct order
    // of destructor calls (gmock will seg fault otherwise).
    {
      aperf::Server server(acceptor, 0, buf_size, file_timeout_speed);
      std::future<void> async_future = std::async(&aperf::Server::run, &server);
      EXPECT_EQ(async_future.wait_for(SERVER_TEST_TIMEOUT), std::future_status::ready);
    }

    EXPECT_EQ(created_clients, 1);
    EXPECT_EQ(created_sockets, 1);
  }
}

TEST(ServerTest, TwoMaxConnections) {
  for (int i = 0; i < SERVER_TEST_REPEAT; i++) {
    std::string address = "test_address2";
    unsigned short port = 1;
    unsigned int buf_size = 471;
    unsigned long long file_timeout_speed = 5758;
    volatile bool interrupted = false;

    int try_again_cnt = 0;
    int created_sockets = 0;
    int created_clients = 0;
    aperf::Socket *last_socket = nullptr;

    std::unique_ptr<aperf::Acceptor> acceptor =
      test::make_mock_acceptor([&](test::MockAcceptor &acceptor) {
        EXPECT_CALL(acceptor, real_accept(buf_size)).Times(AtLeast(5));
        EXPECT_CALL(acceptor, close).Times(1);
      }, [&](test::MockSocket &socket) {
        created_sockets++;
        last_socket = &socket;
        EXPECT_CALL(socket, write("try_again", true)).Times(AtLeast(0))
          .WillRepeatedly(InvokeWithoutArgs([&]() {
            interrupted = true;
            try_again_cnt++;
          }));
        EXPECT_CALL(socket, close).Times(1);
      }, address, port, false, false);

    // A separate scope is needed for ensuring the correct order
    // of destructor calls (gmock will seg fault otherwise).
    {
      aperf::Server server(acceptor, 2, buf_size, file_timeout_speed);

      test::mock_client_init = [&](test::MockClient &client) {
        created_clients++;

        EXPECT_CALL(client, construct(last_socket, file_timeout_speed)).Times(1);
        client.set_interrupt_ptr(&interrupted);
        EXPECT_CALL(client, real_process).Times(1);

        if (created_clients == 4) {
          server.interrupt();
          interrupted = true;
        } else {
          interrupted = false;
        }
      };

      std::future<void> async_future = std::async(&aperf::Server::run, &server);
      EXPECT_EQ(async_future.wait_for(SERVER_TEST_TIMEOUT), std::future_status::ready);
    }

    EXPECT_EQ(created_clients, 4);
    EXPECT_GT(created_sockets, 4);
    EXPECT_EQ(try_again_cnt, created_sockets - created_clients);
  }
}
