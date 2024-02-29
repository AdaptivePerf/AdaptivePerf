#ifndef SOCKET_HPP_
#define SOCKET_HPP_

#include <string>
#include <queue>
#include <memory>
#include <Poco/Net/ServerSocket.h>

#define BUF_SIZE 1024

namespace aperf {
  namespace net = Poco::Net;

  class AlreadyInUseException : std::exception {

  };

  class Socket {
  private:
    net::StreamSocket socket;
    std::unique_ptr<char> buf;
    unsigned int buf_size;
    int start_pos;
    std::queue<std::string> buffered_msgs;

  public:
    Socket(net::StreamSocket & sock, unsigned int buf_size);
    ~Socket();
    unsigned short get_port();
    void close();
    std::string read();
    void write(std::string msg, bool new_line = true);
  };

  class Acceptor {
  private:
    net::ServerSocket acceptor;

  public:
    Acceptor(std::string address, unsigned short port,
             bool try_subsequent_ports = false);
    ~Acceptor();
    std::shared_ptr<Socket> accept(unsigned int buf_size = BUF_SIZE);
    unsigned short get_port();
    void close();
  };
}

#endif
