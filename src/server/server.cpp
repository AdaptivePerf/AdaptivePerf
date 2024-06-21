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
                 unsigned long long file_timeout_seconds) {
    this->acceptor = std::move(acceptor);
    this->max_connections = max_connections;
    this->buf_size = buf_size;
    this->file_timeout_seconds = file_timeout_seconds;
    this->interrupted = false;
  }

  void Server::run(std::unique_ptr<Client::Factory> &client_factory,
                   std::unique_ptr<Acceptor::Factory> &file_acceptor_factory) {
    try {
      std::vector<std::unique_ptr<Client> > clients;
      std::vector<std::future<void> > threads;

      while (!interrupted) {
        std::unique_ptr<Connection> connection =
          this->acceptor->accept(this->buf_size);
        std::unique_ptr<Acceptor> file_acceptor =
          file_acceptor_factory->make_acceptor(UNLIMITED_ACCEPTED);

        int working_count = 0;
        for (int i = 0; i < threads.size(); i++) {
          if (threads[i].wait_for(0ms) != std::future_status::ready) {
            working_count++;
          }
        }

        if (working_count >= std::max(1U, this->max_connections)) {
          connection->write("try_again", true);
        } else {
          clients.push_back(client_factory->make_client(connection,
                                                        file_acceptor,
                                                        this->file_timeout_seconds));

          Client *client = clients.back().get();
          threads.push_back(std::async([client]() { client->process(); }));

          if (this->max_connections == 0) {
            break;
          }
        }
      }

      for (int i = 0; i < threads.size(); i++) {
        try {
          threads[i].get();
        } catch (aperf::ConnectionException &e) {
          std::cerr << "Warning: Connection error in client " << i << ", you will not ";
          std::cerr << "get reliable results from them!" << std::endl;

          std::cerr << "Error details: " << e.what() << std::endl;
        }
      }
    } catch (aperf::AlreadyInUseException &e) {
      throw e;
    } catch (aperf::ConnectionException &e) {
      throw e;
    } catch (...) {
      std::rethrow_exception(std::current_exception());
    }
  }

  void Server::interrupt() {
    this->interrupted = true;
  }
};
