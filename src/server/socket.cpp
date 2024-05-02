// AdaptivePerf: comprehensive profiling tool based on Linux perf
// Copyright (C) CERN. See LICENSE for details.

#include "socket.hpp"
#include <iostream>
#include <cstring>
#include <Poco/Buffer.h>
#include <Poco/Net/NetException.h>

namespace aperf {
  class charstreambuf : public std::streambuf {
  public:
    charstreambuf(std::unique_ptr<char> &begin, unsigned int length) {
      this->setg(begin.get(), begin.get(), begin.get() + length - 1);
    }
  };

  Acceptor::Acceptor(std::string address, unsigned short port,
                     bool try_subsequent_ports) {
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
            throw SocketException(e);
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
          throw SocketException(e);
        }
      }
    }

    try {
      this->acceptor.listen();
    } catch (net::NetException &e) {
      throw SocketException(e);
    }
  }

  Acceptor::~Acceptor() {
    this->close();
  }

  std::shared_ptr<Socket> Acceptor::accept(unsigned int buf_size) {
    try {
      net::StreamSocket socket = this->acceptor.acceptConnection();
      std::shared_ptr<Socket> sock = std::make_shared<Socket>(socket, buf_size);

      return sock;
    } catch (net::NetException &e) {
      throw SocketException(e);
    }
  }

  unsigned short Acceptor::get_port() {
    return this->acceptor.address().port();
  }

  void Acceptor::close() {
    this->acceptor.close();
  }

  Socket::Socket(net::StreamSocket & sock, unsigned int buf_size) {
    this->socket = sock;
    this->buf.reset(new char[buf_size]);
    this->buf_size = buf_size;
    this->start_pos = 0;
  }

  Socket::~Socket() {
    this->close();
  }

  std::string Socket::get_address() {
    return this->socket.address().host().toString();
  }

  unsigned short Socket::get_port() {
    return this->socket.address().port();
  }

  unsigned int Socket::get_buf_size() {
    return this->buf_size;
  }

  void Socket::close() {
    this->socket.close();
  }

  int Socket::read(char *buf, unsigned int len, long timeout_seconds) {
    try {
      this->socket.setReceiveTimeout(Poco::Timespan(timeout_seconds, 0));
      int bytes = this->socket.receiveBytes(buf, len, MSG_WAITALL);
      this->socket.setReceiveTimeout(Poco::Timespan());
      return bytes;
    } catch (net::NetException &e) {
      this->socket.setReceiveTimeout(Poco::Timespan());
      throw SocketException(e);
    } catch (Poco::TimeoutException &e) {
      this->socket.setReceiveTimeout(Poco::Timespan());
      throw TimeoutException();
    }
  }

  std::string Socket::read() {
    try {
      if (!this->buffered_msgs.empty()) {
        std::string msg = this->buffered_msgs.front();
        this->buffered_msgs.pop();
        return msg;
      }

      std::string cur_msg = "";

      while (true) {
        int bytes_received = this->socket.receiveBytes(this->buf.get() + this->start_pos,
                                                       this->buf_size - this->start_pos);

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
      throw SocketException(e);
    }
  }

  void Socket::write(std::string msg, bool new_line) {
    try {
      if (new_line) {
        msg += "\n";
      }

      const char *buf = msg.c_str();

      this->socket.sendBytes(buf, msg.size());
    } catch (net::NetException &e) {
      throw SocketException(e);
    }
  }
}
