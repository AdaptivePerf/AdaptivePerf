// AdaptivePerf: comprehensive profiling tool based on Linux perf
// Copyright (C) CERN. See LICENSE for details.

#include "entrypoint.hpp"
#include "server.hpp"
#include "version.hpp"
#include <CLI/CLI.hpp>

int server_entrypoint(int argc, char **argv) {
  CLI::App app("Post-processing server for AdaptivePerf");

  bool version = false;
  app.add_flag("-v,--version", version, "Print version and exit");

  std::string address = "127.0.0.1";
  app.add_option("-a", address, "Address to bind to (default: 127.0.0.1)");

  unsigned short port = 5000;
  app.add_option("-p", port, "Port to bind to (default: 5000)");

  unsigned int max_connections = 1;
  app.add_option("-m", max_connections,
                 "Max simultaneous connections to accept "
                 "(default: 1, use 0 to exit after the first client)");

  unsigned int buf_size = 1024;
  app.add_option("-b", buf_size,
                 "Buffer size for communication with clients in bytes "
                 "(default: 1024)");

  unsigned long long file_timeout_speed = 10;
  app.add_option("-t", file_timeout_speed,
                 "Worst-case-assumed speed for receiving files from clients "
                 "in bytes per second (default: 10, used for calculating timeout)");

  bool quiet = false;
  app.add_flag("-q", quiet, "Do not print anything except non-port-in-use errors");

  CLI11_PARSE(app, argc, argv);

  if (version) {
    std::cout << aperf::version << std::endl;
    return 0;
  } else {
    try {
      aperf::TCPAcceptor::Factory factory(address, port, false);
      std::unique_ptr<aperf::Acceptor> acceptor =
        factory.make_acceptor(UNLIMITED_ACCEPTED);

      std::unique_ptr<aperf::Acceptor::Factory> acceptor_factory =
        std::make_unique<aperf::TCPAcceptor::Factory>(address,
                                                      port + 1, true);
      std::unique_ptr<aperf::Subclient::Factory> subclient_factory =
        std::make_unique<aperf::StdSubclient::Factory>(acceptor_factory);
      std::unique_ptr<aperf::Client::Factory> client_factory =
        std::make_unique<aperf::StdClient::Factory>(subclient_factory);

      aperf::Server server(acceptor, max_connections, buf_size,
                           file_timeout_speed);

      if (!quiet) {
        std::cout << "Listening on " << address << ", port " << port;
        std::cout << " (TCP)..." << std::endl;
      }

      server.run(client_factory);

      return 0;
    } catch (aperf::AlreadyInUseException &e) {
      if (!quiet) {
        std::cerr << address << ":" << port << " is in use! Please use a ";
        std::cerr << "different address and/or port." << std::endl;
      }

      return 100;
    } catch (aperf::ConnectionException &e) {
      std::cerr << "A connection error has occurred and adaptiveperf-server has to exit!" << std::endl;
      std::cerr << "You may want to check the address/port settings and the stability of " << std::endl;
      std::cerr << "your connection between the server and the client(s)." << std::endl;
      std::cerr << std::endl;
      std::cerr << "The error details are printed below." << std::endl;
      std::cerr << "----------" << std::endl;
      std::cerr << e.what() << std::endl;

      return 1;
    } catch (...) {
      std::cerr << "A fatal error has occurred and adaptiveperf-server has to exit!" << std::endl;
      std::cerr << "The exception will be rethrown to aid debugging." << std::endl;
      std::cerr << std::endl;
      std::cerr << "If this issue persists, please get in touch with the AdaptivePerf developers." << std::endl;
      std::cerr << "----------" << std::endl;
      std::rethrow_exception(std::current_exception());
    }
  }
}
