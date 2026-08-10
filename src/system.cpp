// SPDX-FileCopyrightText: 2026 CERN
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "system.hpp"
#include "print.hpp"
#include "archive.hpp"
#include "adaptyst/output.hpp"
#include <ryml.hpp>
#include <fstream>
#include <dlfcn.h>
#include <regex>
#include <time.h>

// The code segment below is for the C hardware module API.
inline void set_error(adaptyst::Module *mod, int code) {
  std::string msg;

  switch (code) {
  case ADAPTYST_ERR_MODULE_NOT_FOUND:
    msg = "Module not found";
    break;

  case ADAPTYST_ERR_OUT_OF_MEMORY:
    msg = "Out of memory";
    break;

  case ADAPTYST_ERR_EXCEPTION:
    msg = "Exception has occurred";
    break;

  case ADAPTYST_ERR_TERMINAL_NOT_INITIALISED:
    msg = "Terminal-related resources in Adaptyst haven't "
      "been initialised yet";
    break;

  case ADAPTYST_ERR_LOG_DIR_CREATE:
    msg = "Log directories couldn't be created";
    break;

  case ADAPTYST_ERR_INIT_ONLY:
    msg = "This API method can be called only inside "
      "adaptyst_module_init()";
    break;

  case ADAPTYST_ERR_TIMEOUT:
    msg = "Timeout";
    break;
  }

  mod->set_api_error(msg, code);
}

inline void set_error(adaptyst::Module *mod, std::string msg, int code) {
  mod->set_api_error(msg, code);
}

inline adaptyst::Module *get(amod_t id, bool reset_error = true) {
  if (adaptyst::Module::all_modules.find(id) ==
      adaptyst::Module::all_modules.end()) {
    return nullptr;
  }

  if (reset_error) {
    auto to_return = adaptyst::Module::all_modules[id];
    set_error(to_return, ADAPTYST_OK);
    return to_return;
  } else {
    return adaptyst::Module::all_modules[id];
  }
}

extern "C" {
  const char *adaptyst_get_library_dir(amod_t id) {
    auto mod = get(id);

    if (!mod) {
      return NULL;
    }

    return mod->get_lib_path().c_str();
  }

  option *adaptyst_get_option(amod_t id, const char *key) {
    auto mod = get(id);

    if (!mod) {
      return NULL;
    }

    auto &options = mod->get_options();
    std::string key_str(key);

    if (options.find(key_str) == options.end()) {
      return NULL;
    }

    return &options[key_str];
  }

  bool adaptyst_set_error(amod_t id, const char *msg) {
    auto mod = get(id);

    if (!mod) {
      return false;
    }

    mod->set_error(std::string(msg));
    return true;
  }

  const char *adaptyst_get_log_dir(amod_t id) {
    auto mod = get(id);

    if (!mod) {
      return NULL;
    }

    if (!adaptyst::Terminal::instance) {
      set_error(mod, ADAPTYST_ERR_TERMINAL_NOT_INITIALISED);
      return NULL;
    }

    std::filesystem::path &path =
      mod->get_path(adaptyst::Terminal::instance->get_log_dir());

    try {
      if (!std::filesystem::exists(path) &&
          !std::filesystem::create_directories(path)) {
        set_error(mod, ADAPTYST_ERR_LOG_DIR_CREATE);
        return NULL;
      }
    } catch (std::exception &e) {
      set_error(mod, std::string(e.what()), ADAPTYST_ERR_EXCEPTION);
      return NULL;
    }

    return path.c_str();
  }

  bool adaptyst_send_data(amod_t id, char *buf, unsigned int n) {
    auto mod = get(id);

    if (!mod) {
      return false;
    }

    try {
      mod->get_fd()->write(n, buf);
      return true;
    } catch (std::exception &e) {
      set_error(mod, std::string(e.what()), ADAPTYST_ERR_EXCEPTION);
      return false;
    }
  }

  bool adaptyst_receive_data(amod_t id, char *buf, unsigned int buf_size,
                             int *n) {
    auto mod = get(id);

    if (!mod) {
      return false;
    }

    try {
      *n = mod->get_fd()->read(buf, buf_size, NO_TIMEOUT);
      return true;
    } catch (std::exception &e) {
      set_error(mod, std::string(e.what()), ADAPTYST_ERR_EXCEPTION);
      return false;
    }
  }

  bool adaptyst_receive_data_timeout(amod_t id, char *buf, unsigned int buf_size,
                                     int *n, long timeout_seconds) {
    auto mod = get(id);

    if (!mod) {
      return false;
    }

    try {
      *n = mod->get_fd()->read(buf, buf_size, timeout_seconds);
      return true;
    } catch (adaptyst::TimeoutException &e) {
      set_error(mod, ADAPTYST_ERR_TIMEOUT);
      return false;
    } catch (std::exception &e) {
      set_error(mod, std::string(e.what()), ADAPTYST_ERR_EXCEPTION);
      return false;
    }
  }

  bool adaptyst_send_string(amod_t id, const char *str) {
    auto mod = get(id);

    if (!mod) {
      return false;
    }

    try {
      mod->get_fd()->write(std::string(str), true);
      return true;
    } catch (std::exception &e) {
      set_error(mod, std::string(e.what()), ADAPTYST_ERR_EXCEPTION);
      return false;
    }
  }

  bool adaptyst_receive_string(amod_t id, const char **str) {
    auto mod = get(id);

    if (!mod) {
      return false;
    }

    try {
      std::string &received = mod->receive_string_inject();

      if (received.empty()) {
        *str = NULL;
      } else {
        *str = received.c_str();
      }

      return true;
    } catch (std::exception &e) {
      set_error(mod, std::string(e.what()), ADAPTYST_ERR_EXCEPTION);
      return false;
    }
  }

  bool adaptyst_receive_string_timeout(amod_t id, const char **str,
                                       long timeout_seconds) {
    auto mod = get(id);

    if (!mod) {
      return false;
    }

    try {
      std::string &received = mod->receive_string_inject(timeout_seconds);

      if (received.empty()) {
        *str = NULL;
      } else {
        *str = received.c_str();
      }

      return true;
    } catch (adaptyst::TimeoutException &e) {
      set_error(mod, ADAPTYST_ERR_TIMEOUT);
      return false;
    } catch (std::exception &e) {
      set_error(mod, std::string(e.what()), ADAPTYST_ERR_EXCEPTION);
      return false;
    }
  }

  const char *adaptyst_get_node_name(amod_t id) {
    auto mod = get(id);

    if (!mod) {
      return NULL;
    }

    return mod->get_node_name().c_str();
  }

  bool adaptyst_log(amod_t id, const char *msg, const char *type) {
    auto mod = get(id);

    if (!mod) {
      return false;
    }

    if (!adaptyst::Terminal::instance) {
      set_error(mod, ADAPTYST_ERR_TERMINAL_NOT_INITIALISED);
      return false;
    }

    try {
      adaptyst::Terminal::instance->log(std::string(msg), mod,
                                        std::string(type));
    } catch (std::exception &e) {
      set_error(mod, std::string(e.what()), ADAPTYST_ERR_EXCEPTION);
      return false;
    }

    return true;
  }

  bool adaptyst_print(amod_t id, const char *msg, bool sub, bool error,
                      const char *type) {
    auto mod = get(id);

    if (!mod) {
      return false;
    }

    if (!adaptyst::Terminal::instance) {
      set_error(mod, ADAPTYST_ERR_TERMINAL_NOT_INITIALISED);
      return false;
    }

    try {
      adaptyst::Terminal::instance->print(std::string(msg), sub, error, mod,
                                          std::string(type));
    } catch (std::exception &e) {
      set_error(mod, std::string(e.what()), ADAPTYST_ERR_EXCEPTION);
      return false;
    }

    return true;
  }

  const char *adaptyst_get_module_dir(amod_t id) {
    auto mod = get(id);

    if (!mod) {
      return NULL;
    }

    try {
      return mod->get_dir()->get_path_name();
    } catch (std::exception &e) {
      set_error(mod, std::string(e.what()), ADAPTYST_ERR_EXCEPTION);
      return NULL;
    }
  }

  profile_info *adaptyst_get_profile_info(amod_t id) {
    auto mod = get(id);

    if (!mod) {
      return NULL;
    }

    if (!mod->get_will_profile()) {
      return NULL;
    }

    auto &profile_info = mod->get_profile_info();
    return &profile_info;
  }

  bool adaptyst_set_profile_info(amod_t id, profile_info *info) {
    auto mod = get(id);

    if (!mod) {
      return false;
    }

    if (!mod->is_initialising()) {
      set_error(mod, ADAPTYST_ERR_INIT_ONLY);
      return false;
    }

    mod->set_profile_info(*info);
    return true;
  }

  bool adaptyst_is_directing_node(amod_t id) {
    auto mod = get(id);

    if (!mod) {
      return false;
    }

    return mod->is_directing_node();
  }

  bool adaptyst_profile_notify(amod_t id) {
    auto mod = get(id);

    if (!mod) {
      return false;
    }

    try {
      mod->profile_notify();
    } catch (std::exception &e) {
      set_error(mod, std::string(e.what()), ADAPTYST_ERR_EXCEPTION);
      return false;
    }

    return true;
  }

  int adaptyst_profile_wait(amod_t id) {
    auto mod = get(id);

    if (!mod) {
      return -1;
    }

    try {
      return mod->profile_wait();
    } catch (std::exception &e) {
      set_error(mod, std::string(e.what()), ADAPTYST_ERR_EXCEPTION);
      return -1;
    }
  }

  bool adaptyst_process_src_paths(amod_t id, const char **paths, int n) {
    auto mod = get(id);

    if (!mod) {
      return false;
    }

    for (int i = 0; i < n; i++) {
      mod->add_src_code_path(paths[i]);
    }

    return true;
  }

  const char *adaptyst_get_cpu_mask(amod_t id) {
    auto mod = get(id);

    if (!mod) {
      return NULL;
    }

    if (!mod->is_initialising()) {
      set_error(mod, ADAPTYST_ERR_INIT_ONLY);
      return NULL;
    }

    try {
      return mod->get_cpu_mask();
    } catch (std::exception &e) {
      set_error(mod, std::string(e.what()), ADAPTYST_ERR_EXCEPTION);
      return NULL;
    }
  }

  const char *adaptyst_get_tmp_dir(amod_t id) {
    auto mod = get(id);

    if (!mod) {
      return NULL;
    }

    try {
      return mod->get_tmp_dir().c_str();
    } catch (std::exception &e) {
      set_error(mod, std::string(e.what()), ADAPTYST_ERR_EXCEPTION);
      return NULL;
    }
  }

  const char *adaptyst_get_local_config_dir(amod_t id) {
    auto mod = get(id);

    if (!mod) {
      return NULL;
    }

    try {
      return mod->get_local_config_dir().c_str();
    } catch (std::exception &e) {
      set_error(mod, std::string(e.what()), ADAPTYST_ERR_EXCEPTION);
      return NULL;
    }
  }

  bool adaptyst_set_will_profile(amod_t id, bool will_profile) {
    auto mod = get(id);

    if (!mod) {
      return false;
    }

    if (!mod->is_initialising()) {
      set_error(mod, ADAPTYST_ERR_INIT_ONLY);
      return false;
    }

    mod->set_will_profile(will_profile);
    return true;
  }

  bool adaptyst_has_in_tag(amod_t id, const char *tag) {
    auto mod = get(id);

    if (!mod) {
      return false;
    }

    try {
      return mod->has_in_tag(std::string(tag));
    } catch (std::exception &e) {
      set_error(mod, std::string(e.what()), ADAPTYST_ERR_EXCEPTION);
      return false;
    }
  }

  bool adaptyst_has_out_tag(amod_t id, const char *tag) {
    auto mod = get(id);

    if (!mod) {
      return false;
    }

    try {
      return mod->has_out_tag(std::string(tag));
    } catch (std::exception &e) {
      set_error(mod, std::string(e.what()), ADAPTYST_ERR_EXCEPTION);
      return false;
    }
  }

  const char *adaptyst_get_internal_error_msg(amod_t id) {
    auto mod = get(id, false);

    if (!mod) {
      return "Module not found";
    }

    return mod->get_api_error_msg().c_str();
  }

  int adaptyst_get_internal_error_code(amod_t id) {
    auto mod = get(id, false);

    if (!mod) {
      return ADAPTYST_ERR_MODULE_NOT_FOUND;
    }

    return mod->get_api_error_code();
  }

  unsigned long long adaptyst_get_timestamp(amod_t id) {
    auto mod = get(id);

    if (!mod) {
      return 0;
    }

    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1) {
      set_error(mod, ADAPTYST_ERR_TIMESTAMP);
      return 0;
    }

    return ts.tv_sec * 1000000000ULL + ts.tv_nsec;
  }

  unsigned long long adaptyst_get_workflow_start_time(amod_t id) {
    auto mod = get(id);

    if (!mod) {
      return 0;
    }

    if (!mod->is_workflow_ever_run()) {
      set_error(mod, ADAPTYST_ERR_WORKFLOW_NOT_STARTED);
      return 0;
    }

    bool err;
    unsigned long long timestamp = mod->get_workflow_start_time(err);

    if (err) {
      set_error(mod, ADAPTYST_ERR_TIMESTAMP);
      return 0;
    }

    return timestamp;
  }

  unsigned long long adaptyst_get_workflow_end_time(amod_t id) {
    auto mod = get(id);

    if (!mod) {
      return 0;
    }

    if (!mod->is_workflow_ever_run()) {
      set_error(mod, ADAPTYST_ERR_WORKFLOW_NOT_STARTED);
      return 0;
    }

    if (mod->is_workflow_running()) {
      set_error(mod, ADAPTYST_ERR_WORKFLOW_RUNNING);
      return 0;
    }

    bool err;
    unsigned long long timestamp = mod->get_workflow_end_time(err);

    if (err) {
      set_error(mod, ADAPTYST_ERR_TIMESTAMP);
      return 0;
    }

    return timestamp;
  }

  bool adaptyst_is_workflow_running(amod_t id) {
    auto mod = get(id);

    if (!mod) {
      return false;
    }

    return mod->is_workflow_running();
  }
}
// The C hardware module API code segment ends here.

namespace adaptyst {
  namespace ch = std::chrono;

  std::unordered_map<amod_t, Module *> Module::all_modules;
  amod_t Module::next_module_id = 1;

  std::vector<std::unique_ptr<Module> > Module::get_all_modules(std::vector<fs::path> &library_paths) {
    std::vector<std::unique_ptr<Module> > modules;
    std::unordered_set<std::string> module_names;
    for (fs::path &library_path : library_paths) {
      for (auto &entry : fs::directory_iterator(library_path)) {
        if (!entry.is_directory()) {
          continue;
        }

        fs::path mod_path = entry.path() / ("lib" + entry.path().filename().string() + ".so");

        if (!fs::exists(mod_path)) {
          continue;
        }

        if (module_names.contains(entry.path().filename().string())) {
          continue;
        }

        modules.push_back(std::make_unique<Module>(entry.path().filename().string(), library_paths));
        module_names.insert(entry.path().filename().string());
      }
    }

    std::sort(modules.begin(), modules.end(),
              [](const std::unique_ptr<Module> &a,
                 const std::unique_ptr<Module> &b) {
                return a->get_name() < b->get_name();
              });

    return modules;
  }

  std::string Module::get_name() {
    const char **name = (const char **)dlsym(this->handle, "name");

    if (!name) {
      this->throw_error("No name is defined");
    }

    if (!(*name)) {
      this->throw_error("The name is empty");
    }

    std::string name_str(*name);

    if (name_str.empty()) {
      this->throw_error("The name is empty");
    }

    return name_str;
  }

  std::string Module::get_version() {
    const char **version = (const char **)dlsym(this->handle, "version");

    if (!version) {
      this->throw_error("No version is defined");
    }

    if (!(*version)) {
      this->throw_error("The version is empty");
    }

    std::string version_str(*version);

    if (version_str.empty()) {
      this->throw_error("The version is empty");
    }

    return version_str;
  }

  std::vector<int> Module::get_version_nums() {
    int *version_nums = (int *)dlsym(this->handle, "version_nums");

    if (!version_nums) {
      this->throw_error("No version number array is defined");
    }

    std::vector<int> nums;

    for (int i = 0; version_nums[i] >= 0; i++) {
      nums.push_back(version_nums[i]);
    }

    if (nums.empty()) {
      this->throw_error("The array of version numbers is empty");
    }

    return nums;
  }

  fs::path &Module::get_lib_path() {
    return this->lib_path;
  }

  unsigned int Module::get_max_count_per_entity() {
    return this->max_count_per_entity;
  }

  Module::Module(std::string backend_name,
                 std::vector<fs::path> &library_paths) : Identifiable(backend_name) {
    std::unordered_map<std::string, std::string> options;
    std::unordered_map<std::string, std::vector<std::string>> array_options;
    this->construct(backend_name, options, array_options, library_paths, true, false);
  }

  Module::Module(std::string backend_name, std::unordered_map<std::string, std::string> &options,
                 std::unordered_map<std::string, std::vector<std::string>> &array_options,
                 std::vector<fs::path> &library_paths, bool never_directing,
                 bool no_inject) : Identifiable(backend_name) {
    this->construct(backend_name, options, array_options, library_paths, never_directing,
                    no_inject);
  }

  Module::~Module() {
    for (auto &allocated : this->malloced) {
      free(allocated);
    }

    dlclose(this->handle);
  }

  void Module::construct(std::string backend_name,
                         std::unordered_map<std::string, std::string> &options,
                         std::unordered_map<std::string, std::vector<std::string>> &array_options,
                         std::vector<fs::path> &library_paths, bool never_directing, bool no_inject) {
    this->api_error_code = ADAPTYST_OK;
    this->api_error_msg = "OK, no errors";
    bool found = false;
    fs::path backend_path, library_path;

    for (fs::path &path : library_paths) {
      backend_path = path / backend_name / ("lib" + backend_name + ".so");
      library_path = path;

      if (fs::exists(backend_path)) {
        found = true;
        break;
      }
    }

    if (!found) {
      this->throw_error("Could not find the module!");
    }

    this->lib_path = backend_path;
    this->handle = dlopen(backend_path.c_str(), RTLD_LAZY);

    if (!this->handle) {
      this->throw_error("Could not load the module! " + std::string(dlerror()));
    }

    unsigned int *max_count_per_entity = (unsigned int *)dlsym(this->handle,
                                                               "max_count_per_entity");

    this->max_count_per_entity = max_count_per_entity ? *max_count_per_entity : 0;

    const char **tags = (const char **)dlsym(this->handle, "tags");

    if (!tags) {
      this->throw_error("Module \"" + backend_name + "\" doesn't define its tags!");
    }

    for (int i = 0; tags[i]; i++) {
      this->tags.insert(std::string(tags[i]));
    }

    const char **backend_options = (const char **)dlsym(this->handle, "options");

    if (!backend_options) {
      this->throw_error("Module \"" + backend_name + "\" doesn't define "
                        "what options are available!");
    }

    for (int i = 0; backend_options[i]; i++) {
      std::string name(backend_options[i]);
      OptionMetadata metadata;

      const char **help = (const char **)dlsym(this->handle,
                                             (name + "_help").c_str());

      if (!help) {
        this->throw_error("Module \"" + backend_name + "\" doesn't define any "
                          "help message for option \"" + name + "\"!");
      }

      metadata.help = std::string(*help);

      option_type *type = (option_type *)dlsym(this->handle,
                                               (name + "_type").c_str());

      if (type) {
        metadata.type = *type;
      } else {
        metadata.type = NONE;
      }

      option_type *array_type = (option_type *)dlsym(this->handle,
                                                     (name + "_array_type").c_str());

      if (array_type) {
        metadata.array_type = *array_type;
      } else {
        metadata.array_type = NONE;
      }

      if (!type && !array_type) {
        this->throw_error("Module \"" + backend_name + "\" doesn't define any "
                          "type for option \"" + name + "\"!");
      }

      metadata.default_value = dlsym(this->handle,
                                     (name + "_default").c_str());
      metadata.default_array_value = dlsym(this->handle,
                                           (name + "_array_default").c_str());

      unsigned int *default_array_value_size =
        (unsigned int *)dlsym(this->handle,
                              (name + "_array_default_size").c_str());

      if (default_array_value_size) {
        metadata.default_array_value_size = *default_array_value_size;
      } else {
        metadata.default_array_value_size = 0;
      }

      this->option_metadata[name] = metadata;

      if (options.find(name) == options.end() &&
          array_options.find(name) == array_options.end()) {
        if (metadata.type != NONE && metadata.default_value) {
          this->options[name] = option(metadata.type, metadata.default_value, 0);
        } else if (metadata.array_type != NONE &&
                   metadata.default_array_value) {
          this->options[name] = option(metadata.array_type,
                                       metadata.default_array_value,
                                       metadata.default_array_value_size);
        } else {
          this->throw_error("Module \"" + backend_name +
                            "\" requires option "
                            "\"" +
                            name + "\" to be set!");
        }
      } else if (options.find(name) != options.end()) {
        void *allocated = NULL;

        switch (metadata.type) {
        case INT:
          allocated = malloc(sizeof(int));
          break;

        case STRING:
          allocated = malloc(sizeof(char *));
          break;

        case UNSIGNED_INT:
          allocated = malloc(sizeof(unsigned int));
          break;

        case BOOL:
          allocated = malloc(sizeof(bool));
          break;

        default:
          throw std::runtime_error("Unsupported option type for \"" + name + "\"");
        }

        if (!allocated) {
          throw std::runtime_error("Could not allocate memory for \"" + name + "\"");
        }

        malloced.push_back(allocated);
        void *str = NULL;

        try {
          switch (metadata.type) {
          case INT:
            *((int *)allocated) = std::stoi(options[name]);
            break;

          case STRING:
            str = calloc(options[name].length() + 1,
                         sizeof(char));

            if (!str) {
              throw std::runtime_error("Could not allocate memory for \"" + name + "\" "
                                       "(stage 2)");
            }

            malloced.push_back(str);

            strncpy((char *)str, options[name].c_str(),
                    options[name].length());
            *((char **)allocated) = (char *)str;
            break;

          case UNSIGNED_INT:
            *((unsigned int *)allocated) = std::stoul(options[name]);
            break;

          case BOOL:
            std::istringstream(options[name]) >> std::boolalpha >>
                *((bool *)allocated);
            break;

          default:
            // This should have been caught in the previous "switch"
            break;
          }
        } catch (std::invalid_argument) {
          throw std::runtime_error("Could not parse value of \"" + name + "\"");
        } catch (std::out_of_range) {
          throw std::runtime_error("Could not parse value of \"" + name + "\"");
        }

        this->options[name] = option(metadata.type, allocated, 0);
      } else {
        void *allocated = NULL;

        switch (metadata.array_type) {
        case INT:
          allocated = malloc(array_options[name].size() * sizeof(int));
          break;

        case STRING:
          allocated = malloc(array_options[name].size() * sizeof(char *));
          break;

        case UNSIGNED_INT:
          allocated = malloc(array_options[name].size() * sizeof(unsigned int));
          break;

        case BOOL:
          allocated = malloc(array_options[name].size() * sizeof(bool));
          break;

        default:
          throw std::runtime_error("Unsupported option array type for \"" + name + "\"");
        }

        if (!allocated) {
          throw std::runtime_error("Could not allocate memory for \"" + name + "\" (array)");
        }

        malloced.push_back(allocated);

        int i = -1;

        try {
          switch (metadata.array_type) {
          case INT:
            for (i = 0; i < array_options[name].size(); i++) {
              ((int *)allocated)[i] = std::stoi(array_options[name][i]);
            }
            break;

          case STRING:
            for (i = 0; i < array_options[name].size(); i++) {
              void *element = calloc(array_options[name][i].length() + 1,
                                     sizeof(char));

              if (!element) {
                throw std::runtime_error("Could not allocate memory for element of index " +
                                         std::to_string(i) + " of \"" + name + "\"");
              }

              malloced.push_back(element);

              strncpy((char *)element, array_options[name][i].c_str(),
                      array_options[name][i].length());

              ((char **)allocated)[i] = (char *) element;
            }
            break;

          case UNSIGNED_INT:
            for (i = 0; i < array_options[name].size(); i++) {
              ((unsigned int *)allocated)[i] = std::stoul(array_options[name][i]);
            }
            break;

          case BOOL:
            for (i = 0; i < array_options[name].size(); i++) {
              std::istringstream(array_options[name][i]) >> std::boolalpha >>
                ((bool *)allocated)[i];
            }
            break;

          default:
            // This should have been caught in the previous "switch"
            break;
          }
        } catch (std::invalid_argument) {
          throw std::runtime_error("Could not parse value of element of index " +
                                   std::to_string(i) + " of \"" + name + "\"");
        } catch (std::out_of_range) {
          throw std::runtime_error("Could not parse value of element of index " +
                                   std::to_string(i) + " of \"" + name + "\"");
        }

        this->options[name] =
          option(metadata.array_type, &allocated, array_options[name].size());
      }
    }

    const char **log_types =
      (const char **)dlsym(this->handle, "log_types");
    this->log_types = {"General"};

    if (log_types) {
      for (int i = 0; log_types[i]; i++) {
        this->log_types.push_back(std::string(log_types[i]));
      }
    }

    fs::path inject_lib_path = library_path / backend_name / ("lib" + backend_name + "_inject.so");

    if (fs::exists(inject_lib_path)) {
      this->inject_lib_path = inject_lib_path;
      this->injection_available = true;
    } else {
      this->injection_available = false;
    }

    // False until process injection mechanism is implemented (including
    // the commented code immediately below, subject to change)
    this->injecting_process = false;

    // if (this->injection_available) {
    //   bool *inject_defined_ptr =
    //     (bool *)dlsym(this->handle, "inject_defined");

    //   if (inject_defined_ptr && *inject_defined_ptr) {
    //     this->inject_lib_path = library_path / backend_name / ("lib" + backend_name + "_inject.so");
    //     void *inject_handle = dlopen(this->inject_lib_path.c_str(), RTLD_LAZY);

    //     if (!inject_handle) {
    //       this->throw_error("Could not load the injection part of the module! " +
    //                         std::string(dlerror()));
    //     }

    //     bool *inject_required_ptr =
    //       (bool *)dlsym(inject_handle, "required");

    //     if (!inject_required_ptr) {
    //       this->throw_error("The injection part of the module doesn't define "
    //                         "\"required\"!");
    //     }

    //     if (*inject_required_ptr && no_inject) {
    //       this->throw_error("--no-inject is not supported by the module!");
    //     }

    //     this->injecting_process = !no_inject;
    //   } else {
    //     this->injecting_process = false;
    //   }
    // } else {
    //   this->injecting_process = false;
    // }

    this->never_directing = never_directing;
    this->id = Module::next_module_id++;
    this->node = nullptr;
    this->initialising = false;

    Module::all_modules[this->id] = this;
  }

  bool Module::init(unsigned int buf_size) {
    this->initialising = true;

    bool (*init_func)(amod_t) =
      (bool (*)(amod_t))dlsym(this->handle, "adaptyst_module_init");

    if (!init_func) {
      this->throw_error("Module \"" + this->get_name() + "\" doesn't define adaptyst_module_init()! "
                        "Has it been compiled correctly?");
    }

    int read_fd[2];
    int write_fd[2];

    if (pipe(read_fd) == -1) {
      this->throw_error("pipe() failed for read_fd, error " + std::to_string(errno));
    }

    if (pipe(write_fd) == -1) {
      ::close(read_fd[0]);
      ::close(read_fd[1]);

      this->throw_error("pipe() failed for write_fd, error " + std::to_string(errno));
    }

    this->fd = std::make_shared<FileDescriptor>(read_fd, write_fd, buf_size);

    bool result = init_func(this->id);

    if (result) {
      this->initialised = true;
    } else if (!this->error.empty()) {
      this->throw_error(this->error);
    }

    this->initialising = false;
    return result;
  }

  void Module::process(std::shared_ptr<IR> ir_obj) {
    bool (*process_func)(amod_t, ir) =
      (bool (*)(amod_t, ir))dlsym(this->handle,
                                  "adaptyst_module_process");

    if (!process_func) {
      this->throw_error("Module \"" + this->get_name() + "\" doesn't define adaptyst_module_process()! "
                        "Has it been compiled correctly?");
    }

    ir ir_info = ir_obj->to_c_type();

    this->process_future = std::async([this, process_func, ir_info]() {
      return process_func(this->id, ir_info);
    });
  }

  bool Module::wait() {
    bool result = this->process_future.get();

    if (!result && !this->error.empty()) {
      this->throw_error(this->error);
    }

    return result;
  }

  void Module::close() {
    if (!this->initialised) {
      return;
    }

    void (*close)(amod_t) = (void (*)(amod_t))dlsym(this->handle,
                                                    "adaptyst_module_close");

    if (!close) {
      this->throw_error("Module \"" + this->get_name() + "\" doesn't define adaptyst_module_close()! "
                        "Has it been compiled correctly?");
    }

    close(this->id);
  }

  void Module::set_will_profile(bool will_profile) {
    this->will_profile = will_profile;

    if (will_profile) {
      this->node->inc_modules_profiling();
    }
  }

  bool Module::get_will_profile() {
    return this->will_profile;
  }

  void Module::set_error(std::string error) {
    this->error = error;
  }

  std::unordered_map<std::string, option> &Module::get_options() {
    return this->options;
  }

  std::unique_ptr<Path> &Module::get_dir() {
    return this->dir;
  }

  std::unordered_set<std::string> &Module::get_tags() {
    return this->tags;
  }

  std::unordered_map<std::string, Module::OptionMetadata> &Module::get_all_options() {
    return this->option_metadata;
  }

  void Module::set_dir(fs::path dir) {
    this->dir = std::make_unique<Path>(dir);
    this->dir->set_metadata<std::vector<int> >("version", this->get_version_nums());
  }

  void Module::profile_notify() {
    this->node->profile_notify();
  }

  int Module::profile_wait() {
    return this->node->profile_wait();
  }

  std::string &Module::get_node_name() {
    return this->node->get_name();
  }

  std::vector<std::string> Module::get_log_types() {
    return this->log_types;
  }

  std::string Module::get_type() {
    return "Module";
  }

  void Module::set_node(Node *node) {
    this->node = node;
  }

  void Module::set_api_error(std::string msg, int code) {
    this->api_error_msg = msg;
    this->api_error_code = code;
  }

  std::string &Module::get_api_error_msg() {
    return this->api_error_msg;
  }

  int Module::get_api_error_code() {
    return this->api_error_code;
  }

  bool Module::is_directing_node() {
    return !this->never_directing &&
      this->node->is_directing();
  }

  void Module::add_src_code_path(fs::path path) {
    this->src_code_paths.insert(path);
  }

  fs::path &Module::get_tmp_dir() {
    return this->node->get_tmp_dir();
  }

  fs::path &Module::get_local_config_dir() {
    return this->node->get_local_config_dir();
  }

  bool Module::has_in_tag(std::string tag) {
    return this->node->has_in_tag(tag);
  }

  bool Module::has_out_tag(std::string tag) {
    return this->node->has_out_tag(tag);
  }

  profile_info &Module::get_profile_info() {
    return this->node->get_profile_info();
  }

  void Module::set_profile_info(profile_info info) {
    this->node->set_profile_info(info);
  }

  bool Module::is_initialising() {
    return this->initialising;
  }

  const char *Module::get_cpu_mask() {
    return this->node->get_cpu_mask();
  }

  std::unordered_set<fs::path> &Module::get_src_code_paths() {
    return this->src_code_paths;
  }

  bool Module::is_injection_available() {
    return this->injection_available;
  }

  fs::path Module::get_inject_lib_path() {
    return this->inject_lib_path;
  }

  amod_t Module::get_id() {
    return this->id;
  }

  std::shared_ptr<FileDescriptor> Module::get_fd() {
    return this->fd;
  }

  std::string &Module::receive_string_inject(long timeout_seconds) {
    this->last_received_message_inject = this->fd->read(timeout_seconds);
    return this->last_received_message_inject;
  }

  bool Module::is_workflow_running() {
    return this->node->is_workflow_running();
  }

  bool Module::is_workflow_ever_run() {
    return this->node->is_workflow_ever_run();
  }

  unsigned long long Module::get_workflow_start_time(bool &err) {
    return this->node->get_workflow_start_time(err);
  }

  unsigned long long Module::get_workflow_end_time(bool &err) {
    return this->node->get_workflow_end_time(err);
  }

  void Module::region_switch(std::string name, std::string part_id,
                             std::string state, std::string timestamp_str) {
    bool (*func_switch)(amod_t, const char *, const char *, const char *) =
      (bool (*)(amod_t, const char *, const char *, const char *))dlsym(this->handle,
                                                                        ("adaptyst_region_" + state).c_str());

    if (!func_switch) {
      return;
    }

    if (!func_switch(this->id, name.c_str(), part_id.c_str(),
                     timestamp_str.c_str())) {
      Terminal::instance->print(this->error, true, true, this, "General");
    }
  }

  Node::Node(std::string name,
             std::shared_ptr<Entity> &entity) : Identifiable(name) {
    this->entity = entity;
    this->modules_profiling = 0;
  }

  bool Node::init(unsigned int buf_size) {
    for (auto &mod : this->modules) {
      if (!mod->init(buf_size)) {
        return false;
      }
    }

    std::stringstream log_paths;

    log_paths << this->get_path(adaptyst::Terminal::instance->get_log_dir()).string();

    for (auto &mod : this->modules) {
      log_paths << "\n" << mod->get_path(adaptyst::Terminal::instance->get_log_dir()).string();
    }

    Terminal::instance->print("Node " + this->get_name() + " in entity " + this->get_parent_name() +
                              " has been initialised. If produced, you can look at "
                              "the logs of it and its modules in the folders below (also in real time)."
                              "\n" + log_paths.str(), true, false);

    return true;
  }

  void Node::process(std::shared_ptr<IR> ir_obj) {
    for (auto &mod : this->modules) {
      mod->process(ir_obj);
    }
  }

  bool Node::wait() {
    bool success = true;
    for (auto &mod : this->modules) {
      if (!mod->wait()) {
        success = false;
      }
    }

    return success;
  }

  void Node::close() {
    for (auto &mod : this->modules) {
      mod->close();
    }
  }

  std::unordered_set<std::string> &Node::get_tags() {
    return this->tags;
  }

  void Node::profile_notify() {
    this->entity->profile_notify();
  }

  int Node::profile_wait() {
    return this->entity->profile_wait();
  }

  void Node::add_in_tags(std::unordered_set<std::string> &tags) {
    for (auto &tag : tags) {
      this->in_tags.insert(tag);
    }
  }

  void Node::add_out_tags(std::unordered_set<std::string> &tags) {
    for (auto &tag : tags) {
      this->out_tags.insert(tag);
    }
  }

  bool Node::has_in_tag(std::string tag) {
    return this->in_tags.contains(tag);
  }

  bool Node::has_out_tag(std::string tag) {
    return this->out_tags.contains(tag);
  }

  void Node::add_module(std::unique_ptr<Module> &mod) {
    mod->set_parent(this);
    mod->set_node(this);
    this->modules.push_back(std::move(mod));
  }

  int Node::get_modules_profiling() {
    return this->modules_profiling;
  }

  void Node::inc_modules_profiling() {
    this->modules_profiling++;
  }

  void Node::set_dir(fs::path path) {
    this->dir = std::make_unique<Path>(path);

    for (auto &mod : this->modules) {
      mod->set_dir(path / mod->get_name());
    }
  }

  bool Node::is_directing() {
    return this->entity->get_directing_node() == this->get_name();
  }

  profile_info &Node::get_profile_info() {
    return this->entity->get_profile_info();
  }

  void Node::set_profile_info(profile_info info) {
    this->entity->set_profile_info(info);
  }

  const char *Node::get_cpu_mask() {
    return this->entity->get_cpu_mask();
  }

  fs::path &Node::get_tmp_dir() {
    return this->entity->get_tmp_dir();
  }

  fs::path &Node::get_local_config_dir() {
    return this->entity->get_local_config_dir();
  }

  std::unordered_set<fs::path> Node::get_src_code_paths() {
    std::unordered_set<fs::path> paths;

    for (auto &module : this->modules) {
      for (auto &path : module->get_src_code_paths()) {
        paths.insert(path);
      }
    }

    return paths;
  }

  std::vector<std::string> Node::get_log_types() {
    return {};
  }

  std::string Node::get_type() {
    return "Node";
  }

  std::vector<InjectPath> Node::get_module_inject_paths() {
    std::vector<InjectPath> paths;

    for (auto &module : this->modules) {
      if (!module->is_injection_available()) {
        continue;
      }

      auto read_fd = module->get_fd()->get_read_fd();
      auto write_fd = module->get_fd()->get_write_fd();

      InjectPath path = {
        module->get_name(),
        module->get_id(),
        {read_fd.first, read_fd.second},
        {write_fd.first, write_fd.second},
        module->get_inject_lib_path()
      };

      paths.push_back(path);
    }

    return paths;
  }

  bool Node::is_workflow_running() {
    return this->entity->is_workflow_running();
  }

  bool Node::is_workflow_ever_run() {
    return this->entity->is_workflow_ever_run();
  }

  unsigned long long Node::get_workflow_start_time(bool &err) {
    return this->entity->get_workflow_start_time(err);
  }

  unsigned long long Node::get_workflow_end_time(bool &err) {
    return this->entity->get_workflow_end_time(err);
  }

  void Node::region_switch(std::string name, std::string part_id,
                           std::string state, std::string timestamp_str) {
    for (auto &mod : this->modules) {
      mod->region_switch(name, part_id, state, timestamp_str);
    }
  }

  NodeConnection::NodeConnection(std::string id,
                                 std::shared_ptr<Node> &departure_node,
                                 std::shared_ptr<Node> &arrival_node) : Identifiable(id) {
    this->departure_node = departure_node;
    this->arrival_node = arrival_node;
  }

  std::shared_ptr<Node> &NodeConnection::get_departure_node() {
    return this->departure_node;
  }

  std::shared_ptr<Node> &NodeConnection::get_arrival_node() {
    return this->arrival_node;
  }

  std::vector<std::string> NodeConnection::get_log_types() {
    return {};
  }

  std::string NodeConnection::get_type() {
    return "Connection";
  }

  Entity::Entity(std::string id, AccessMode access_mode,
                 unsigned int processing_threads,
                 fs::path local_config_path,
                 fs::path tmp_dir, bool no_inject,
                 unsigned int buf_size) : Identifiable(id) {
    this->access_mode = access_mode;
    this->processing_threads = processing_threads;
    this->local_config_path = local_config_path;
    this->tmp_dir = tmp_dir;
    this->no_inject = no_inject;
    this->buf_size = buf_size;
    this->src_code_paths_collected = false;
    this->workflow_finish_printed = false;
    this->modules_notified = 0;
    this->modules_profiling = 0;
    this->workflow_timestamp = 0;
    this->workflow_timestamp_error = false;
    this->workflow_end_timestamp = 0;
    this->workflow_end_timestamp_error = false;
    this->process_finished = false;
    this->workflow_start_time = 0;
    this->workflow_start_time_set = false;
  }

  void Entity::add_node(std::shared_ptr<Node> &node) {
    node->set_parent(this);
    this->nodes[node->get_name()] = node;
  }

  void Entity::add_connection(std::string id,
                              std::string departure_node,
                              std::string arrival_node) {
    if (this->connections.find(id) != this->connections.end()) {
      this->throw_error("A connection with ID \"" + id + "\" already exists!");
    }

    this->connections[id] =
      std::make_shared<NodeConnection>(id, this->get_node(departure_node),
                                   this->get_node(arrival_node));
  }

  std::shared_ptr<Node> &Entity::get_node(std::string id) {
    if (this->nodes.find(id) == this->nodes.end()) {
      this->throw_error("Node \"" + id + "\" does not exist!");
    }

    return this->nodes[id];
  }

  void Entity::set_directing_node(std::string node) {
    this->directing_node = node;
  }

  std::string Entity::get_directing_node() {
    return this->directing_node;
  }

  profile_info &Entity::get_profile_info() {
    return this->profiling_info;
  }

  void Entity::set_profile_info(profile_info &info) {
    this->profiling_info = info;
  }

  void Entity::init() {
    for (auto entry : this->nodes) {
      entry.second->init(this->buf_size);

      this->modules_profiling += entry.second->get_modules_profiling();
    }
  }

  void Entity::process(bool save_src_code_paths) {
    if (this->modules_profiling > 0 && this->access_mode != CUSTOM &&
        this->access_mode != CUSTOM_REMOTE) {
      ir_obj->compile();

      fs::path stdout_path =
        fs::path(Terminal::instance->get_log_dir()) / (this->get_name() + "_stdout.log");
      fs::path stderr_path =
        fs::path(Terminal::instance->get_log_dir()) / (this->get_name() + "_stderr.log");

      int pipe1[2];
      int pipe2[2];

      if (pipe(pipe1) == -1 || pipe(pipe2) == -1) {
        this->throw_error("pipe() failed when preparing to run the workflow");
      }

      this->profiled_process = ir_obj->execute();

      this->profiled_process->set_redirect_stdout(stdout_path);
      this->profiled_process->set_redirect_stderr(stderr_path);

      this->workflow_stdout_path = stdout_path;
      this->workflow_stderr_path = stderr_path;

      this->profiled_process->add_env("ADAPTYST_READ_FD1", std::to_string(pipe1[0]));
      this->profiled_process->add_env("ADAPTYST_READ_FD2", std::to_string(pipe1[1]));
      this->profiled_process->add_env("ADAPTYST_WRITE_FD1", std::to_string(pipe2[0]));
      this->profiled_process->add_env("ADAPTYST_WRITE_FD2", std::to_string(pipe2[1]));

      int read_fd1 = pipe1[0];
      int read_fd2 = pipe1[1];
      int write_fd1 = pipe2[0];
      int write_fd2 = pipe2[1];

      std::vector<InjectPath> module_inject_paths;

      for (auto &entry : this->nodes) {
        for (auto &path : entry.second->get_module_inject_paths()) {
          module_inject_paths.push_back(path);
        }
      }

      this->profiling_info.type = LINUX_PROCESS;
      this->profiling_info.data.pid = this->profiled_process->start(
          true, CPUConfig(this->get_cpu_mask()), false);

      this->workflow_comm = std::async([this, read_fd1, read_fd2, write_fd1, write_fd2,
                                        module_inject_paths]() {
        int read_fd[2] = {read_fd1, read_fd2};
        int write_fd[2] = {write_fd1, write_fd2};

        FileDescriptor fd(read_fd, write_fd, this->buf_size);
        std::string msg;

        auto get_msg = [this, &fd](std::string &msg) {
          try {
            msg = fd.read(1);
            return !msg.empty();
          } catch (TimeoutException) {
            msg = "";
            return this->is_workflow_running();
          }
        };

        while (get_msg(msg)) {
          if (msg.empty()) {
            continue;
          } else if (msg == "init") {
            fd.write("ack", true);
            for (auto &path : module_inject_paths) {
              fd.write(path.name + " " + std::to_string(path.id) + " " +
                       std::to_string(path.read_fd[0]) + " " +
                       std::to_string(path.read_fd[1]) + " " +
                       std::to_string(path.write_fd[0]) + " " +
                       std::to_string(path.write_fd[1]) + " " +
                       path.path, true);
            }
            fd.write("<STOP>", true);
          } else {
            std::smatch match;

            if (std::regex_match(msg, match, std::regex("^(start|end) (.+) (-?\\d+) (.+)$"))) {
              Terminal::instance->log("Region \"" + match[4].str() +
                                      "\", workflow part ID " + match[2].str() + ": " +
                                      match[1].str() + " at " + match[3].str() + " ns",
                                      this, "General");

              std::string timestamp_str = match[3].str();

              for (auto &entry : this->nodes) {
                entry.second->region_switch(match[4].str(), match[2].str(), match[1].str(),
                                            timestamp_str);
              }

              fd.write("ack", true);
            } else if (std::regex_match(msg, match, std::regex("^time (.+) (.+) (-?\\d+) (-?\\d+)$"))) {
              std::string workflow_part_id = match[1].str();
              std::string func_name = match[2].str();
              std::string timestamp_start_raw_str = match[3].str();
              std::string timestamp_end_raw_str = match[4].str();

              std::string timestamp_start_str, timestamp_end_str;

              if (this->workflow_timestamp_error) {
                timestamp_start_str = "-1";
                timestamp_end_str = "-1";
              } else {
                if (timestamp_start_raw_str == "-1") {
                  timestamp_start_str = "-1";
                } else {
                  timestamp_start_str = std::to_string(std::stoull(timestamp_start_raw_str) - this->workflow_timestamp);
                }

                if (timestamp_end_raw_str == "-1") {
                  timestamp_end_str = "-1";
                } else {
                  timestamp_end_str = std::to_string(std::stoull(timestamp_end_raw_str) - this->workflow_timestamp);
                }
              }

              this->api_timestamps.push_back({workflow_part_id,
                  func_name,
                  timestamp_start_str,
                  timestamp_end_str});

              // No ack here to minimise timestamping overhead on the workflow side.
            } else {
              fd.write("invalid", true);
            }
          }
        }
      });

      Terminal::instance->print("Workflow is ready to run in entity " + this->get_name() + ". "
                                "It will be started when modules indicate that they are ready to "
                                "start performance analysis.", true, false);
    }

    for (auto entry : this->nodes) {
      entry.second->process(this->ir_obj);
    }

    int exit_code = this->profile_wait();
    this->entity_dir->set_metadata<int>("exit_code", exit_code);

    for (auto entry : this->nodes) {
      entry.second->wait();
    }

    Array<ApiTimestamp> api_timestamps_array(*this->entity_dir, "api_timestamps");

    for (auto &timestamp : this->api_timestamps) {
      api_timestamps_array.push_back(timestamp);
    }

    if (save_src_code_paths) {
      Archive archive(fs::path(this->entity_dir->get_path_name()) / "src.zip");
      nlohmann::json src_mapping = nlohmann::json::object();

      for (auto &node_elem : this->nodes) {
        for (auto &path : node_elem.second->get_src_code_paths()) {
          if (!src_mapping.contains(path.string()) && fs::exists(path)) {
            std::string filename =
              std::to_string(src_mapping.size()) + path.extension().string();
            src_mapping[path.string()] = filename;
            archive.add_file(filename, path);
          }

          this->src_code_paths.insert(path);
        }
      }

      std::string src_mapping_str = nlohmann::to_string(src_mapping) + '\n';
      std::stringstream s;
      s << src_mapping_str;
      archive.add_file_stream("index.json", s, src_mapping_str.length());
      archive.close();

      this->src_code_paths_collected = true;
    }
  }

  void Entity::close() {
    for (auto entry : this->nodes) {
      entry.second->close();
    }
  }

  void Entity::set_entity_dir(fs::path &entity_dir) {
    this->entity_dir = std::make_unique<Path>(entity_dir);

    for (auto node : this->nodes) {
      fs::path node_dir = entity_dir / node.first;
      node.second->set_dir(node_dir);
    }
  }

  void Entity::profile_notify() {
    if (this->profiled_process) {
      std::unique_lock lock(this->profile_notify_mutex);

      this->modules_notified++;

      if (this->modules_notified == this->modules_profiling) {
        this->profiled_process->notify();
        this->process_notified = true;

        struct timespec ts;

        if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1) {
          this->workflow_timestamp = 0;
          this->workflow_timestamp_error = true;
        } else {
          this->workflow_timestamp = ts.tv_sec * 1000000000ULL + ts.tv_nsec;
          this->workflow_timestamp_error = false;
        }

        this->workflow_start_time =
            ch::duration_cast<ch::milliseconds>(
                ch::system_clock::now().time_since_epoch())
                .count();
        this->workflow_start_time_set = true;

        Terminal::instance->print(
            "Workflow has been started in entity " + this->get_name() +
                ". "
                "You can check its stdout and stderr in real time by looking "
                "at:\n" +
                this->workflow_stdout_path.string() + "\n" +
                this->workflow_stderr_path.string(),
            true, false);
      }
    }
  }

  int Entity::profile_wait() {
    if (this->profiled_process) {
      std::unique_lock lock(this->profile_wait_mutex);

      if (this->process_finished) {
        return this->process_exit_code;
      }

      int result = this->profiled_process->join();
      this->process_exit_code = result;
      this->process_finished = true;

      struct timespec ts;

      if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1) {
        this->workflow_end_timestamp = 0;
        this->workflow_end_timestamp_error = true;
      } else {
        this->workflow_end_timestamp = ts.tv_sec * 1000000000ULL + ts.tv_nsec;
        this->workflow_end_timestamp_error = false;
      }

      auto end_time = ch::duration_cast<ch::milliseconds>(ch::system_clock::now().time_since_epoch()).count();

      {
        std::unique_lock lock(this->workflow_finish_print_mutex);

        if (!this->workflow_finish_printed) {
          if (this->workflow_start_time_set) {
            unsigned long long elapsed = end_time - this->workflow_start_time;
            std::string elapsed_str;

            if (elapsed >= 1000) {
              int ms = elapsed % 1000;
              elapsed /= 1000;

              elapsed_str = std::to_string(elapsed) + ".";

              if (ms >= 100) {
                elapsed_str += std::to_string(ms);
              } else if (ms >= 10) {
                elapsed_str += "0" + std::to_string(ms);
              } else {
                elapsed_str += "00" + std::to_string(ms);
              }

              elapsed_str += " s";
            } else {
              elapsed_str = std::to_string(elapsed) + " ms";
            }

            if (result == 0) {
              Terminal::instance->print("Workflow in entity " + this->get_name() +
                                        " has completed successfully in " +
                                        elapsed_str + ".",
                                        true, false);
            } else {
              std::string msg = "Workflow in entity " + this->get_name() +
                " has completed with a non-zero exit code "
                "(" + std::to_string(result) + ") in " + elapsed_str + ". "
                "The way of handling this "
                "is module-dependent.";

              if (result == Process::ERROR_ABNORMAL_EXIT) {
                msg += "\nHint: The exit code is " + std::to_string(Process::ERROR_ABNORMAL_EXIT) +
                  ", which may suggest that your workflow "
                  "has encountered an unrecoverable error, e.g. a segmentation fault.";
              }

              Terminal::instance->print(msg, true, true);
            }
          } else {
            std::string msg = "Workflow in entity " + this->get_name() +
              " has completed with exit code " + std::to_string(result) +
              " before all module indications! This should not happen.";

            if (result == Process::ERROR_ABNORMAL_EXIT) {
              msg += "\nHint: The exit code is " + std::to_string(Process::ERROR_ABNORMAL_EXIT) +
                ", which may suggest that your workflow "
                "has encountered an unrecoverable error, e.g. a segmentation fault.";
            }

            Terminal::instance->print(msg, true, true);
          }

          this->workflow_finish_printed = true;
        }
      }

      return result;
    }

    return -1;
  }

  const char *Entity::get_cpu_mask() {
    if (!this->cpu_mask.empty()) {
      return this->cpu_mask.c_str();
    }

    Terminal::instance->print("The CPU mask has been requested, calculating it...", false,
                              false, this, "General");

    int num_proc = std::thread::hardware_concurrency();

    if (num_proc == 0) {
      Terminal::instance->print("Could not determine the number "
                                "of available logical cores!", true, true, this, "General");
      return nullptr;
    }

    std::stringstream mask_stream;

    if (processing_threads == 0) {
      for (int i = 0; i < num_proc; i++) {
        mask_stream << 'b';
      }

      this->cpu_mask = mask_stream.str();
    } else if (processing_threads > num_proc - 3) {
      Terminal::instance->print("The value of \"processing_threads\" must be less "
                                "than or equal to the number of "
                                "logical cores minus 3 (i.e. " +
                                std::to_string(num_proc - 3) + ")!", true, true, this, "General");
      return nullptr;
    } else {
      if (num_proc < 4) {
        Terminal::instance->print("Because there are fewer than 4 logical cores, "
                                  "the value of \"processing_threads\" will be ignored for the profiled "
                                  "program unless it is 0.", true, false);
      }

      switch (num_proc) {
      case 1:
        Terminal::instance->print("Running analysis along with processing is *NOT* "
                                  "recommended on a machine with only one logical core! "
                                  "You are very likely to get inconsistent results due "
                                  "to processing threads interfering with the analysed "
                                  "program. If you want to proceed anyway, "
                                  "set \"processing_threads\" to 0.", true, true, this, "General");
        return nullptr;

      case 2:
        Terminal::instance->print("2 logical cores detected, running processing and "
                                  "hardware modules on core #0 and the command on core #1.", true,
                                  false, this, "General");
        this->cpu_mask = "pc";
        break;

      case 3:
        Terminal::instance->print("3 logical cores detected, running processing and "
                                  "hardware modules on cores #0 and #1 and the command on core #2.",
                                  true, false, this, "General");
        this->cpu_mask = "ppc";
        break;

      default:
        mask_stream << "  ";
        for (int i = 2; i < 2 + processing_threads; i++) {
          mask_stream << 'p';
        }
        for (int i = 2 + processing_threads; i < num_proc; i++) {
          mask_stream << 'c';
        }
        this->cpu_mask = mask_stream.str();
        break;
      }
    }

    Terminal::instance->print("The CPU mask has been obtained.", false,
                              false, this, "General");
    return this->cpu_mask.c_str();
  }

  fs::path &Entity::get_tmp_dir() {
    return this->tmp_dir;
  }

  fs::path &Entity::get_local_config_dir() {
    return this->local_config_path;
  }

  std::vector<std::shared_ptr<Node>> Entity::get_all_nodes() {
    std::vector<std::shared_ptr<Node> > result;

    for (auto &node : this->nodes) {
      result.push_back(node.second);
    }

    return result;
  }

  std::vector<std::string> Entity::get_log_types() {
    return {"General", "stdout", "stderr"};
  }

  std::string Entity::get_type() {
    return "Entity";
  }

  void Entity::set_ir(std::shared_ptr<IR> ir_obj) {
    this->ir_obj = ir_obj;
  }

  std::unordered_set<fs::path> &Entity::get_src_code_paths() {
    if (!this->src_code_paths_collected) {
      for (auto &node_elem : this->nodes) {
        for (auto &path : node_elem.second->get_src_code_paths()) {
          this->src_code_paths.insert(path);
        }
      }

      this->src_code_paths_collected = true;
    }

    return this->src_code_paths;
  }

  bool Entity::is_workflow_running() {
    if (this->profiled_process) {
      return this->profiled_process->is_running();
    }

    return false;
  }

  bool Entity::is_workflow_ever_run() {
    if (this->profiled_process) {
      return this->process_notified;
    }

    return false;
  }

  unsigned long long Entity::get_workflow_start_time(bool &err) {
    err = this->workflow_timestamp_error;
    return this->workflow_timestamp;
  }

  unsigned long long Entity::get_workflow_end_time(bool &err) {
    err = this->workflow_end_timestamp_error;
    return this->workflow_end_timestamp;
  }

  void System::init(fs::path def_file,
                    fs::path root_dir,
                    std::vector<fs::path> &library_paths,
                    fs::path local_config_path,
                    fs::path tmp_dir,
                    bool no_inject,
                    unsigned int buf_size) {
    ryml::Tree tree;

    {
      std::ifstream yaml(def_file);
      std::string yaml_str((std::istreambuf_iterator<char>(yaml)),
                            std::istreambuf_iterator<char>());
      tree = ryml::parse_in_arena(yaml_str.c_str());
    }

    auto root = tree.rootref();

    if (!root.is_map()) {
       throw std::runtime_error("The system YAML file is not a map!");
    }

    if (!root.has_child("entities")) {
       throw std::runtime_error("The system YAML file does not have "
                               "\"entities\" in its root!");
    }

    auto entities = root["entities"];

    if (!entities.is_map()) {
      throw std::runtime_error("\"entities\" in the system YAML file "
                               "is not a map!");
    }

    for (auto entity : entities.children()) {
      std::unordered_map<std::string, unsigned int> mod_counts;
      std::string name(entity.key().data(), entity.key().len);

      if (!entity.is_map()) {
        throw std::runtime_error("\"" + name + "\" in \"entities\" in "
                                 "the system YAML file is not a map!");
      }

      if (!entity.has_child("options")) {
        throw std::runtime_error("\"" + name + "\" in \"entities\" "
                                 "in the system YAML file does not have "
                                 "\"options\"!");
      }

      auto options = entity["options"];

      if (!options.is_map()) {
        throw std::runtime_error("\"options\" in \"" + name + "\" in "
                                 "\"entities\" in the system YAML file "
                                 "is not a map!");
      }

      if (!options.has_child("handle_mode")) {
        throw std::runtime_error("\"options\" in \"" + name + "\" in "
                                 "\"entities\" in the system YAML file "
                                 "does not have \"handle_mode\"!");
      }


      auto access_mode = options["handle_mode"];

      if (!access_mode.is_keyval()) {
        throw std::runtime_error("\"handle_mode\" in \"options\" in "
                                 "\"" + name + "\" in \"entities\" "
                                 "in the system YAML file is not of "
                                 "simple key-value type!");
      }

      unsigned int processing_threads = 1;

      if (options.has_child("processing_threads")) {
        auto threads = options["processing_threads"];
        if (!threads.is_keyval()) {
          throw std::runtime_error("\"processing_threads\" in \"options\" in "
                                   "\"" + name + "\" in \"entities\" "
                                   "in the system YAML file is not of "
                                   "simple key-value type!");
        }

        std::string threads_str(threads.val().data(), threads.val().len);

        try {
          processing_threads = std::stoul(threads_str);
        } catch (...) {
          throw std::runtime_error("\"processing_threads\" in \"options\" in "
                                   "\"" + name + "\" in \"entities\" "
                                   "in the system YAML file is not a valid "
                                   "unsigned integer!");
        }
      }

      std::string access_mode_val(access_mode.val().data(), access_mode.val().len);
      Entity::AccessMode access_mode_final;

      if (access_mode_val == "local") {
        access_mode_final = Entity::LOCAL;
      } else if (access_mode_val == "remote") {
        throw std::runtime_error("Remote access to entities is not "
                                 "yet supported! (entity \"" + name + "\")");
      } else if (access_mode_val == "custom") {
        access_mode_final = Entity::CUSTOM;
      } else if (access_mode_val == "custom_remote") {
        throw std::runtime_error("Remote access to entities is not "
                                 "yet supported! (entity \"" + name + "\")");
      } else {
        throw std::runtime_error("\"handle_mode\" in \"options\" in "
                                 "\"" + name + "\" in \"entities\" "
                                 "in the system YAML file has an "
                                 "invalid value! " + access_mode_val);
      }

      std::shared_ptr<Entity> entity_obj =
        std::make_shared<Entity>(name, access_mode_final,
                                 processing_threads,
                                 local_config_path,
                                 tmp_dir, no_inject, buf_size);

      if (!entity.has_child("nodes")) {
        throw std::runtime_error("\"" + name + "\" in \"entities\" in "
                                 "the system YAML file does not have "
                                 "\"nodes\"!");
      }

      auto nodes = entity["nodes"];

      if (!nodes.is_map()) {
        throw std::runtime_error("\"nodes\" in \"" + name + "\" in "
                                 "\"entities\" in the system YAML file "
                                 "is not a map!");
      }

      for (auto node : nodes.children()) {
        std::string node_name(node.key().data(), node.key().len);
        if (!node.is_map()) {
          throw std::runtime_error("Node \"" + node_name + "\" in entity "
                                   "\"" + name + "\" in the system YAML file "
                                   "is not a map!");
        }

        bool directing = false;

        if (node.has_child("directing")) {
          if (!c4::from_chars(node["directing"].val(), &directing)) {
            throw std::runtime_error("\"directing\" in node \"" + node_name + "\" in "
                                     "entity \"" + name + "\" in the system YAML file "
                                     "is not a valid boolean!");
          }
        }

        if (access_mode_final != Entity::CUSTOM &&
            access_mode_final != Entity::CUSTOM_REMOTE) {
          directing = false;
        }

        if (!node.has_child("modules")) {
          throw std::runtime_error("Node \"" + node_name + "\" in entity "
                                   "\"" + name + "\" in the system YAML file "
                                   "does not have \"modules\"!");
        }

        auto modules = node["modules"];

        if (!modules.is_seq()) {
          throw std::runtime_error("\"modules\" in node \"" + node_name + "\" in "
                                   "entity \"" + name + "\" in the system YAML file "
                                   "is not of a sequence form!");
        }

        std::shared_ptr<Node> node_obj = std::make_shared<Node>(node_name,
                                                                entity_obj);

        int index = 0;
        for (auto mod : modules.children()) {
          auto module_name = mod["name"];

          if (!module_name.is_keyval()) {
            throw std::runtime_error("\"name\" in entry " + std::to_string(index) +
                                     " of \"modules\" in node \"" +
                                     node_name + "\" in entity \"" + name + "\" in "
                                     "the system YAML file is not of simple key-value type!");
          }

          std::string module_name_str(module_name.val().data(), module_name.val().len);
          bool never_directing = false;

          if (mod.has_child("never_directing")) {
            if (!c4::from_chars(mod["never_directing"].val(), &never_directing)) {
              throw std::runtime_error("\"never_directing\" in module \"" + module_name_str +
                                       "\" in node \"" + node_name + "\" in entity \"" + name +
                                       "\" in the system YAML file is not a valid boolean!");
            }
          }

          if (!directing) {
            never_directing = false;
          }

          std::unordered_map<std::string, std::string> options_map;
          std::unordered_map<std::string, std::vector<std::string> > array_options_map;

          if (mod.has_child("options")) {
            auto options = mod["options"];

            if (!options.is_map()) {
              throw std::runtime_error("\"options\" in module \"" + module_name_str +
                                       " in node \"" + node_name + "\" in "
                                       "entity \"" + name + "\" in the system YAML file "
                                       "is not a map!");
            }

            for (auto option : options.children()) {
              std::string key(option.key().data(), option.key().len);

              if (option.is_keyval()) {
                options_map[key] = std::string(option.val().data(), option.val().len);
              } else if (option.is_seq()) {
                array_options_map[key] = std::vector<std::string>();

                for (auto element : option.children()) {
                  if (!element.is_val()) {
                    throw std::runtime_error(
                        "Element with index " +
                        std::to_string(array_options_map[key].size()) +
                        " in option \"" + key + "\" in module \"" +
                        module_name_str + "\" in node \"" + node_name +
                        "\" in entity \"" + name +
                        "\" in "
                        "the system YAML file is not a simple value!");
                  }

                  array_options_map[key].push_back(
                      std::string(element.val().data(), element.val().len));
                }
              }
            }
          }

          std::unique_ptr<Module> mod_obj = std::make_unique<Module>(
                module_name_str, options_map, array_options_map, library_paths,
                never_directing, no_inject);

          if (mod_counts.contains(module_name_str)) {
            mod_counts[module_name_str]++;
          } else {
            mod_counts[module_name_str] = 1;
          }

          if (mod_obj->get_max_count_per_entity() > 0 &&
              mod_counts[module_name_str] >
              mod_obj->get_max_count_per_entity()) {
            throw std::runtime_error("Too many module instances of " +
                                     module_name_str + " are declared! "
                                     "The maximum allowed number is " +
                                     std::to_string(mod_obj->get_max_count_per_entity()) + ".");
          }

          node_obj->add_module(mod_obj);

          index++;
        }

        entity_obj->add_node(node_obj);
      }

      if (entity.has_child("edges")) {
        auto edges = entity["edges"];

        if (!edges.is_map()) {
          throw std::runtime_error("\"edges\" in \"" + name + "\" in "
                                   "\"entities\" in the system YAML file "
                                   "is not a map!");
        }

        for (auto edge : edges.children()) {
          std::string edge_name(edge.key().data(), edge.key().len);

          if (!edge.is_map()) {
            throw std::runtime_error("\"" + edge_name + "\" in \"edges\" in "
                                     "\"" + name + "\" in \"entities\" in the "
                                     "system YAML file is not a map!");
          }

          if (!edge.has_child("from")) {
            throw std::runtime_error("Edge \"" + edge_name + "\" in entity \"" +
                                     name + "\" in the system YAML file does not "
                                     "have \"from\"!");
          }

          if (!edge.has_child("to")) {
            throw std::runtime_error("Edge \"" + edge_name + "\" in entity \"" +
                                     name + "\" in the system YAML file does not "
                                     "have \"to\"!");
          }

          auto node1 = edge["from"];
          auto node2 = edge["to"];

          if (!node1.is_keyval()) {
            throw std::runtime_error("\"from\" in edge \"" +
                                     edge_name + "\" in entity \"" + name + "\" "
                                     "in the system YAML file is not of a simple "
                                     "key-value type!");
          }

          if (!node2.is_keyval()) {
            throw std::runtime_error("\"to\" in edge \"" +
                                     edge_name + "\" in entity \"" + name +
                                     "\" "
                                     "in the system YAML file is not of a simple "
                                     "key-value type!");
          }

          std::shared_ptr<NodeConnection> connection =
            std::make_shared<
              NodeConnection>(edge_name, entity_obj->get_node(std::string(node1.val().data(), node1.val().len)),
                              entity_obj->get_node(std::string(node2.val().data(), node2.val().len)));
        }
      }

      this->entities[name] = entity_obj;
    }

    this->root_dir = std::make_unique<Path>(root_dir);

    try {
      if (!fs::copy_file(def_file, root_dir / "system.yml")) {
        throw std::runtime_error("Could not copy the system definition file "
                                 "to the output directory!");
      }
    } catch (fs::filesystem_error &e) {
      throw std::runtime_error("Could not copy the system definition file "
                               "to the output directory! Error details: " +
                               std::string(e.what()));
    }

    for (auto &entity : this->entities) {
      fs::path entity_dir = root_dir / entity.first;
      entity.second->set_entity_dir(entity_dir);
      entity.second->init();
    }
  }

  System::System(fs::path def_file,
                 fs::path root_dir,
                 std::vector<fs::path> &library_paths,
                 fs::path local_config_path,
                 fs::path tmp_dir,
                 bool no_inject,
                 unsigned int buf_size) {
    this->init(def_file, root_dir, library_paths,
               local_config_path, tmp_dir, no_inject, buf_size);
    this->custom_src_code_paths_save = false;
  }

  System::System(fs::path def_file,
                 fs::path root_dir,
                 std::vector<fs::path> &library_paths,
                 fs::path local_config_path,
                 fs::path tmp_dir,
                 bool no_inject,
                 unsigned int buf_size,
                 std::variant<fs::path, int> codes_dst) {
    this->init(def_file, root_dir, library_paths,
               local_config_path, tmp_dir, no_inject, buf_size);
    this->custom_src_code_paths_save = true;
    this->codes_dst = codes_dst;
  }

  System::~System() {
    for (auto &entity : this->entities) {
      entity.second->close();
    }
  }

  void System::set_ir(std::shared_ptr<IR> ir_obj) {
    for (auto &entity : this->entities) {
      entity.second->set_ir(ir_obj);
    }
  }

  void System::process() {
    for (auto &entity : this->entities) {
      entity.second->process(!this->custom_src_code_paths_save);
    }

    if (this->custom_src_code_paths_save) {
      if (this->codes_dst.index() == 0) {
        fs::path src_code_paths_file = std::get<0>(this->codes_dst);

        std::ofstream src_code_paths_stream(src_code_paths_file);

        if (!src_code_paths_stream) {
          throw std::runtime_error("Could not open " +
                                   src_code_paths_file.string() +
                                   " for writing!");
        }

        for (auto &entry : this->entities) {
          for (auto &path : entry.second->get_src_code_paths()) {
            src_code_paths_stream << path.string() << std::endl;
          }
        }
      } else {
        int write_fd[2] = {-1, std::get<1>(this->codes_dst)};
        FileDescriptor fd(nullptr, write_fd, 1024);

        for (auto &entry : this->entities) {
          for (auto &path : entry.second->get_src_code_paths()) {
            fd.write(path.string(), true);
          }
        }
      }
    }
  }
};
