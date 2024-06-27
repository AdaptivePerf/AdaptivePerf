// AdaptivePerf: comprehensive profiling tool based on Linux perf
// Copyright (C) CERN. See LICENSE for details.

#include "socket.hpp"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <fstream>
#include <poll.h>
#include <Poco/Buffer.h>
#include <Poco/Net/NetException.h>
#include <Poco/StreamCopier.h>
#include <Poco/FileStream.h>
#include <Poco/Net/SocketStream.h>

namespace aperf {
  class charstreambuf : public std::streambuf {
  public:
    charstreambuf(std::unique_ptr<char> &begin, unsigned int length) {
      this->setg(begin.get(), begin.get(), begin.get() + length - 1);
    }
  };

  TCPAcceptor::TCPAcceptor(std::string address, unsigned short port,
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

  TCPAcceptor::~TCPAcceptor() {
    this->close();
  }

  std::unique_ptr<Connection> TCPAcceptor::accept_connection(unsigned int buf_size) {
    try {
      net::StreamSocket socket = this->acceptor.acceptConnection();
      return std::make_unique<TCPSocket>(socket, buf_size);
    } catch (net::NetException &e) {
      throw ConnectionException(e);
    }
  }

  std::string TCPAcceptor::get_connection_instructions() {
    return this->acceptor.address().host().toString() + "_" + std::to_string(this->acceptor.address().port());
  }

  std::string TCPAcceptor::get_type() {
    return "tcp";
  }

  void TCPAcceptor::close() {
    this->acceptor.close();
  }

  TCPSocket::TCPSocket(net::StreamSocket & sock, unsigned int buf_size) {
    this->socket = sock;
    this->buf.reset(new char[buf_size]);
    this->buf_size = buf_size;
    this->start_pos = 0;
  }

  TCPSocket::~TCPSocket() {
    this->close();
  }

  std::string TCPSocket::get_address() {
    return this->socket.address().host().toString();
  }

  unsigned short TCPSocket::get_port() {
    return this->socket.address().port();
  }

  unsigned int TCPSocket::get_buf_size() {
    return this->buf_size;
  }

  void TCPSocket::close() {
    this->socket.close();
  }

  int TCPSocket::read(char *buf, unsigned int len, long timeout_seconds) {
    try {
      this->socket.setReceiveTimeout(Poco::Timespan(timeout_seconds, 0));
      int bytes = this->socket.receiveBytes(buf, len);
      this->socket.setReceiveTimeout(Poco::Timespan());
      return bytes;
    } catch (net::NetException &e) {
      this->socket.setReceiveTimeout(Poco::Timespan());
      throw ConnectionException(e);
    } catch (Poco::TimeoutException &e) {
      this->socket.setReceiveTimeout(Poco::Timespan());
      throw TimeoutException();
    }
  }

  std::string TCPSocket::read(long timeout_seconds) {
    try {
      if (!this->buffered_msgs.empty()) {
        std::string msg = this->buffered_msgs.front();
        this->buffered_msgs.pop();
        return msg;
      }

      std::string cur_msg = "";

      while (true) {
        int bytes_received;

        if (timeout_seconds == NO_TIMEOUT) {
          bytes_received =
            this->socket.receiveBytes(this->buf.get() + this->start_pos,
                                      this->buf_size - this->start_pos);
        } else {
          bytes_received =
              this->read(this->buf.get() + this->start_pos,
                         this->buf_size - this->start_pos, timeout_seconds);
        }

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

  void TCPSocket::write(std::string msg, bool new_line) {
    try {
      if (new_line) {
        msg += "\n";
      }

      const char *buf = msg.c_str();

      this->socket.sendBytes(buf, msg.size());
    } catch (net::NetException &e) {
      throw ConnectionException(e);
    }
  }

  void TCPSocket::write(fs::path file) {
    try {
      net::SocketStream socket_stream(this->socket);
      Poco::FileInputStream stream(file, std::ios::in | std::ios::binary);
      Poco::StreamCopier::copyStream(stream, socket_stream);
    } catch (net::NetException &e) {
      throw ConnectionException(e);
    }
  }

#ifndef SERVER_ONLY
  FileDescriptor::FileDescriptor(int read_fd[2], int write_fd[2],
                                 unsigned int buf_size) {
    this->buf.reset(new char[buf_size]);
    this->buf_size = buf_size;
    this->start_pos = 0;
    this->read_fd[0] = read_fd[0];
    this->read_fd[1] = read_fd[1];
    this->write_fd[0] = write_fd[0];
    this->write_fd[1] = write_fd[1];
  }

  FileDescriptor::~FileDescriptor() {
    this->close();
  }

  void FileDescriptor::close() {
    ::close(this->read_fd[0]);
    ::close(this->write_fd[1]);
  }

  int FileDescriptor::read(char *buf, unsigned int len, long timeout_seconds) {
    struct pollfd poll_struct;
    poll_struct.fd = this->read_fd[0];
    poll_struct.events = POLLIN;

    int code = ::poll(&poll_struct, 1, 1000 * timeout_seconds);

    if (code == -1) {
      throw ConnectionException();
    } else if (code == 0) {
      throw TimeoutException();
    }

    return ::read(this->read_fd[0], buf, len);
  }

  std::string FileDescriptor::read(long timeout_seconds) {
    if (!this->buffered_msgs.empty()) {
      std::string msg = this->buffered_msgs.front();
      this->buffered_msgs.pop();
      return msg;
    }

    std::string cur_msg = "";

    while (true) {
      int bytes_received;

      if (timeout_seconds == NO_TIMEOUT) {
        bytes_received =
            ::read(this->read_fd[0], this->buf.get() + this->start_pos,
                   this->buf_size - this->start_pos);

        if (bytes_received == -1) {
          throw ConnectionException();
        }
      } else {
        bytes_received = this->read(this->buf.get() + this->start_pos,
                                    this->buf_size - this->start_pos,
                                    timeout_seconds);
      }

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
  }

  void FileDescriptor::write(std::string msg, bool new_line) {
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

  void FileDescriptor::write(fs::path file) {
    std::unique_ptr<char> buf(new char[FILE_BUFFER_SIZE]);
    std::ifstream file_stream(file, std::ios_base::in |
                              std::ios_base::binary);

    if (!file_stream) {
      throw std::runtime_error("Could not open the file " +
                               file.string() + "!");
    }

    while (file_stream) {
      file_stream.read(buf.get(), FILE_BUFFER_SIZE);
      int bytes_read = file_stream.gcount();
      int bytes_written = ::write(this->write_fd[1], buf.get(),
                                  bytes_read);

      if (bytes_written != bytes_read) {
        throw std::runtime_error("Wrote " +
                                 std::to_string(bytes_written) +
                                 " bytes instead of " +
                                 std::to_string(bytes_read) +
                                 " to fd " +
                                 std::to_string(this->write_fd[1]));
      }
    }
  }

  unsigned int FileDescriptor::get_buf_size() {
    return this->buf_size;
  }

  PipeAcceptor::PipeAcceptor() : Acceptor(1) {
    if (pipe(this->read_fd) != 0) {
      throw std::runtime_error("Could not open read pipe for FileDescriptor, "
                               "code " + std::to_string(errno));
    }

    if (pipe(this->write_fd) != 0) {
      throw std::runtime_error("Could not open write pipe for FileDescriptor, "
                               "code " + std::to_string(errno));
    }
  }

  std::unique_ptr<Connection> PipeAcceptor::accept_connection(unsigned int buf_size) {
    std::string expected = "connect";
    const int size = expected.size();

    char buf[size];
    int bytes_received = 0;

    while (bytes_received < size) {
      int received = ::read(this->read_fd[0], buf + bytes_received,
                            size - bytes_received);

      if (received <= 0) {
        break;
      }

      bytes_received += received;
    }

    std::string msg(buf, size);

    if (msg != expected) {
      throw std::runtime_error("Message received from pipe when establishing connection "
                               "is \"" + msg + "\" instead of \"" + expected + "\".");
    }

    return std::unique_ptr<Connection>(new FileDescriptor(this->read_fd,
                                                          this->write_fd,
                                                          buf_size));
  }

  void PipeAcceptor::close() {}

  std::string PipeAcceptor::get_connection_instructions() {
    return std::to_string(this->write_fd[0]) + "_" + std::to_string(this->read_fd[1]);
  }

  std::string PipeAcceptor::get_type() {
    return "pipe";
  }
#endif
}
