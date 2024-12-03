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

  /**
     An exception which is thrown when a connection error occurs.

     This can serve as a wrapper for another exception thrown by
     a Connection implementation.
  */
  class ConnectionException : public std::exception {
  public:
    ConnectionException() {}
    ConnectionException(std::exception &other) : std::exception(other) { }
  };

  /**
     An exception which is thrown when the specified address/port is
     already in use.
  */
  class AlreadyInUseException : public ConnectionException {

  };

  /**
     An exception which is thrown in case of timeout.
  */
  class TimeoutException : public std::exception {

  };

  /**
     An interface describing a two-end connection.
  */
  class Connection {
  protected:
    /**
       Closes the connection.
    */
    virtual void close() = 0;

  public:
    virtual ~Connection() { }

    /**
       Reads data from the connection.

       @param buf             A buffer where received data should
                              be stored.
       @param len             The size of the buffer.
       @param timeout_seconds A maximum number of seconds that can pass
                              while waiting for the data.

       @throw TimeoutException    In case of timeout (see timeout_seconds).
       @throw ConnectionException In case of any other errors.
    */
    virtual int read(char *buf, unsigned int len, long timeout_seconds) = 0;

    /**
       Reads a line from the connection.

       @param timeout_seconds A maximum number of seconds that can pass
                              while waiting for the data. Use NO_TIMEOUT for
                              no timeout.

       @throw TimeoutException    In case of timeout (see timeout_seconds).
       @throw ConnectionException In case of any other errors.
    */
    virtual std::string read(long timeout_seconds = NO_TIMEOUT) = 0;

    /**
       Writes a string to the connection.

       @param msg      A string to be sent.
       @param new_line Indicates whether a newline character should be
                       appended to the string.

       @throw ConnectionException In case of any errors.
    */
    virtual void write(std::string msg, bool new_line = true) = 0;

    /**
       Writes a file to the connection.

       @param file The path to a file to be sent.

       @throw ConnectionException In case of any errors.
    */
    virtual void write(fs::path file) = 0;

    /**
       Writes data to the connection.

       @param len The number of bytes to be sent.
       @param buf A buffer storing data to be written. Its size
                  must be equal to or greater than the number of
                  bytes to be sent.
    */
    virtual void write(unsigned int len, char *buf) = 0;

    /**
       Gets the buffer size for communication, in bytes.
    */
    virtual unsigned int get_buf_size() = 0;
  };

  /**
     An interface describing a network socket.
  */
  class Socket : public Connection {
  protected:
    virtual void close() = 0;

  public:
    virtual ~Socket() { }

    /**
       Gets the socket address string.
    */
    virtual std::string get_address() = 0;

    /**
       Gets the port of the socket.
    */
    virtual unsigned short get_port() = 0;

    virtual unsigned int get_buf_size() = 0;
    virtual int read(char *buf, unsigned int len, long timeout_seconds) = 0;
    virtual std::string read(long timeout_seconds = NO_TIMEOUT) = 0;
    virtual void write(std::string msg, bool new_line = true) = 0;
    virtual void write(fs::path file) = 0;
    virtual void write(unsigned int len, char *buf) = 0;
  };

  /**
     A class describing a connection acceptor.
  */
  class Acceptor {
  private:
    int max_accepted;
    int accepted;

  protected:
    /**
       Constructs an Acceptor object.

       @param max_accepted A maximum number of connections that
                           the acceptor can accept during its lifetime.
                           Use UNLIMITED_ACCEPTED for no limit.
    */
    Acceptor(int max_accepted) {
      this->max_accepted = max_accepted;
      this->accepted = 0;
    }

    /**
       An internal method called by accept() accepting a new connection.

       It should always return the new connection, regardless of the
       number of connections already accepted by the object.
    */
    virtual std::unique_ptr<Connection> accept_connection(unsigned int buf_size) = 0;

    /**
       Closes the acceptor.
    */
    virtual void close() = 0;

  public:
    /**
       An Acceptor factory.
    */
    class Factory {
    public:
      /**
         Makes a new Acceptor-derived object.

         @param max_accepted A maximum number of connections that
                             the acceptor can accept during its lifetime.
                             Use UNLIMITED_ACCEPTED for no limit.
      */
      virtual std::unique_ptr<Acceptor> make_acceptor(int max_accepted) = 0;

      /**
         Gets the string describing the connection type of the acceptor
         (e.g. TCP).
      */
      virtual std::string get_type() = 0;
    };

    /**
       Accepts a new connection.

       If the maximum number of accepted connections is reached,
       a runtime error is thrown immediately.

       @param buf_size The buffer size for communication, in bytes.

       @throw std::runtime_error When the maximum number of accepted
                                 connections is reached.
    */
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

    /**
       Gets the instructions how the other end of the connection should
       connect to this end so that accept() can return a Connection-derived
       object.

       These are in form of a "<field1>_<field2>_..._<fieldX>" string, where
       the number of fields and their content are implementation-dependent.
    */
    virtual std::string get_connection_instructions() = 0;

    /**
       Gets the string describing the connection type of the acceptor
       (e.g. TCP).
    */
    virtual std::string get_type() = 0;
  };

  /**
     A class describing a TCP socket.
  */
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
    void write(unsigned int len, char *buf);
  };

  /**
     A class describing a TCP acceptor.
  */
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
    /**
       A TCPAcceptor factory.
    */
    class Factory : public Acceptor::Factory {
    private:
      std::string address;
      unsigned short port;
      bool try_subsequent_ports;

    public:
      /**
         Constructs a TCPAcceptor::Factory object.

         @param address              An address where the TCP server should listen at.
         @param port                 A port where the TCP server should listen at.
         @param try_subsequent_ports Indicates whether subsequent ports should be
                                     tried when the initially-specified port is
                                     already in use. The potential port change
                                     will be reflected in the output of
                                     get_connection_instructions().
      */
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

  /**
     A class describing a file-descriptor-based connection.
  */
  class FileDescriptor : public Connection {
  private:
    int read_fd[2];
    int write_fd[2];
    unsigned int buf_size;
    std::queue<std::string> buffered_msgs;
    std::unique_ptr<char> buf;
    int start_pos;

  public:
    FileDescriptor(int read_fd[2],
                   int write_fd[2],
                   unsigned int buf_size);
    ~FileDescriptor();
    int read(char *buf, unsigned int len, long timeout_seconds);
    std::string read(long timeout_seconds = NO_TIMEOUT);
    void write(std::string msg, bool new_line);
    void write(fs::path file);
    void write(unsigned int len, char *buf);
    unsigned int get_buf_size();
    void close();
  };

  /**
     A class describing an inter-process pipe acceptor.
  */
  class PipeAcceptor : public Acceptor {
  private:
    int read_fd[2];
    int write_fd[2];
    PipeAcceptor();

  protected:
    std::unique_ptr<Connection> accept_connection(unsigned int buf_size);
    void close();

  public:
    /**
       A PipeAcceptor factory.
    */
    class Factory : public Acceptor::Factory {
    public:
      /**
         Makes a new PipeAcceptor object.

         @param max_accepted Must be set to 1.

         @throw std::runtime_error  When max_accepted is not 1.
         @throw ConnectionException In case of any other errors.
      */
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
}

#endif
