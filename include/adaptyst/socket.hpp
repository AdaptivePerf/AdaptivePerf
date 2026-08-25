// SPDX-FileCopyrightText: 2026 CERN
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef ADAPTYST_SOCKET_HPP_
#define ADAPTYST_SOCKET_HPP_

#include "os_detect.h"
#include <string>
#include <queue>
#include <memory>
#include <iostream>
#include <filesystem>
#include <Poco/Net/ServerSocket.h>
#include <cstring>
#include <unistd.h>
#include <fstream>
#include <poll.h>
#include <Poco/Buffer.h>
#include <Poco/Net/NetException.h>
#include <Poco/StreamCopier.h>
#include <Poco/FileStream.h>
#include <Poco/Net/SocketStream.h>

#define UNLIMITED_ACCEPTED -1
#define NO_TIMEOUT -1

#ifndef FILE_BUFFER_SIZE
#define FILE_BUFFER_SIZE 1048576
#endif

namespace adaptyst {
  namespace net = Poco::Net;
  namespace fs = std::filesystem;

  class charstreambuf : public std::streambuf {
  public:
    charstreambuf(std::unique_ptr<char[]> &begin, unsigned int length) {
      this->setg(begin.get(), begin.get(), begin.get() + length - 1);
    }
  };

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
                              while waiting for the data. Use NO_TIMEOUT
                              for no timeout.

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

       @param buf_size The buffer size for communication, in bytes.
       @param timeout  The maximum number of seconds the acceptor will
                       wait for to accept a connection. Afterwards,
                       TimeoutException will be thrown. Use NO_TIMEOUT
                       to wait indefinitely for a connection.
    */
    virtual std::unique_ptr<Connection> accept_connection(unsigned int buf_size,
                                                          long timeout) = 0;

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
       @param timeout  The maximum number of seconds the acceptor
                       will wait for to accept a connection. Afterwards,
                       TimeoutException will be thrown. Use NO_TIMEOUT
                       to wait indefinitely for a connection.

       @throw std::runtime_error When the maximum number of accepted
                                 connections is reached.
       @throw TimeoutException   When the timeout is reached.
    */
    std::unique_ptr<Connection> accept(unsigned int buf_size,
                                       long timeout = NO_TIMEOUT) {
      if (this->max_accepted != UNLIMITED_ACCEPTED &&
          this->accepted >= this->max_accepted) {
        throw std::runtime_error("Maximum accepted connections reached.");
      }

      std::unique_ptr<Connection> connection = this->accept_connection(buf_size,
                                                                       timeout);
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
    std::unique_ptr<char[]> buf;
    unsigned int buf_size;
    int start_pos;
    std::queue<std::string> buffered_msgs;

  protected:
    void close();

  public:
    /**
       Constructs a TCPSocket object.

       @param sock     The Poco::Net::StreamSocket object corresponding to
                       the already-established TCP socket.
       @param buf_size The buffer size for communication, in bytes.
    */
    TCPSocket(net::StreamSocket &sock, unsigned int buf_size) {
      this->socket = sock;
      this->buf.reset(new char[buf_size]);
      this->buf_size = buf_size;
      this->start_pos = 0;
    }

    ~TCPSocket() {
      this->close();
    }

    std::string get_address() {
      return this->socket.address().host().toString();
    }

    unsigned short get_port() {
      return this->socket.address().port();
    }

    unsigned int get_buf_size() {
      return this->buf_size;
    }

    int read(char *buf, unsigned int len, long timeout_seconds) {
      auto set_timeout = [this, &timeout_seconds]() {
        if (timeout_seconds != NO_TIMEOUT) {
          this->socket.setReceiveTimeout(Poco::Timespan(timeout_seconds, 0));
        }
      };

      auto unset_timeout = [this, &timeout_seconds]() {
        if (timeout_seconds != NO_TIMEOUT) {
          this->socket.setReceiveTimeout(Poco::Timespan());
        }
      };

      try {
        set_timeout();
        int bytes = this->socket.receiveBytes(buf, len);
        unset_timeout();
        return bytes;
      } catch (net::NetException &e) {
        unset_timeout();
        throw ConnectionException(e);
      } catch (Poco::TimeoutException &e) {
        unset_timeout();
        throw TimeoutException();
      }
    }

    std::string read(long timeout_seconds = NO_TIMEOUT) {
      try {
        if (!this->buffered_msgs.empty()) {
          std::string msg = this->buffered_msgs.front();
          this->buffered_msgs.pop();
          return msg;
        }

        std::string cur_msg = "";

        while (true) {
          int bytes_received =
            this->read(this->buf.get() + this->start_pos,
                       this->buf_size - this->start_pos, timeout_seconds);

          if (bytes_received == 0) {
            return std::string(this->buf.get(), this->start_pos);
          }

          bool first_msg_to_receive = true;
          std::string first_msg;

          charstreambuf buf(this->buf, bytes_received + this->start_pos);
          std::istream in(&buf);

          int cur_pos = 0;
          bool last_is_newline = this->buf.get()[bytes_received + this->start_pos - 1] == '\n';

          while (!in.eof()) {
            std::string msg;
            std::getline(in, msg);

            if (in.eof() && !last_is_newline) {
              int size = bytes_received + this->start_pos - cur_pos;

              if (size == this->buf_size) {
                cur_msg += std::string(this->buf.get(), this->buf_size);
                this->start_pos = 0;
              } else {
                std::memmove(this->buf.get(), this->buf.get() + cur_pos, size);
                this->start_pos = size;
              }
            } else {
              if (!cur_msg.empty() || !msg.empty()) {
                if (first_msg_to_receive) {
                  first_msg = cur_msg + msg;
                  first_msg_to_receive = false;
                } else {
                  this->buffered_msgs.push(cur_msg + msg);
                }

                cur_msg = "";
              }

              cur_pos += msg.length() + 1;
            }
          }

          if (last_is_newline) {
            this->start_pos = 0;
          }

          if (!first_msg_to_receive) {
            return first_msg;
          }
        }

        // Should not get here.
        return "";
      } catch (net::NetException &e) {
        throw ConnectionException(e);
      }
    }

    void write(std::string msg, bool new_line) {
      try {
        if (new_line) {
          msg += "\n";
        }

        const char *buf = msg.c_str();

        int bytes_written = this->socket.sendBytes(buf, msg.size());

        if (bytes_written != msg.size()) {
          std::runtime_error err("Wrote " +
                                 std::to_string(bytes_written) +
                                 " bytes instead of " +
                                 std::to_string(msg.size()) +
                                 " to " +
                                 this->socket.address().toString());
          throw ConnectionException(err);
        }
      } catch (net::NetException &e) {
        throw ConnectionException(e);
      }
    }

    void write(fs::path file) {
      try {
        net::SocketStream socket_stream(this->socket);
        Poco::FileInputStream stream(file, std::ios::in | std::ios::binary);
        Poco::StreamCopier::copyStream(stream, socket_stream);
      } catch (net::NetException &e) {
        throw ConnectionException(e);
      }
    }

    void write(unsigned int len, char *buf) {
      try {
        int bytes_written = this->socket.sendBytes(buf, len);
        if (bytes_written != len) {
          std::runtime_error err("Wrote " +
                                 std::to_string(bytes_written) +
                                 " bytes instead of " +
                                 std::to_string(len) +
                                 " to " +
                                 this->socket.address().toString());
          throw ConnectionException(err);
        }
      } catch (net::NetException &e) {
        throw ConnectionException(e);
      }
    }
  };

  /**
     A class describing a TCP acceptor.
  */
  class TCPAcceptor : public Acceptor {
  private:
    net::ServerSocket acceptor;

    TCPAcceptor(std::string address, unsigned short port,
                int max_accepted,
                bool try_subsequent_ports) : Acceptor(max_accepted) {
      if (try_subsequent_ports) {
        bool success = false;
        while (!success) {
          try {
            this->acceptor.bind(net::SocketAddress(address, port), false);
            success = true;
          } catch (net::NetException &e) {
            if (e.message().find("already in use") != std::string::npos) {
              port++;
            } else {
              throw ConnectionException(e);
            }
          }
        }
      } else {
        try {
          this->acceptor.bind(net::SocketAddress(address, port), false);
        } catch (net::NetException &e) {
          if (e.message().find("already in use") != std::string::npos) {
            throw AlreadyInUseException();
          } else {
            throw ConnectionException(e);
          }
        }
      }

      try {
        this->acceptor.listen();
      } catch (net::NetException &e) {
        throw ConnectionException(e);
      }
    }

  protected:
    std::unique_ptr<Connection> accept_connection(unsigned int buf_size,
                                                  long timeout) {
      try {
        net::StreamSocket socket = this->acceptor.acceptConnection();
        return std::make_unique<TCPSocket>(socket, buf_size);
      } catch (net::NetException &e) {
        throw ConnectionException(e);
      }
    }

    void close() { this->acceptor.close(); }

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

    ~TCPAcceptor() {
      this->close();
    }

    /**
       Returns "<TCP server address>_<TCP server port>".
    */
    std::string get_connection_instructions() {
      return this->acceptor.address().host().toString() + "_" + std::to_string(this->acceptor.address().port());
    }

    std::string get_type() { return "tcp"; }
  };

#ifdef ADAPTYST_UNIX
  /**
     A class describing a file-descriptor-based connection.
     This is available only when compiled for Unix-based platforms.
  */
  class FileDescriptor : public Connection {
  private:
    int read_fd[2];
    int write_fd[2];
    unsigned int buf_size;
    std::queue<std::string> buffered_msgs;
    std::unique_ptr<char[]> buf;
    int start_pos;
    bool close_on_destruct;

  public:
    /**
       Constructs a FileDescriptor object.

       @param read_fd           The pair of file descriptors for read()
                                as returned by the pipe system call. Can
                                be nullptr.
       @param write_fd          The pair of file descriptors for write()
                                as returned by the pipe system call. Can
                                be nullptr.
       @param buf_size          The buffer size for communication, in bytes.
                                Can be set to zero if no read operations are
                                foreseen.
       @param close_on_destruct Whether the file descriptors should
                                be closed when the object is destroyed.
    */
    FileDescriptor(int read_fd[2],
                   int write_fd[2],
                   unsigned int buf_size,
                   bool close_on_destruct = true) {
      if (buf_size > 0) {
        this->buf.reset(new char[buf_size]);
      }

      this->buf_size = buf_size;
      this->start_pos = 0;
      this->close_on_destruct = close_on_destruct;

      if (read_fd != nullptr) {
        this->read_fd[0] = read_fd[0];
        this->read_fd[1] = read_fd[1];
      } else {
        this->read_fd[0] = -1;
        this->read_fd[1] = -1;
      }

      if (write_fd != nullptr) {
        this->write_fd[0] = write_fd[0];
        this->write_fd[1] = write_fd[1];
      } else {
        this->write_fd[0] = -1;
        this->write_fd[1] = -1;
      }
    }

    ~FileDescriptor() {
      if (this->close_on_destruct) {
        this->close();
      }
    }

    int read(char *buf, unsigned int len, long timeout_seconds) {
      if (timeout_seconds != NO_TIMEOUT) {
        struct pollfd poll_struct;
        poll_struct.fd = this->read_fd[0];
        poll_struct.events = POLLIN;

        int code = ::poll(&poll_struct, 1, 1000 * timeout_seconds);

        if (code == -1) {
          throw ConnectionException();
        } else if (code == 0) {
          throw TimeoutException();
        }
      }

      return ::read(this->read_fd[0], buf, len);
    }

    std::string read(long timeout_seconds = NO_TIMEOUT) {
      if (!this->buffered_msgs.empty()) {
        std::string msg = this->buffered_msgs.front();
        this->buffered_msgs.pop();
        return msg;
      }

      std::string cur_msg = "";

      while (true) {
        int bytes_received =
          this->read(this->buf.get() + this->start_pos,
                     this->buf_size - this->start_pos,
                     timeout_seconds);

        if (bytes_received == -1) {
          throw ConnectionException();
        } else if (bytes_received == 0) {
          return std::string(this->buf.get(), this->start_pos);
        }

        bool first_msg_to_receive = true;
        std::string first_msg;

        charstreambuf buf(this->buf, bytes_received + this->start_pos);
        std::istream in(&buf);

        int cur_pos = 0;
        bool last_is_newline = this->buf.get()[bytes_received + this->start_pos - 1] == '\n';

        while (!in.eof()) {
          std::string msg;
          std::getline(in, msg);

          if (in.eof() && !last_is_newline) {
            int size = bytes_received + this->start_pos - cur_pos;

            if (size == this->buf_size) {
              cur_msg += std::string(this->buf.get(), this->buf_size);
              this->start_pos = 0;
            } else {
              std::memmove(this->buf.get(), this->buf.get() + cur_pos, size);
              this->start_pos = size;
            }
          } else {
            if (!cur_msg.empty() || !msg.empty()) {
              if (first_msg_to_receive) {
                first_msg = cur_msg + msg;
                first_msg_to_receive = false;
              } else {
                this->buffered_msgs.push(cur_msg + msg);
              }

              cur_msg = "";
            }

            cur_pos += msg.length() + 1;
          }
        }

        if (last_is_newline) {
          this->start_pos = 0;
        }

        if (!first_msg_to_receive) {
          return first_msg;
        }
      }

      // Should not get here.
      return "";
    }

    void write(std::string msg, bool new_line) {
      if (new_line) {
        msg += "\n";
      }

      const char *buf = msg.c_str();
      int written = ::write(this->write_fd[1], buf, msg.size());

      if (written != msg.size()) {
        std::runtime_error err("Wrote " +
                               std::to_string(written) +
                               " bytes instead of " +
                               std::to_string(msg.size()) +
                               " to fd " +
                               std::to_string(this->write_fd[1]));
        throw ConnectionException(err);
      }
    }

    void write(fs::path file) {
      std::unique_ptr<char> buf(new char[FILE_BUFFER_SIZE]);
      std::ifstream file_stream(file, std::ios_base::in |
                                std::ios_base::binary);

      if (!file_stream) {
        std::runtime_error err("Could not open the file " +
                               file.string() + "!");
        throw ConnectionException(err);
      }

      while (file_stream) {
        file_stream.read(buf.get(), FILE_BUFFER_SIZE);
        int bytes_read = file_stream.gcount();
        int bytes_written = ::write(this->write_fd[1], buf.get(),
                                    bytes_read);

        if (bytes_written != bytes_read) {
          std::runtime_error err("Wrote " +
                                 std::to_string(bytes_written) +
                                 " bytes instead of " +
                                 std::to_string(bytes_read) +
                                 " to fd " +
                                 std::to_string(this->write_fd[1]));
          throw ConnectionException(err);
        }
      }
    }

    void write(unsigned int len, char *buf) {
      int bytes_written = ::write(this->write_fd[1], buf, len);

      if (bytes_written != len) {
        std::runtime_error err("Wrote " +
                               std::to_string(bytes_written) +
                               " bytes instead of " +
                               std::to_string(len) +
                               " to fd " +
                               std::to_string(this->write_fd[1]));
        throw ConnectionException(err);
      }
    }

    unsigned int get_buf_size() {
      return this->buf_size;
    }

    void close() {
      if (this->read_fd[0] != -1) {
        ::close(this->read_fd[0]);
        this->read_fd[0] = -1;
      }

      if (this->write_fd[1] != -1) {
        ::close(this->write_fd[1]);
        this->write_fd[1] = -1;
      }
    }

    std::pair<int, int> get_read_fd() {
      return std::make_pair(this->read_fd[0],
                            this->read_fd[1]);
    }

    std::pair<int, int> get_write_fd() {
      return std::make_pair(this->write_fd[0],
                            this->write_fd[1]);
    }
  };

  /**
     A class describing an inter-process pipe acceptor.
     This is available only when compiled for Unix-based platforms.
  */
  class PipeAcceptor : public Acceptor {
  private:
    int read_fd[2];
    int write_fd[2];

    /**
       Constructs a PipeAcceptor object.

       @throw ConnectionException When the pipe system call fails.
    */
    PipeAcceptor() : Acceptor(1) {
      if (pipe(this->read_fd) != 0) {
        std::runtime_error err("Could not open read pipe for FileDescriptor, "
                               "code " + std::to_string(errno));
        throw ConnectionException(err);
      }

      if (pipe(this->write_fd) != 0) {
        std::runtime_error err("Could not open write pipe for FileDescriptor, "
                               "code " + std::to_string(errno));
        throw ConnectionException(err);
      }
    }

  protected:
    std::unique_ptr<Connection> accept_connection(unsigned int buf_size,
                                                  long timeout) {
      std::string expected = "connect";
      const int size = expected.size();

      char buf[size];
      int bytes_received = 0;

      while (bytes_received < size) {
        if (timeout != NO_TIMEOUT) {
          struct pollfd poll_struct;
          poll_struct.fd = this->read_fd[0];
          poll_struct.events = POLLIN;

          int code = ::poll(&poll_struct, 1, 1000 * timeout);

          if (code == -1) {
            throw ConnectionException();
          } else if (code == 0) {
            throw TimeoutException();
          }
        }

        int received = ::read(this->read_fd[0], buf + bytes_received,
                              size - bytes_received);

        if (received <= 0) {
          break;
        }

        bytes_received += received;
      }

      std::string msg(buf, size);

      if (msg != expected) {
        std::runtime_error err("Message received from pipe when establishing connection "
                               "is \"" + msg + "\" instead of \"" + expected + "\".");
        throw ConnectionException(err);
      }

      return std::unique_ptr<Connection>(new FileDescriptor(this->read_fd,
                                                            this->write_fd,
                                                            buf_size));
    }

    void close() {}

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

    /**
       Returns "<file descriptor for reading from this end>_<file descriptor for writing by the other end>".
    */
    std::string get_connection_instructions() {
      return std::to_string(this->write_fd[0]) + "_" + std::to_string(this->read_fd[1]);
    }

    std::string get_type() { return "pipe"; }
  };
#endif
}

#endif
