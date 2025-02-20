#ifndef COMMON_HPP_
#define COMMON_HPP_

#include "archive.hpp"
#include <unordered_set>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace aperf {
  void create_src_archive(Archive &archive,
                          std::unordered_set<fs::path> &src_paths,
                          bool close) {
    nlohmann::json src_mapping = nlohmann::json::object();
    for (const fs::path &path : src_paths) {
      std::string filename =
        std::to_string(src_mapping.size()) + path.extension().string();
      src_mapping[path.string()] = filename;
      archive.add_file(filename, path);
    }

    std::string src_mapping_str = nlohmann::to_string(src_mapping) + '\n';
    std::stringstream s;
    s << src_mapping_str;
    archive.add_file_stream("index.json", s, src_mapping_str.length());

    if (close) {
      archive.close();
    }
  }
};

#endif
