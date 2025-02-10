// AdaptivePerf: comprehensive profiling tool based on Linux perf
// Copyright (C) CERN. See LICENSE for details.

#ifndef SERVER_HPP_
#define SERVER_HPP_

#include "socket.hpp"
#include <nlohmann/json.hpp>
#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>

namespace aperf {
  /**
     An interface whose implementation can be sent a notification
     by a different thread.
  */
  class Notifiable {
  public:
    /**
       Notifies a Notifiable-derived object.
    */
    virtual void notify() = 0;
  };

  /**
     An interface describing a client.

     See the main page for learning how the server, clients, and subclients work.
  */
  class Client : public Notifiable {
  public:
    /**
       A Client factory.
    */
    class Factory {
    public:
      /**
         Makes a Client-derived object.

         @param connection           A connection used for communicating between the
                                     client and the frontend.
         @param file_acceptor        An acceptor used for opening a connection for
                                     file transfer between the client and the frontend.
         @param file_timeout_seconds A maximum number of seconds the client can wait
                                     for receiving a next packet of data during file transfer.
      */
      virtual std::unique_ptr<Client> make_client(std::unique_ptr<Connection> &connection,
                                                  std::unique_ptr<Acceptor> &file_acceptor,
                                                  unsigned long long file_timeout_seconds) = 0;
    };

    virtual ~Client() { }

    /**
       Starts the client processing loop.

       @param working_dir A working directory where all profiling results are stored.
    */
    virtual void process(fs::path working_dir = fs::current_path()) = 0;

    /**
       Notifies the client that a subclient has made a connection with the frontend.

       This method should be called by a Subclient-derived object.
    */
    virtual void notify() = 0;

    /**
       Gets the Unix timestamp of the start of profiling and saves it to the variable
       referenced by tstamp if tstamp is not null.

       Returns false if profiling hasn't started yet, true otherwise.

       The value referenced by tstamp is unchanged if false is returned or
       tstamp is null.
    */
    virtual bool get_profile_start_tstamp(unsigned long long *tstamp) = 0;
  };

  /**
     An interface describing a subclient.

     A subclient is usually a separate thread reporting to the client
     (i.e. a Client-derived object).

     See the main page for learning how the server, clients, and subclients work.
  */
  class Subclient {
  public:
    /**
       A Subclient factory.
    */
    class Factory {
    public:
      /**
         Makes a new Subclient-derived object.

         @param context            A context a subclient should notify when the
                                   frontend successfully connects to it.
         @param profiled_filename  The filename of a profiled executable.
         @param buf_size           A buffer size used for communication, in bytes.
      */
      virtual std::unique_ptr<Subclient> make_subclient(Client &context,
                                                        std::string profiled_filename,
                                                        unsigned int buf_size) = 0;

      /**
         Gets a string describing the connection type the frontend should use for
         connecting to a subclient made by the factory (e.g. TCP).
      */
      virtual std::string get_type() = 0;
    };

    virtual ~Subclient() {}

    /**
       Starts the processing loop of the subclient.

       The method should produce a JSON object that can be retrieved later
       by calling get_result().
    */
    virtual void process() = 0;

    /**
       Gets the JSON object produced after the call to process() finishes.
    */
    virtual nlohmann::json &get_result() = 0;

    /**
       Gets a string describing how the frontend should connect to the
       subclient.

       The string is in form of "<field1>_<field2>_..._<fieldX>" where the
       number of fields and their content are implementation-dependent.
    */
    virtual std::string get_connection_instructions() = 0;
  };

  /**
     An internal class describing a Subclient with some fields initialised.

     This is used for avoiding duplication between the source code
     and the test code.
  */
  class InitSubclient : public Subclient {
  protected:
    Client &context;
    std::unique_ptr<Acceptor> acceptor;
    std::string profiled_filename;
    unsigned int buf_size;

    InitSubclient(Client &context,
                  std::unique_ptr<Acceptor> &acceptor,
                  std::string profiled_filename,
                  unsigned int buf_size) : context(context) {
      this->acceptor = std::move(acceptor);
      this->profiled_filename = profiled_filename;
      this->buf_size = buf_size;
    }

  public:
    virtual void process() = 0;
    virtual nlohmann::json &get_result() = 0;
    std::string get_connection_instructions() {
      return this->acceptor->get_connection_instructions();
    }
  };

  /**
     An internal class describing a Client with some fields initialised.

     This is used for avoiding duplication between the source code
     and the test code.
  */
  class InitClient : public Client {
  protected:
    std::shared_ptr<Subclient::Factory> subclient_factory;
    std::unique_ptr<Connection> connection;
    std::unique_ptr<Acceptor> file_acceptor;
    unsigned long long file_timeout_seconds;

    InitClient(std::shared_ptr<Subclient::Factory> &subclient_factory,
               std::unique_ptr<Connection> &connection,
               std::unique_ptr<Acceptor> &file_acceptor,
               unsigned long long file_timeout_seconds) {
      this->subclient_factory = subclient_factory;
      this->connection = std::move(connection);
      this->file_acceptor = std::move(file_acceptor);
      this->file_timeout_seconds = file_timeout_seconds;
    }

  public:
    virtual void process(fs::path working_dir) = 0;
    virtual void notify() = 0;
    virtual bool get_profile_start_tstamp(unsigned long long *tstamp) = 0;
  };

  /**
     A class describing a standard subclient.

     This class should be used for instantiating new subclients. See the main page for
     learning how the server, clients, and subclients work.
  */
  class StdSubclient : public InitSubclient {
  private:
    nlohmann::json json_result;

    StdSubclient(Client &context,
                 std::unique_ptr<Acceptor> &acceptor,
                 std::string profiled_filename,
                 unsigned int buf_size);
    void recurse(nlohmann::json &cur_elem,
                 std::vector<std::pair<std::string, std::string> > &callchain_parts,
                 int callchain_index,
                 unsigned long long period,
                 bool time_ordered, bool offcpu);

  public:
    /**
       A StdSubclient factory.
    */
    class Factory : public Subclient::Factory {
    private:
      std::shared_ptr<Acceptor::Factory> factory;

    public:
      /**
         Constructs a StdSubclient::Factory object.

         @param factory  An Acceptor factory. StdSubclient needs an acceptor
                         as a way of establishing the conection between
                         the subclient and the frontend.
      */
      Factory(std::unique_ptr<Acceptor::Factory> &factory) {
        this->factory = std::move(factory);
      }

      std::unique_ptr<Subclient> make_subclient(Client &context,
                                                std::string profiled_filename,
                                                unsigned int buf_size) {
        std::unique_ptr<Acceptor> acceptor = this->factory->make_acceptor(1);
        return std::unique_ptr<
          StdSubclient>(new StdSubclient(context, acceptor,
                                         profiled_filename, buf_size));
      }

      std::string get_type() {
        return this->factory->get_type();
      }
    };

    void process();
    nlohmann::json &get_result();
  };

  /**
     A class describing a standard client.

     This class should be used for instantiating new clients. See the main page
     for learning how the server, clients, and subclients work.
  */
  class StdClient : public InitClient {
  private:
    unsigned int accepted;
    std::mutex accepted_mutex;
    std::condition_variable accepted_cond;
    bool profile_start;
    unsigned long long profile_start_tstamp;

    StdClient(std::shared_ptr<Subclient::Factory> &subclient_factory,
              std::unique_ptr<Connection> &connection,
              std::unique_ptr<Acceptor> &file_acceptor,
              unsigned long long file_timeout_seconds);

  public:
    /**
       A StdClient factory.
    */
    class Factory : public Client::Factory {
    private:
      std::shared_ptr<Subclient::Factory> factory;

    public:
      /**
         Constructs a StdClient::Factory object.

         @param factory A Subclient factory for spawning new
                        subclients by the client.
      */
      Factory(std::unique_ptr<Subclient::Factory> &factory) {
        this->factory = std::move(factory);
      }

      std::unique_ptr<Client> make_client(std::unique_ptr<Connection> &connection,
                                          std::unique_ptr<Acceptor> &file_acceptor,
                                          unsigned long long file_timeout_seconds) {
        return std::unique_ptr<
          StdClient>(new StdClient(this->factory,
                                   connection,
                                   file_acceptor,
                                   file_timeout_seconds));
      }
    };

    void process(fs::path working_dir);
    void notify();
    bool get_profile_start_tstamp(unsigned long long *tstamp);
  };

  /**
     A class describing the server.

     See the main page for learning how the server, clients, and subclients work.
  */
  class Server {
  private:
    std::unique_ptr<Acceptor> acceptor;
    unsigned int max_connections;
    unsigned int buf_size;
    unsigned long long file_timeout_seconds;
    bool interrupted;

  public:
    Server(std::unique_ptr<Acceptor> &acceptor,
           unsigned int max_connections,
           unsigned int buf_size,
           unsigned long long file_timeout_seconds);
    void run(std::unique_ptr<Client::Factory> &client_factory,
             std::unique_ptr<Acceptor::Factory> &file_acceptor_factory);
    void interrupt();
  };
};

#endif
