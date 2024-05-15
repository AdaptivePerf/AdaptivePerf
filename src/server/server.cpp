// AdaptivePerf: comprehensive profiling tool based on Linux perf
// Copyright (C) CERN. See LICENSE for details.

#include "server.hpp"
#include <future>
#include <iostream>
#include <chrono>

namespace aperf {
  using namespace std::chrono_literals;

  Server::Server(std::unique_ptr<Acceptor> &acceptor,
                 unsigned int max_connections,
                 unsigned int buf_size,
                 unsigned long long file_timeout_speed) {
    this->acceptor = std::move(acceptor);
    this->max_connections = max_connections;
    this->buf_size = buf_size;
    this->file_timeout_speed = file_timeout_speed;
    this->interrupted = false;
  }

  void Server::run() {
    try {
      std::vector<std::unique_ptr<Client> > clients;
      std::vector<std::future<void> > threads;

      while (!interrupted) {
        std::unique_ptr<Socket> accepted_socket =
          this->acceptor->accept(this->buf_size);

        int working_count = 0;
        for (int i = 0; i < threads.size(); i++) {
          if (threads[i].wait_for(0ms) != std::future_status::ready) {
            working_count++;
          }
        }

        if (working_count >= std::max(1U, this->max_connections)) {
          accepted_socket->write("try_again");
        } else {
          clients.push_back(std::make_unique<Client>(accepted_socket, this->file_timeout_speed));
          threads.push_back(std::async(&Client::process, clients.back().get()));

          if (this->max_connections == 0) {
            break;
          }
        }
      }

      for (int i = 0; i < threads.size(); i++) {
        try {
          threads[i].get();
        } catch (aperf::SocketException &e) {
          std::cerr << "Warning: Socket error in client " << i << ", you will not ";
          std::cerr << "get reliable results from them!" << std::endl;

          std::cerr << "Error details: " << e.what() << std::endl;
        }
      }
    } catch (aperf::AlreadyInUseException &e) {
      throw e;
    } catch (aperf::SocketException &e) {
      throw e;
    } catch (...) {
      std::rethrow_exception(std::current_exception());
    }
  }

  void Server::interrupt() {
    this->interrupted = true;
  }
};
