// SPDX-FileCopyrightText: 2026 CERN
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "ir.hpp"

namespace adaptyst {
  IR::IR(unsigned int type) {
    this->type = type;
    this->compiled = false;
  }

  ir IR::to_c_type() {
    return { this->type, this->get_c_data() };
  }

  void IR::compile() {
    this->_compile();
    this->compiled = true;
  }

  std::unique_ptr<Process> IR::execute() {
    if (!this->compiled) {
      throw std::runtime_error("Compile first before calling execute()");
    }

    return std::make_unique<Process>([this]() {
      return this->_execute();
    });
  }

  MLIR::MLIR(fs::path output_dir) : IR(ADAPTYST_IR_MLIR) {
    this->output_dir = std::make_unique<Path>(output_dir);
  }

  void *MLIR::get_c_data() {
    throw std::runtime_error("MLIR class is not implemented yet");
  }

  void MLIR::_compile() {
    throw std::runtime_error("MLIR class is not implemented yet");
  }

  int MLIR::_execute() {
    Path exec_dir = *this->output_dir / "workflow";
    char *const argv[] = {const_cast<char *>("workflow"), NULL};
    execv(exec_dir.get_path_name(), argv);
    return errno;
  }

  SingleCmd::SingleCmd(std::vector<std::string> elements) : IR(ADAPTYST_IR_SINGLE_CMD) {
    for (std::string &s : elements) {
      this->element_sizes.push_back(s.length());
    }

    this->cmd_copies.push_back(new char *[this->element_sizes.size() + 1]);
    for (int i = 0; i < this->element_sizes.size(); i++) {
      this->cmd_copies[0][i] = new char[this->element_sizes[i] + 1];
      this->cmd_copies[0][i][this->element_sizes[i]] = 0;
      strncpy(this->cmd_copies[0][i], elements[i].c_str(), this->element_sizes[i]);
    }

    this->cmd_copies[0][this->element_sizes.size()] = 0;
  }

  SingleCmd::~SingleCmd() {
    for (char **ptr : this->cmd_copies) {
      for (int i = 0; ptr[i]; i++) {
        delete [] ptr[i];
      }

      delete [] ptr;
    }
  }

  void *SingleCmd::get_c_data() {
    this->cmd_copies.push_back(new char *[this->element_sizes.size() + 1]);
    int index = this->cmd_copies.size() - 1;
    for (int i = 0; i < this->element_sizes.size(); i++) {
      this->cmd_copies[index][i] = new char[this->element_sizes[i] + 1];
      this->cmd_copies[index][i][this->element_sizes[i]] = 0;
      strncpy(this->cmd_copies[index][i], this->cmd_copies[0][i], this->element_sizes[i]);
    }

    this->cmd_copies[index][this->element_sizes.size()] = 0;

    return this->cmd_copies[index];
  }

  void SingleCmd::_compile() {}

  int SingleCmd::_execute() {
    int forked = fork();

    if (forked == 0) {
      execvp(this->cmd_copies[0][0], this->cmd_copies[0]);
      std::exit(errno);
    } else {
      int status;
      int result = waitpid(forked, &status, 0);

      if (result != forked) {
        return 104;
      } else if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
      } else {
        return 210;
      }
    }
  }
};
