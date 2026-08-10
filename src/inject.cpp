// SPDX-FileCopyrightText: 2026 CERN
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <dlfcn.h>
#include <adaptyst/inject.h>
#include <adaptyst/hw_inject.h>
#include <adaptyst/socket.hpp>
#include <time.h>
#include <iostream>
#include <mutex>

extern "C" {
  static char *error_msg = NULL;
}

namespace adaptyst {
  class Injection {
  private:
    struct ModuleHandle {
      std::string name;
      amod_t id;
      std::unique_ptr<FileDescriptor> fd;
      void *handle;
    };

    int *read_fd;
    int *write_fd;
    unsigned int buf_size;
    std::unordered_map<amod_t, struct ModuleHandle> handles;
    std::string error_message;
    int status;
    std::unique_ptr<FileDescriptor> fd;
    std::unordered_map<std::string, std::unordered_set<std::string> > regions;
    std::string module_error;
    std::string last_received_message;

  public:
    Injection(int *read_fd,
              int *write_fd,
              unsigned int buf_size) {
      this->read_fd = read_fd;
      this->write_fd = write_fd;
      this->buf_size = buf_size;
      this->status = ADAPTYST_INJECT_OK;
      this->error_message = "";
      this->module_error = "";

      this->fd = std::make_unique<FileDescriptor>(this->write_fd,
                                                  this->read_fd,
                                                  this->buf_size,
                                                  false);

      this->fd->write("init", true);
      std::string answer = this->fd->read();

      if (answer != "ack") {
        this->status = ADAPTYST_INJECT_ERR_INVALID_REPLY;
        return;
      }

      while ((answer = this->fd->read()) != "<STOP>") {
        std::unique_ptr<char> stream_buffer(new char[answer.length() + 1]);
        bool name_extracted = false;
        bool id_extracted = false;
        bool read_fd_extracted[2] = {false, false};
        bool write_fd_extracted[2] = {false, false};

        std::string name = "";
        amod_t id;
        std::string path = "";
        int read_fd[2];
        int write_fd[2];
        int index = 0;

        for (int i = 0; i < answer.length(); i++) {
          if (answer[i] == ' ') {
            if (!name_extracted) {
              stream_buffer.get()[index++] = 0;
              name = std::string(stream_buffer.get());
              index = 0;
              name_extracted = true;
              continue;
            } else if (!id_extracted) {
              stream_buffer.get()[index++] = 0;
              id = std::stoul(std::string(stream_buffer.get()));
              index = 0;
              id_extracted = true;
              continue;
            } else if (!read_fd_extracted[0]) {
              stream_buffer.get()[index++] = 0;
              read_fd[0] = std::stoi(std::string(stream_buffer.get()));
              index = 0;
              read_fd_extracted[0] = true;
              continue;
            } else if (!read_fd_extracted[1]) {
              stream_buffer.get()[index++] = 0;
              read_fd[1] = std::stoi(std::string(stream_buffer.get()));
              index = 0;
              read_fd_extracted[1] = true;
              continue;
            } else if (!write_fd_extracted[0]) {
              stream_buffer.get()[index++] = 0;
              write_fd[0] = std::stoi(std::string(stream_buffer.get()));
              index = 0;
              write_fd_extracted[0] = true;
              continue;
            } else if (!write_fd_extracted[1]) {
              stream_buffer.get()[index++] = 0;
              write_fd[1] = std::stoi(std::string(stream_buffer.get()));
              index = 0;
              write_fd_extracted[1] = true;
              continue;
            }
          }

          stream_buffer.get()[index++] = answer[i];
        }

        if (!name_extracted || !id_extracted ||
            !read_fd_extracted[0] || !read_fd_extracted[1] ||
            !write_fd_extracted[0] || !write_fd_extracted[1]) {
          this->error_message += "\nInvalid reply from Adaptyst when "
            "processing module list";
          this->status = ADAPTYST_INJECT_WARN_NOT_ALL_MODULES_SUCCEEDED;
          continue;
        }

        stream_buffer.get()[index++] = 0;
        path = std::string(stream_buffer.get());

        void *handle = dlopen(path.c_str(), RTLD_LAZY);

        if (!handle) {
          this->error_message += "\n" + name + ": " + std::string(dlerror());
          this->status = ADAPTYST_INJECT_WARN_NOT_ALL_MODULES_SUCCEEDED;
          continue;
        }

        struct ModuleHandle mod_handle;
        mod_handle.name = name;
        mod_handle.id = id;
        mod_handle.fd = std::make_unique<FileDescriptor>(write_fd, read_fd, this->buf_size,
                                                         false);
        mod_handle.handle = handle;

        this->handles[id] = std::move(mod_handle);
      }

      if (!this->error_message.empty()) {
        this->error_message = this->error_message.substr(1);
        error_msg = (char *)this->error_message.c_str();
      }
    }

    ~Injection() {
      for (auto &entry : this->handles) {
        void (*close)(amod_t) = (void (*)(amod_t))dlsym(entry.second.handle,
                                                        "adaptyst_close");

        if (close) {
          close(entry.second.id);
        }

        dlclose(entry.second.handle);
      }
    }

    void init() {
      std::vector<amod_t> to_remove;
      for (auto &entry : this->handles) {
        int (*init)(amod_t) = (int (*)(amod_t))dlsym(entry.second.handle, "adaptyst_init");

        if (!init) {
          this->error_message += "\n" + entry.second.name + ": Could not find adaptyst_init()";
          this->status = ADAPTYST_INJECT_WARN_NOT_ALL_MODULES_SUCCEEDED;
          to_remove.push_back(entry.first);
          continue;
        }

        this->module_error = "";
        int result = init(entry.second.id);

        if (result != ADAPTYST_MODULE_OK) {
          this->error_message += "\n" + entry.second.name + ": adaptyst_init() returned " +
            std::to_string(result);

          if (!this->module_error.empty()) {
            this->error_message += ", message: " + this->module_error;
          }

          this->status = ADAPTYST_INJECT_WARN_NOT_ALL_MODULES_SUCCEEDED;
          to_remove.push_back(entry.first);
          continue;
        }
      }

      if (!this->error_message.empty()) {
        this->error_message = this->error_message.substr(1);
        error_msg = (char *)this->error_message.c_str();
      }

      for (auto &id : to_remove) {
        dlclose(this->handles[id].handle);
        this->handles.erase(id);
      }
    }

    void set_module_error(std::string error) {
      this->module_error = error;
    }

    int get_status() {
      return this->status;
    }

    int region_switch(std::string name,
                      std::string state) {
      if (state != "start" && state != "end") {
        return ADAPTYST_INJECT_ERR_INVALID_REGION_STATE;
      }

      if (state == "start" && this->regions.contains(name)) {
        return ADAPTYST_INJECT_ERR_REGION_ALREADY_STARTED;
      }

      std::string part_id = std::to_string(getpid()) + "_" + std::to_string(gettid());

      if (state == "end") {
        if (!this->regions.contains(name)) {
          return ADAPTYST_INJECT_ERR_REGION_NOT_FOUND;
        }

        if (!this->regions[name].contains(part_id)) {
          return ADAPTYST_INJECT_ERR_REGION_IN_DIFFERENT_UNIT;
        }
      }

      int error;
      unsigned long long timestamp = adaptyst_get_timestamp(&error);
      std::string timestamp_str;

      if (error) {
        timestamp_str = "-1";
      } else {
        timestamp_str = std::to_string(timestamp);
      }

      this->fd->write(state + " " + part_id + " " + timestamp_str + " " + name, true);
      std::string answer = this->fd->read();

      if (answer != "ack") {
        return ADAPTYST_INJECT_ERR_INVALID_REPLY;
      }

      int to_return = ADAPTYST_INJECT_OK;
      this->error_message = "";
      error_msg = NULL;

      for (auto &entry : this->handles) {
        int (*module_switch)(amod_t, const char *, const char *, const char *) =
          (int (*)(amod_t, const char *, const char *, const char *))
          dlsym(entry.second.handle, ("adaptyst_region_" + state).c_str());

        if (!module_switch) {
          to_return = ADAPTYST_INJECT_WARN_NOT_ALL_MODULES_SUCCEEDED;
          this->error_message += "\n" + entry.second.name + ": adaptyst_region_" + state + "() not found";
          continue;
        }

        this->module_error = "";
        int result = module_switch(entry.second.id, part_id.c_str(),
                                   name.c_str(), timestamp_str.c_str());

        if (result != ADAPTYST_MODULE_OK) {
          to_return = ADAPTYST_INJECT_WARN_NOT_ALL_MODULES_SUCCEEDED;
          this->error_message += "\n" + entry.second.name + ": adaptyst_region_ " + state + "() "
            "returned " + std::to_string(result);

          if (!this->module_error.empty()) {
            this->error_message += ", message: " + this->module_error;
          }
        }
      }

      if (!this->error_message.empty()) {
        this->error_message = this->error_message.substr(1);
        error_msg = (char *)this->error_message.c_str();
      }

      if (this->regions.find(name) == this->regions.end()) {
        this->regions[name] = std::unordered_set<std::string>();
      }

      if (state == "start") {
        this->regions[name].insert(part_id);
      } else {
        this->regions[name].erase(part_id);

        if (this->regions[name].empty()) {
          this->regions.erase(name);
        }
      }

      return to_return;
    }

    void send_msg(amod_t id, char *buf, unsigned int n) {
      this->handles[id].fd->write(n, buf);
    }

    void send_msg(amod_t id, const char *msg) {
      this->handles[id].fd->write(std::string(msg), true);
    }

    void receive_msg(amod_t id, char *buf, unsigned int buf_status,
                     int *n, long timeout_seconds = NO_TIMEOUT) {
      *n = this->handles[id].fd->read(buf, buf_status, timeout_seconds);
    }

    std::string &receive_msg(amod_t id, long timeout_seconds = NO_TIMEOUT) {
      this->last_received_message = this->handles[id].fd->read(timeout_seconds);
      return this->last_received_message;
    }

    void send_timestamp_info(unsigned long long timestamp_start,
                             bool timestamp_start_valid,
                             unsigned long long timestamp_end,
                             bool timestamp_end_valid,
                             std::string func_name) {
      try {
        std::string part_id = std::to_string(getpid()) + "_" + std::to_string(gettid());
        this->fd->write("time " + part_id + " " + func_name + " " +
                        (timestamp_start_valid ? std::to_string(timestamp_start) : "-1") + " " +
                        (timestamp_end_valid ? std::to_string(timestamp_end) : "-1"), true);
      } catch (std::exception &e) {
        std::cerr << "Exception related to sending timestamp information has occurred: ";
        std::cerr << e.what() << std::endl;
      }
    }
  };
};

static std::unique_ptr<adaptyst::Injection> instance;
static unsigned int print_errors = 1;
static std::mutex inject_mutex;

extern "C" {
  static const char *result[] = {
    "ADAPTYST_READ_FD1", getenv("ADAPTYST_READ_FD1"),
    "ADAPTYST_READ_FD2", getenv("ADAPTYST_READ_FD2"),
    "ADAPTYST_WRITE_FD1", getenv("ADAPTYST_WRITE_FD1"),
    "ADAPTYST_WRITE_FD2", getenv("ADAPTYST_WRITE_FD2") };

  int handle_error_if_any(unsigned long long timestamp_start,
                          bool timestamp_start_valid,
                          int code, const char *type) {
    if (code != ADAPTYST_INJECT_OK && print_errors > 0) {
      std::cerr << "[Adaptyst, " << type << "] ";

      switch (code) {
      case ADAPTYST_INJECT_ERR_MISSING_RUNTIME_INFO:
        std::cerr << "Runtime information such as the Adaptyst env variables ";
        std::cerr << "is missing";
        break;

      case ADAPTYST_INJECT_ERR_INVALID_RUNTIME_INFO:
        std::cerr << "Runtime information such as the Adaptyst env variables ";
        std::cerr << "is invalid";
        break;

      case ADAPTYST_INJECT_EXCEPTION:
        std::cerr << "Exception has occurred: ";
        std::cerr << adaptyst_get_error_msg();
        break;

      case ADAPTYST_INJECT_ERR_INVALID_REPLY:
        std::cerr << "Invalid reply from Adaptyst has been received by ";
        std::cerr << "the workflow";
        break;

      case ADAPTYST_INJECT_ERR_NOT_INITIALISED:
        std::cerr << "The Adaptyst injection resources are not initialised";
        break;

      case ADAPTYST_INJECT_WARN_NOT_ALL_MODULES_SUCCEEDED:
        std::cerr << "Warning: Not all modules have succeeded in processing. ";
        std::cerr << "Details: " << adaptyst_get_error_msg();
        break;

      case ADAPTYST_INJECT_ERR_TIMEOUT:
        std::cerr << "Connection timeout between the workflow and Adaptyst";
        break;

      case ADAPTYST_INJECT_ERR_REGION_NOT_FOUND:
        std::cerr << "Region not found (has it been started?)";
        break;

      case ADAPTYST_INJECT_ERR_REGION_ALREADY_STARTED:
        std::cerr << "Region already started (you need to end it first)";
        break;

      case ADAPTYST_INJECT_ERR_INVALID_REGION_STATE:
        std::cerr << "Invalid new region state (it can be either \"start\" ";
        std::cerr << "or \"end\")";
        break;

      case ADAPTYST_INJECT_ERR_REGION_IN_DIFFERENT_UNIT:
        std::cerr << "Region can be ended only in the same threads/processes ";
        std::cerr << "where it is active";
        break;

      default:
        std::cerr << "Code " << code;
        break;
      }

      std::cerr << std::endl;
    }

    if (instance) {
      int err;
      unsigned long long timestamp_end = adaptyst_get_timestamp(&err);
      bool timestamp_end_valid = err == 0;

      instance->send_timestamp_info(timestamp_start,
                                    timestamp_start_valid,
                                    timestamp_end,
                                    timestamp_end_valid,
                                    std::string(type));
    }

    return code;
  }

  int _adaptyst_init_custom_buf_size(unsigned int size) {
    const char **runtime_info = adaptyst_get_runtime_info();

    if (!runtime_info[1] || !runtime_info[3]
        || !runtime_info[5] || !runtime_info[7]) {
      return ADAPTYST_INJECT_ERR_MISSING_RUNTIME_INFO;
    }

    int read_fd[] = {0, 0};

    try {
      read_fd[0] = std::stoi(std::string(runtime_info[1]));
      read_fd[1] = std::stoi(std::string(runtime_info[3]));
    } catch (std::exception) {
      return ADAPTYST_INJECT_ERR_INVALID_RUNTIME_INFO;
    }

    int write_fd[] = {0, 0};

    try {
      write_fd[0] = std::stoi(std::string(runtime_info[5]));
      write_fd[1] = std::stoi(std::string(runtime_info[7]));
    } catch (std::exception) {
      return ADAPTYST_INJECT_ERR_INVALID_RUNTIME_INFO;
    }

    try {
      instance = std::make_unique<adaptyst::Injection>(read_fd, write_fd, size);

      if (instance->get_status() == ADAPTYST_INJECT_OK) {
        instance->init();
      }

      return instance->get_status();
    } catch (std::exception &e) {
      error_msg = (char *)e.what();
      return ADAPTYST_INJECT_EXCEPTION;
    }
  }

  int _adaptyst_init() {
    return _adaptyst_init_custom_buf_size(1024);
  }

  const char **adaptyst_get_runtime_info() {
    return result;
  }

  char *adaptyst_get_error_msg() {
    return error_msg;
  }

  int _adaptyst_region_start(const char *name) {
    if (!instance) {
      int result = _adaptyst_init();

      if (result != ADAPTYST_INJECT_OK) {
        return result;
      }
    }

    try {
      return instance->region_switch(std::string(name),
                                     "start");
    } catch (std::exception &e) {
      error_msg = (char *)e.what();
      return ADAPTYST_INJECT_EXCEPTION;
    }
  }

  int _adaptyst_region_end(const char *name) {
    if (!instance) {
      int result = _adaptyst_init();

      if (result != ADAPTYST_INJECT_OK) {
        return result;
      }
    }

    try {
      return instance->region_switch(std::string(name),
                                     "end");
    } catch (std::exception &e) {
      error_msg = (char *)e.what();
      return ADAPTYST_INJECT_EXCEPTION;
    }
  }

  void adaptyst_close() {
    int err;
    unsigned long long ts_s = adaptyst_get_timestamp(&err);
    bool ts_s_v = err == 0;

    {
      std::unique_lock lock(inject_mutex);
      if (instance) {
        unsigned long long ts_e = adaptyst_get_timestamp(&err);
        bool ts_e_v = err == 0;

        instance->send_timestamp_info(ts_s, ts_s_v,
                                      ts_e, ts_e_v, "close");

        instance.reset();
      }
    }
  }

  int _adaptyst_send_data(amod_t id, char *buf, unsigned int n) {
    if (!instance) {
      int result = _adaptyst_init();

      if (result != ADAPTYST_INJECT_OK) {
        return result;
      }
    }

    try {
      instance->send_msg(id, buf, n);
      return ADAPTYST_INJECT_OK;
    } catch (std::exception &e) {
      error_msg = (char *)e.what();
      return ADAPTYST_INJECT_EXCEPTION;
    }
  }

  int _adaptyst_receive_data(amod_t id, char *buf, unsigned int buf_size,
                             int *n) {
    if (!instance) {
      int result = _adaptyst_init();

      if (result != ADAPTYST_INJECT_OK) {
        return result;
      }
    }

    try {
      instance->receive_msg(id, buf, buf_size, n);
      return ADAPTYST_INJECT_OK;
    } catch (std::exception &e) {
      error_msg = (char *)e.what();
      return ADAPTYST_INJECT_EXCEPTION;
    }
  }

  int _adaptyst_receive_data_timeout(amod_t id, char *buf, unsigned int buf_size,
                                     int *n, long timeout_seconds) {
    if (!instance) {
      int result = _adaptyst_init();

      if (result != ADAPTYST_INJECT_OK) {
        return result;
      }
    }

    try {
      instance->receive_msg(id, buf, buf_size, n, timeout_seconds);
      return ADAPTYST_INJECT_OK;
    } catch (adaptyst::TimeoutException) {
      return ADAPTYST_INJECT_ERR_TIMEOUT;
    } catch (std::exception &e) {
      error_msg = (char *)e.what();
      return ADAPTYST_INJECT_EXCEPTION;
    }
  }

  int _adaptyst_send_string(amod_t id, const char *str) {
    if (!instance) {
      int result = _adaptyst_init();

      if (result != ADAPTYST_INJECT_OK) {
        return result;
      }
    }

    try {
      instance->send_msg(id, str);
      return ADAPTYST_INJECT_OK;
    } catch (std::exception &e) {
      error_msg = (char *)e.what();
      return ADAPTYST_INJECT_EXCEPTION;
    }
  }

  int _adaptyst_receive_string(amod_t id, const char **str) {
    if (!instance) {
      int result = _adaptyst_init();

      if (result != ADAPTYST_INJECT_OK) {
        return result;
      }
    }

    try {
      std::string &received = instance->receive_msg(id);

      if (received.empty()) {
        *str = NULL;
      } else {
        *str = received.c_str();
      }

      return ADAPTYST_INJECT_OK;
    } catch (std::exception &e) {
      error_msg = (char *)e.what();
      return ADAPTYST_INJECT_EXCEPTION;
    }
  }

  int _adaptyst_receive_string_timeout(amod_t id, const char **str,
                                       long timeout_seconds) {
    if (!instance) {
      int result = _adaptyst_init();

      if (result != ADAPTYST_INJECT_OK) {
        return result;
      }
    }

    try {
      std::string &received = instance->receive_msg(id, timeout_seconds);

      if (received.empty()) {
        *str = NULL;
      } else {
        *str = received.c_str();
      }

      return ADAPTYST_INJECT_OK;
    } catch (adaptyst::TimeoutException) {
      return ADAPTYST_INJECT_ERR_TIMEOUT;
    } catch (std::exception &e) {
      error_msg = (char *)e.what();
      return ADAPTYST_INJECT_EXCEPTION;
    }
  }

  int adaptyst_send_data(amod_t id, char *buf, unsigned int n) {
    int err;
    unsigned long long ts_s = adaptyst_get_timestamp(&err);
    bool ts_s_v = err == 0;

    {
      std::unique_lock lock(inject_mutex);
      return handle_error_if_any(ts_s, ts_s_v,
                                 _adaptyst_send_data(id, buf, n),
                                 "send_data");
    }
  }

  int adaptyst_receive_data(amod_t id, char *buf, unsigned int buf_size,
                            int *n) {
    int err;
    unsigned long long ts_s = adaptyst_get_timestamp(&err);
    bool ts_s_v = err == 0;

    {
      std::unique_lock lock(inject_mutex);
      return handle_error_if_any(ts_s, ts_s_v, _adaptyst_receive_data(id, buf, buf_size, n),
                                 "receive_data");
    }
  }

  int adaptyst_receive_data_timeout(amod_t id, char *buf, unsigned int buf_size,
                                   int *n, long timeout_seconds) {
    int err;
    unsigned long long ts_s = adaptyst_get_timestamp(&err);
    bool ts_s_v = err == 0;

    {
      std::unique_lock lock(inject_mutex);
      return handle_error_if_any(ts_s, ts_s_v, _adaptyst_receive_data_timeout(id, buf, buf_size,
                                                                n, timeout_seconds),
                                 "receive_data_timeout");
    }
  }

  int adaptyst_send_string(amod_t id, const char *str) {
    int err;
    unsigned long long ts_s = adaptyst_get_timestamp(&err);
    bool ts_s_v = err == 0;

    {
      std::unique_lock lock(inject_mutex);
      return handle_error_if_any(ts_s, ts_s_v, _adaptyst_send_string(id, str),
                                 "send_string");
    }
  }

  int adaptyst_receive_string(amod_t id, const char **str) {
    int err;
    unsigned long long ts_s = adaptyst_get_timestamp(&err);
    bool ts_s_v = err == 0;

    {
      std::unique_lock lock(inject_mutex);
      return handle_error_if_any(ts_s, ts_s_v, _adaptyst_receive_string(id, str),
                                 "receive_string");
    }
  }

  int adaptyst_receive_string_timeout(amod_t id, const char **str,
                                      long timeout_seconds) {
    int err;
    unsigned long long ts_s = adaptyst_get_timestamp(&err);
    bool ts_s_v = err == 0;

    {
      std::unique_lock lock(inject_mutex);
      return handle_error_if_any(ts_s, ts_s_v, _adaptyst_receive_string_timeout(id, str, timeout_seconds),
                                 "receive_string_timeout");
    }
  }

  int adaptyst_send_data_nl(amod_t id, char *buf, unsigned int n) {
    int err;
    unsigned long long ts_s = adaptyst_get_timestamp(&err);
    bool ts_s_v = err == 0;

    return handle_error_if_any(ts_s, ts_s_v, _adaptyst_send_data(id, buf, n),
                               "send_data_nl");
  }

  int adaptyst_receive_data_nl(amod_t id, char *buf, unsigned int buf_size,
                               int *n) {
    int err;
    unsigned long long ts_s = adaptyst_get_timestamp(&err);
    bool ts_s_v = err == 0;

    return handle_error_if_any(ts_s, ts_s_v, _adaptyst_receive_data(id, buf, buf_size, n),
                               "receive_data_nl");
  }

  int adaptyst_receive_data_timeout_nl(amod_t id, char *buf, unsigned int buf_size,
                                       int *n, long timeout_seconds) {
    int err;
    unsigned long long ts_s = adaptyst_get_timestamp(&err);
    bool ts_s_v = err == 0;

    return handle_error_if_any(ts_s, ts_s_v, _adaptyst_receive_data_timeout(id, buf, buf_size,
                                                              n, timeout_seconds),
                               "receive_data_timeout");
  }

  int adaptyst_send_string_nl(amod_t id, const char *str) {
    int err;
    unsigned long long ts_s = adaptyst_get_timestamp(&err);
    bool ts_s_v = err == 0;

    return handle_error_if_any(ts_s, ts_s_v, _adaptyst_send_string(id, str),
                               "send_string_nl");
  }

  int adaptyst_receive_string_nl(amod_t id, const char **str) {
    int err;
    unsigned long long ts_s = adaptyst_get_timestamp(&err);
    bool ts_s_v = err == 0;

    return handle_error_if_any(ts_s, ts_s_v, _adaptyst_receive_string(id, str),
                               "receive_string_nl");
  }

  int adaptyst_receive_string_timeout_nl(amod_t id, const char **str,
                                         long timeout_seconds) {
    int err;
    unsigned long long ts_s = adaptyst_get_timestamp(&err);
    bool ts_s_v = err == 0;

    return handle_error_if_any(ts_s, ts_s_v, _adaptyst_receive_string_timeout(id, str,
                                                                timeout_seconds),
                               "receive_string_timeout_nl");
  }

  void adaptyst_set_print_errors(unsigned int print) {
    int err;
    unsigned long long ts_s = adaptyst_get_timestamp(&err);
    bool ts_s_v = err == 0;

    {
      std::unique_lock lock(inject_mutex);
      print_errors = print;

      if (instance) {
        unsigned long long ts_e = adaptyst_get_timestamp(&err);
        bool ts_e_v = err == 0;

        instance->send_timestamp_info(ts_s, ts_s_v,
                                      ts_e, ts_e_v,
                                      "set_print_errors");
      }
    }
  }

  int adaptyst_init() {
    int err;
    unsigned long long ts_s = adaptyst_get_timestamp(&err);
    bool ts_s_v = err == 0;

    {
      std::unique_lock lock(inject_mutex);
      return handle_error_if_any(ts_s, ts_s_v, _adaptyst_init(), "init");
    }
  }

  int adaptyst_init_custom_buf_size(unsigned int size) {
    int err;
    unsigned long long ts_s = adaptyst_get_timestamp(&err);
    bool ts_s_v = err == 0;

    {
      std::unique_lock lock(inject_mutex);
      return handle_error_if_any(ts_s, ts_s_v, _adaptyst_init_custom_buf_size(size),
                                 "init_custom_buf_size");
    }
  }

  int adaptyst_region_start(const char *name) {
    int err;
    unsigned long long ts_s = adaptyst_get_timestamp(&err);
    bool ts_s_v = err == 0;

    {
      std::unique_lock lock(inject_mutex);
      return handle_error_if_any(ts_s, ts_s_v, _adaptyst_region_start(name),
                                 "region_start");
    }
  }

  int adaptyst_region_end(const char *name) {
    int err;
    unsigned long long ts_s = adaptyst_get_timestamp(&err);
    bool ts_s_v = err == 0;

    {
      std::unique_lock lock(inject_mutex);
      return handle_error_if_any(ts_s, ts_s_v, _adaptyst_region_end(name),
                                 "region_end");
    }
  }

  void adaptyst_set_error(const char *msg) {
    int err;
    unsigned long long ts_s = adaptyst_get_timestamp(&err);
    bool ts_s_v = err == 0;

    {
      std::unique_lock lock(inject_mutex);
      if (instance) {
        instance->set_module_error(std::string(msg));

        unsigned long long ts_e = adaptyst_get_timestamp(&err);
        bool ts_e_v = err == 0;

        instance->send_timestamp_info(ts_s, ts_s_v,
                                      ts_e, ts_e_v, "set_error");
      }
    }
  }

  void adaptyst_set_error_nl(const char *msg) {
    int err;
    unsigned long long ts_s = adaptyst_get_timestamp(&err);
    bool ts_s_v = err == 0;

    if (instance) {
      instance->set_module_error(std::string(msg));

      unsigned long long ts_e = adaptyst_get_timestamp(&err);
      bool ts_e_v = err == 0;

      instance->send_timestamp_info(ts_s, ts_s_v,
                                    ts_e, ts_e_v, "set_error_nl");
    }
  }

  unsigned long long adaptyst_get_timestamp(int *err) {
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1) {
      *err = errno;
      return 0;
    }

    *err = 0;
    return ts.tv_sec * 1000000000ULL + ts.tv_nsec;
  }
}
