// AdaptivePerf: comprehensive profiling tool based on Linux perf
// Copyright (C) CERN. See LICENSE for details.

#include "socket.hpp"
#include "consts.hpp"
#include <gtest/gtest.h>
#include <future>

using namespace testing;

class TCPAcceptorTestWithSocket : public Test {
protected:
  std::future<void> future;
  bool interrupted;
  unsigned short port;
  TCPAcceptorTestWithSocket() {
    this->port = 9580;
    this->interrupted = false;
    this->future = std::async([&]() {
      Poco::Net::StreamSocket socket1;

      while (!this->interrupted) {
        try {
          socket1.connect(Poco::Net::SocketAddress("127.0.0.1", port));
          break;
        } catch (...) {

        }
      }

      Poco::Net::StreamSocket socket2;

      while (!this->interrupted) {
        try {
          socket2.connect(Poco::Net::SocketAddress("127.0.0.1", port));
          break;
        } catch (...) {

        }
      }
    });
  }
  ~TCPAcceptorTestWithSocket() {
    this->interrupted = true;
  }
};

class TCPSocketTest : public Test {
protected:
  std::future<std::string> future;
  bool interrupted;
  unsigned short port;
  TCPSocketTest() {
    this->port = 1271;
    this->interrupted = false;
    this->future = std::async([&]() {
      Poco::Net::StreamSocket socket;

      while (!this->interrupted) {
        try {
          socket.connect(Poco::Net::SocketAddress("127.0.0.1", port));
          break;
        } catch (...) {

        }
      }

      char buf[1024];

      buf[0] = 22;
      buf[1] = 2;
      buf[2] = 0;
      buf[3] = 56;
      buf[4] = 99;
      buf[5] = 107;

      socket.sendBytes(buf, 6);
      socket.sendBytes(buf, 6);

      strncpy(buf, SOCKET_LOREM_IPSUM1, 1024);

      socket.sendBytes(buf, 1024);

      strncpy(buf, SOCKET_LOREM_IPSUM1, 1024);

      socket.sendBytes(buf, 1024);

      strncpy(buf, SOCKET_LOREM_IPSUM_SHORT "\n", 27);

      socket.sendBytes(buf, 27);

      socket.setReceiveTimeout(Poco::Timespan(15, 0));

      try {
        int bytes = socket.receiveBytes(buf, 1024, MSG_WAITALL);
        return std::string(buf, 1024);
      } catch (std::exception &e) {
        if (!this->interrupted) {
          throw e;
        }

        return std::string();
      }
    });
  }
  ~TCPSocketTest() {
    this->interrupted = true;
  }
};

TEST(TCPAcceptorTest, GetPortAndInstrWithNoSubsequentPorts) {
  const unsigned short port = 2475;
  aperf::TCPAcceptor::Factory factory("127.0.0.1", port, false);
  std::unique_ptr<aperf::Acceptor> acceptor = factory.make_acceptor(UNLIMITED_ACCEPTED);
  ASSERT_EQ(((aperf::TCPAcceptor *)acceptor.get())->get_port(), port);
  ASSERT_EQ(acceptor->get_connection_instructions(), std::to_string(port));
}

TEST(TCPAcceptorTest, GetPortAndInstrWithSubsequentPorts) {
  const unsigned short port = 3333;

  // Block port and port + 1
  Poco::Net::StreamSocket socket1, socket2;
  socket1.bind(Poco::Net::SocketAddress("127.0.0.1", port));
  socket2.bind(Poco::Net::SocketAddress("127.0.0.1", port + 1));

  // Test
  aperf::TCPAcceptor::Factory factory("127.0.0.1", port, true);
  std::unique_ptr<aperf::Acceptor> acceptor = factory.make_acceptor(UNLIMITED_ACCEPTED);
  ASSERT_EQ(((aperf::TCPAcceptor *)acceptor.get())->get_port(), port + 2);
  ASSERT_EQ(acceptor->get_connection_instructions(), std::to_string(port + 2));
}

TEST_F(TCPAcceptorTestWithSocket, AcceptWithTwoMaxAccepted) {
  const unsigned int buf_size1 = 1024;
  const unsigned int buf_size2 = 2048;

  aperf::TCPAcceptor::Factory factory("127.0.0.1", port, false);
  std::unique_ptr<aperf::Acceptor> acceptor = factory.make_acceptor(2);

  std::unique_ptr<aperf::Connection> connection1 = acceptor->accept(buf_size1);
  ASSERT_EQ(connection1->get_buf_size(), buf_size1);

  std::unique_ptr<aperf::Connection> connection2 = acceptor->accept(buf_size2);
  ASSERT_EQ(connection2->get_buf_size(), buf_size2);

  ASSERT_THROW({
      try {
        acceptor->accept(0);
      } catch (std::runtime_error &e) {
        if (std::string(e.what()) == "Maximum accepted connections reached.") {
          throw e;
        }
      } catch (...) {

      }
    }, std::runtime_error);
}

TEST_F(TCPSocketTest, SocketCorrectness) {
  const unsigned int buf_size = 512;

  Poco::Net::ServerSocket socket;
  socket.bind(Poco::Net::SocketAddress("127.0.0.1", port));
  socket.listen();
  Poco::Net::StreamSocket sock = socket.acceptConnection();

  aperf::TCPSocket tcp_socket(sock, buf_size);
  ASSERT_EQ(tcp_socket.get_address(), "127.0.0.1");
  ASSERT_EQ(tcp_socket.get_port(), port);
  ASSERT_EQ(tcp_socket.get_buf_size(), buf_size);

  char buf[12];
  int bytes = tcp_socket.read(buf, 12, 5);

  ASSERT_EQ(bytes, 12);
  ASSERT_EQ(buf[0], 22);
  ASSERT_EQ(buf[1], 2);
  ASSERT_EQ(buf[2], 0);
  ASSERT_EQ(buf[3], 56);
  ASSERT_EQ(buf[4], 99);
  ASSERT_EQ(buf[5], 107);
  ASSERT_EQ(buf[6], 22);
  ASSERT_EQ(buf[7], 2);
  ASSERT_EQ(buf[8], 0);
  ASSERT_EQ(buf[9], 56);
  ASSERT_EQ(buf[10], 99);
  ASSERT_EQ(buf[11], 107);

  std::string msg = tcp_socket.read();
  ASSERT_EQ(msg, SOCKET_LOREM_IPSUM1 SOCKET_LOREM_IPSUM1 SOCKET_LOREM_IPSUM_SHORT);

  ASSERT_THROW({
      tcp_socket.read(buf, 6, 5);
    }, aperf::TimeoutException);

  tcp_socket.write("123test!@#*@!$^^$@!(#*#&)@!$)*&!)&@#&@!$&!(*ABCDE", true);
  tcp_socket.write(SOCKET_LOREM_IPSUM2, false);

  interrupted = true;

  std::string message_str = future.get();

  ASSERT_EQ(message_str, "123test!@#*@!$^^$@!(#*#&)@!$)*&!)&@#&@!$&!(*ABCDE\n"
            SOCKET_LOREM_IPSUM2);
}
