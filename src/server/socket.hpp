// AdaptivePerf: comprehensive profiling tool based on Linux perf
// Copyright (C) CERN. See LICENSE for details.

#ifndef SOCKET_HPP_
#define SOCKET_HPP_

#include <string>
#include <queue>
#include <memory>
#include <iostream>
#include <filesystem>
#include <Poco/Net/ServerSocket.h>

#define UNLIMITED_ACCEPTED -1
#define NO_TIMEOUT -1

#ifndef FILE_BUFFER_SIZE
#define FILE_BUFFER_SIZE 1048576
#endif

namespace aperf {
  namespace net = Poco::Net;
  namespace fs = std::filesystem;

  class ConnectionException : public std::exception {
  public:
    ConnectionException() {}
    ConnectionException(std::exception &other) : std::exception(other) { }
  };

  class AlreadyInUseException : public ConnectionException {

  };

  class TimeoutException : public std::exception {

  };

  class Connection {
  protected:
    virtual void close() = 0;

  public:
    virtual ~Connection() { }
    virtual int read(char *buf, unsigned int len, long timeout_seconds) = 0;
    virtual std::string read(long timeout_seconds = NO_TIMEOUT) = 0;
    virtual void write(std::string msg, bool new_line = true) = 0;
    virtual void write(fs::path file) = 0;
    virtual unsigned int get_buf_size() = 0;
  };

  class Socket : public Connection {
  protected:
    virtual void close() = 0;

  public:
    virtual ~Socket() { }
    virtual std::string get_address() = 0;
    virtual unsigned short get_port() = 0;
    virtual unsigned int get_buf_size() = 0;
    virtual int read(char *buf, unsigned int len, long timeout_seconds) = 0;
    virtual std::string read(long timeout_seconds = NO_TIMEOUT) = 0;
    virtual void write(std::string msg, bool new_line = true) = 0;
    virtual void write(fs::path file) = 0;
  };

  class Acceptor {
  private:
    int max_accepted;
    int accepted;

  protected:
    virtual std::unique_ptr<Connection> accept_connection(unsigned int buf_size) = 0;
    virtual void close() = 0;

  public:
    class Factory {
    public:
      virtual std::unique_ptr<Acceptor> make_acceptor(int max_accepted) = 0;
      virtual std::string get_type() = 0;
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
    virtual std::string get_type() = 0;
  };

  class TCPSocket : public Socket {
  private:
    net::StreamSocket socket;
    std::unique_ptr<char> buf;
    unsigned int buf_size;
    int start_pos;
    std::queue<std::string> buffered_msgs;

  protected:
    void close();

  public:
    TCPSocket(net::StreamSocket &sock, unsigned int buf_size);
    ~TCPSocket();
    std::string get_address();
    unsigned short get_port();
    unsigned int get_buf_size();
    int read(char *buf, unsigned int len, long timeout_seconds);
    std::string read(long timeout_seconds = NO_TIMEOUT);
    void write(std::string msg, bool new_line);
    void write(fs::path file);
  };

  class TCPAcceptor : public Acceptor {
  private:
    net::ServerSocket acceptor;

    TCPAcceptor(std::string address, unsigned short port,
                int max_accepted,
                bool try_subsequent_ports);

  protected:
    std::unique_ptr<Connection> accept_connection(unsigned int buf_size);
    void close();

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

      std::string get_type() {
        return "tcp";
      }
    };

    ~TCPAcceptor();
    std::string get_connection_instructions();
    std::string get_type();
  };

#ifndef SERVER_ONLY
  class FileDescriptor : public Connection {
  private:
    int read_fd[2];
    int write_fd[2];
    unsigned int buf_size;
    std::queue<std::string> buffered_msgs;
    std::unique_ptr<char> buf;
    int start_pos;

  protected:
    void close();

  public:
    FileDescriptor(int read_fd[2],
                   int write_fd[2],
                   unsigned int buf_size);
    ~FileDescriptor();
    int read(char *buf, unsigned int len, long timeout_seconds);
    std::string read(long timeout_seconds = NO_TIMEOUT);
    void write(std::string msg, bool new_line);
    void write(fs::path file);
    unsigned int get_buf_size();
  };

  class PipeAcceptor : public Acceptor {
  private:
    int read_fd[2];
    int write_fd[2];
    PipeAcceptor();

  protected:
    std::unique_ptr<Connection> accept_connection(unsigned int buf_size);
    void close();

  public:
    class Factory : public Acceptor::Factory {
    public:
      std::unique_ptr<Acceptor> make_acceptor(int max_accepted) {
        if (max_accepted != 1) {
          throw std::runtime_error("max_accepted can only be 1 for FileDescriptor");
        }

        return std::unique_ptr<Acceptor>(new PipeAcceptor());
      }

      std::string get_type() {
        return "pipe";
      }
    };

    std::string get_connection_instructions();
    std::string get_type();
  };
#endif
}

#endif
