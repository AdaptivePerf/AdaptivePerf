// AdaptivePerf: comprehensive profiling tool based on Linux perf
// Copyright (C) CERN. See LICENSE for details.

#ifndef SOCKET_HPP_
#define SOCKET_HPP_

#include <string>
#include <queue>
#include <memory>
#include <Poco/Net/ServerSocket.h>

#define UNLIMITED_ACCEPTED -1

namespace aperf {
  namespace net = Poco::Net;

  class AlreadyInUseException : std::exception {

  };

  class ConnectionException : public std::exception {
  public:
    ConnectionException(std::exception &other) : std::exception(other) { }
  };

  class TimeoutException : std::exception {

  };

  class Connection {
  public:
    virtual ~Connection() { }
    virtual int read(char *buf, unsigned int len, long timeout_seconds) = 0;
    virtual std::string read() = 0;
    virtual void write(std::string msg, bool new_line = true) = 0;
    virtual unsigned int get_buf_size() = 0;
    virtual void close() = 0;
  };

  class Socket : public Connection {
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
  private:
    int max_accepted;
    int accepted;

  protected:
    virtual std::unique_ptr<Connection> accept_connection(unsigned int buf_size) = 0;

  public:
    class Factory {
    public:
      virtual std::unique_ptr<Acceptor> make_acceptor(int max_accepted) = 0;
    };

    Acceptor(int max_accepted) {
      this->max_accepted = max_accepted;
      this->accepted = 0;
    }

    std::unique_ptr<Connection> accept(unsigned int buf_size) {
      if (this->max_accepted != UNLIMITED_ACCEPTED &&
          this->accepted >= this->max_accepted) {
        throw std::runtime_error("Maximum accepted connections reached.");
      }

      std::unique_ptr<Connection> connection = this->accept_connection(buf_size);
      this->accepted++;

      return connection;
    }

    virtual ~Acceptor() { }
    virtual std::string get_connection_instructions() = 0;
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

    TCPAcceptor(std::string address, unsigned short port,
                int max_accepted,
                bool try_subsequent_ports);

  protected:
    std::unique_ptr<Connection> accept_connection(unsigned int buf_size);

  public:
    class Factory : public Acceptor::Factory {
    private:
      std::string address;
      unsigned short port;
      bool try_subsequent_ports;

    public:
      Factory(std::string address, unsigned short port,
              bool try_subsequent_ports = false) {
        this->address = address;
        this->port = port;
        this->try_subsequent_ports = try_subsequent_ports;
      };

      std::unique_ptr<Acceptor> make_acceptor(int max_accepted) {
        return std::unique_ptr<Acceptor>(new TCPAcceptor(this->address,
                                                         this->port,
                                                         max_accepted,
                                                         this->try_subsequent_ports));
      }
    };

    ~TCPAcceptor();
    unsigned short get_port();
    std::string get_connection_instructions();
    void close();
  };
}

#endif
