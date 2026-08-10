// SPDX-FileCopyrightText: 2026 CERN
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef SYSTEM_HPP_
#define SYSTEM_HPP_

#include <string>
#include <memory>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <filesystem>
#include <future>
#include "ir.hpp"
#include "adaptyst/output.hpp"
#include "adaptyst/process.hpp"

namespace adaptyst {
  namespace fs = std::filesystem;

  class Identifiable {
  private:
    std::string name;
    Identifiable *parent;
    std::unordered_map<fs::path, fs::path> paths;

  protected:
    Identifiable(std::string name) {
      this->name = name;
      this->parent = nullptr;
    }

    inline void throw_error(std::string msg) {
      throw std::runtime_error(name + ": " + msg);
    }

  public:
    std::string &get_name() {
      return this->name;
    }

    void set_parent(Identifiable *identifiable) {
      this->parent = identifiable;
    }

    std::string get_parent_name() {
      if (this->parent) {
        return this->parent->get_name();
      } else {
        return "(N/A)";
      }
    }

    fs::path &get_path(fs::path start) {
      if (this->paths.find(start) == this->paths.end()) {
        std::vector<std::string> chain;

        chain.push_back(this->name);

        Identifiable *current = this->parent;

        while (current) {
          chain.push_back(current->name);
          current = current->parent;
        }

        fs::path result = start;

        for (int i = chain.size() - 1; i >= 0; i--) {
          result /= chain[i];
        }

        this->paths[start] = result;
      }

      return this->paths[start];
    }

    virtual std::vector<std::string> get_log_types() = 0;
    virtual std::string get_type() = 0;
  };

  class Node;
  class Entity;
  class System;

  typedef struct InjectPath {
    std::string name;
    amod_t id;
    int read_fd[2];
    int write_fd[2];
    std::string path;
  } InjectPath;

  typedef std::tuple<std::string,
                     std::string,
                     std::string,
                     std::string> ApiTimestamp;

  class Module : public Identifiable {
  public:
    static std::unordered_map<amod_t, Module *> all_modules;
    static amod_t next_module_id;

    struct OptionMetadata {
      std::string help;
      option_type type;
      option_type array_type;
      void *default_value;
      void *default_array_value;
      unsigned int default_array_value_size;
    };

    static std::vector<std::unique_ptr<Module> > get_all_modules(std::vector<fs::path> &library_paths);

    Module(std::string backend_name,
           std::vector<fs::path> &library_paths);
    Module(std::string backend_name,
           std::unordered_map<std::string, std::string> &options,
           std::unordered_map<std::string, std::vector<std::string>> &array_options,
           std::vector<fs::path> &library_paths,
           bool never_directing,
           bool no_inject);
    ~Module();
    bool init(unsigned int buf_size);
    void process(std::shared_ptr<IR> ir_obj);
    bool wait();
    void close();
    void set_will_profile(bool will_profile);
    bool get_will_profile();
    void set_error(std::string error);
    std::unordered_map<std::string, option> &get_options();
    std::unique_ptr<Path> &get_dir();
    std::unordered_set<std::string> &get_tags();
    std::unordered_map<std::string, OptionMetadata> &get_all_options();
    void set_dir(fs::path dir);
    void profile_notify();
    int profile_wait();
    std::vector<std::string> get_log_types();
    std::string get_type();
    std::string &get_node_name();
    void set_api_error(std::string msg, int code);
    int get_api_error_code();
    std::string &get_api_error_msg();
    bool is_directing_node();
    void add_src_code_path(fs::path path);
    void set_node(Node *node);
    fs::path &get_tmp_dir();
    fs::path &get_local_config_dir();
    bool has_in_tag(std::string tag);
    bool has_out_tag(std::string tag);
    profile_info &get_profile_info();
    void set_profile_info(profile_info info);
    bool is_initialising();
    const char *get_cpu_mask();
    std::unordered_set<fs::path> &get_src_code_paths();
    std::string get_name();
    std::string get_version();
    std::vector<int> get_version_nums();
    fs::path &get_lib_path();
    unsigned int get_max_count_per_entity();
    bool is_injection_available();
    fs::path get_inject_lib_path();
    amod_t get_id();
    std::shared_ptr<FileDescriptor> get_fd();
    std::string &receive_string_inject(long timeout_seconds = NO_TIMEOUT);
    bool is_workflow_running();
    bool is_workflow_ever_run();
    unsigned long long get_workflow_start_time(bool &err);
    unsigned long long get_workflow_end_time(bool &err);
    void region_switch(std::string name, std::string part_id,
                       std::string state, std::string timestamp_str);

  private:
    amod_t id;
    std::unordered_map<std::string, option> options;
    std::unordered_map<std::string, OptionMetadata> option_metadata;
    std::unique_ptr<Path> dir;
    bool will_profile;
    std::string error;
    void *context;
    void *handle;
    fs::path inject_lib_path;
    bool injecting_process;
    bool injection_available;
    std::future<bool> process_future;
    std::vector<std::string> log_types;
    std::vector<void *> malloced;
    std::unordered_set<std::string> tags;
    Node *node;
    bool initialised;
    bool never_directing;
    int api_error_code;
    std::string api_error_msg;
    bool initialising;
    std::unordered_set<fs::path> src_code_paths;
    fs::path lib_path;
    unsigned int max_count_per_entity;
    int read_fd[2];
    int write_fd[2];
    std::shared_ptr<FileDescriptor> fd;
    std::string last_received_message_inject;

    void construct(std::string backend_name,
                   std::unordered_map<std::string, std::string> &options,
                   std::unordered_map<std::string, std::vector<std::string>> &array_options,
                   std::vector<fs::path> &library_paths, bool never_directing, bool no_inject);
  };

  class Node : public Identifiable {
  public:
    Node(std::string name,
         std::shared_ptr<Entity> &entity);
    bool init(unsigned int buf_size);
    void process(std::shared_ptr<IR> ir_obj);
    bool wait();
    void close();
    std::unordered_set<std::string> &get_tags();
    void add_in_tags(std::unordered_set<std::string> &tags);
    void add_out_tags(std::unordered_set<std::string> &tags);
    bool has_in_tag(std::string tag);
    bool has_out_tag(std::string tag);
    void add_module(std::unique_ptr<Module> &mod);
    void profile_notify();
    int profile_wait();
    int get_modules_profiling();
    void inc_modules_profiling();
    void set_dir(fs::path path);
    std::vector<std::string> get_log_types();
    std::string get_type();
    bool is_directing();
    profile_info &get_profile_info();
    void set_profile_info(profile_info info);
    const char *get_cpu_mask();
    fs::path &get_tmp_dir();
    fs::path &get_local_config_dir();
    std::unordered_set<fs::path> get_src_code_paths();
    std::vector<InjectPath> get_module_inject_paths();
    bool is_workflow_running();
    bool is_workflow_ever_run();
    unsigned long long get_workflow_start_time(bool &err);
    unsigned long long get_workflow_end_time(bool &err);
    void region_switch(std::string name, std::string part_id,
                       std::string state, std::string timestamp_str);

  private:
    std::unique_ptr<Path> dir;
    std::vector<std::unique_ptr<Module> > modules;
    std::shared_ptr<Entity> entity;
    std::unordered_set<std::string> tags;
    std::unordered_set<std::string> in_tags;
    std::unordered_set<std::string> out_tags;
    int modules_profiling;
    std::mutex modules_profiling_mutex;
  };

  class NodeConnection : public Identifiable {
  private:
    std::shared_ptr<Node> departure_node;
    std::shared_ptr<Node> arrival_node;

  public:
    NodeConnection(std::string id,
                   std::shared_ptr<Node> &departure_node,
                   std::shared_ptr<Node> &arrival_node);
    std::shared_ptr<Node> &get_departure_node();
    std::shared_ptr<Node> &get_arrival_node();
    std::vector<std::string> get_log_types();
    std::string get_type();
  };

  class Entity : public Identifiable {
  public:
    enum AccessMode {
      LOCAL,
      REMOTE,
      CUSTOM,
      CUSTOM_REMOTE
    };

    Entity(std::string id, AccessMode access_mode,
           unsigned int processing_threads,
           fs::path local_config_path,
           fs::path tmp_dir, bool no_inject,
           unsigned int buf_size);
    void add_node(std::shared_ptr<Node> &node);
    void add_connection(std::string id,
                        std::string departure_node,
                        std::string arrival_node);
    std::shared_ptr<Node> &get_node(std::string id);
    void set_directing_node(std::string node);
    std::string get_directing_node();
    profile_info &get_profile_info();
    void set_profile_info(profile_info &info);
    void init();
    void process(bool save_src_code_paths);
    void close();
    void set_entity_dir(fs::path &entity_dir);
    void profile_notify();
    int profile_wait();
    const char *get_cpu_mask();
    fs::path &get_tmp_dir();
    fs::path &get_local_config_dir();
    std::vector<std::shared_ptr<Node> > get_all_nodes();
    std::vector<std::string> get_log_types();
    std::string get_type();
    void set_ir(std::shared_ptr<IR> ir_obj);
    std::unordered_set<fs::path> &get_src_code_paths();
    bool is_workflow_running();
    bool is_workflow_ever_run();
    unsigned long long get_workflow_start_time(bool &err);
    unsigned long long get_workflow_end_time(bool &err);

  private:
    AccessMode access_mode;
    std::unordered_map<std::string, std::shared_ptr<Node> > nodes;
    std::unordered_map<std::string, std::shared_ptr<NodeConnection> > connections;
    std::string directing_node;
    profile_info profiling_info;
    std::unique_ptr<Path> entity_dir;
    unsigned int processing_threads;
    fs::path local_config_path;
    fs::path tmp_dir;
    std::string cpu_mask;
    std::shared_ptr<IR> ir_obj;
    std::unique_ptr<Process> profiled_process;
    std::unordered_set<fs::path> src_code_paths;
    bool src_code_paths_collected;
    bool workflow_finish_printed;
    unsigned long long workflow_start_time;
    bool workflow_start_time_set;
    std::mutex workflow_finish_print_mutex;
    std::mutex profile_notify_mutex;
    int modules_notified;
    int modules_profiling;
    bool no_inject;
    unsigned int buf_size;
    std::future<void> workflow_comm;
    unsigned long long workflow_timestamp;
    bool workflow_timestamp_error;
    unsigned long long workflow_end_timestamp;
    bool workflow_end_timestamp_error;
    bool process_notified;
    fs::path workflow_stdout_path;
    fs::path workflow_stderr_path;
    std::mutex profile_wait_mutex;
    bool process_finished;
    int process_exit_code;
    std::vector<ApiTimestamp> api_timestamps;
  };

  class System {
  private:
    std::unordered_map<std::string, std::shared_ptr<Entity> > entities;
    std::unordered_map<std::string,
                       std::shared_ptr<NodeConnection> > connections;
    std::unique_ptr<Path> root_dir;
    std::variant<fs::path, int> codes_dst;
    bool custom_src_code_paths_save;

    void init(fs::path def_file, fs::path root_dir,
              std::vector<fs::path> &library_paths, fs::path local_config_path,
              fs::path tmp_dir, bool no_inject, unsigned int buf_size);
  public:
    System(fs::path def_file, fs::path root_dir,
           std::vector<fs::path> &library_paths, fs::path local_config_path,
           fs::path tmp_dir, bool no_inject, unsigned int buf_size);
    System(fs::path def_file, fs::path root_dir,
           std::vector<fs::path> &library_paths, fs::path local_config_path,
           fs::path tmp_dir, bool no_inject, unsigned int buf_size,
           std::variant<fs::path, int> codes_dst);
    ~System();
    void set_ir(std::shared_ptr<IR> ir_obj);
    void process();
    bool with_custom_src_code_paths();
  };
};

#endif
