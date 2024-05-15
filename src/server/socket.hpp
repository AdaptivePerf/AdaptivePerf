// AdaptivePerf: comprehensive profiling tool based on Linux perf
// Copyright (C) CERN. See LICENSE for details.

#ifndef SOCKET_HPP_
#define SOCKET_HPP_

#include <string>
#include <queue>
#include <memory>
#include <Poco/Net/ServerSocket.h>

namespace aperf {
  namespace net = Poco::Net;

  class AlreadyInUseException : std::exception {

  };

  class SocketException : public std::exception {
  public:
    SocketException(std::exception &other) : std::exception(other) { }
  };

  class TimeoutException : std::exception {

  };

  class Socket {
  public:
    virtual ~Socket() { }
    virtual std::string get_address() = 0;
    virtual unsigned short get_port() = 0;
    virtual unsigned int get_buf_size() = 0;
    virtual void close() = 0;
    virtual int read(char *buf, unsigned int len, long timeout_seconds) = 0;
    virtual std::string read() = 0;
    virtual void write(std::string msg, bool new_line = true) = 0;
  };

  class Acceptor {
  public:
    Acceptor(std::string address, unsigned short port,
             bool try_subsequent_ports) { }
    virtual ~Acceptor() { }
    virtual std::unique_ptr<Socket> accept(unsigned int buf_size) = 0;
    virtual unsigned short get_port() = 0;
    virtual void close() = 0;
  };

  class TCPSocket : public Socket {
  private:
    net::StreamSocket socket;
    std::unique_ptr<char> buf;
    unsigned int buf_size;
    int start_pos;
    std::queue<std::string> buffered_msgs;

  public:
    TCPSocket(net::StreamSocket &sock, unsigned int buf_size);
    ~TCPSocket();
    std::string get_address();
    unsigned short get_port();
    unsigned int get_buf_size();
    void close();
    int read(char *buf, unsigned int len, long timeout_seconds);
    std::string read();
    void write(std::string msg, bool new_line = true);
  };

  class TCPAcceptor : public Acceptor {
  private:
    net::ServerSocket acceptor;

  public:
    TCPAcceptor(std::string address, unsigned short port,
                bool try_subsequent_ports = false);
    ~TCPAcceptor();
    std::unique_ptr<Socket> accept(unsigned int buf_size);
    unsigned short get_port();
    void close();
  };
}

#endif
