// SPDX-FileCopyrightText: 2026 CERN
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef ADAPTYST_OUTPUT_HPP_
#define ADAPTYST_OUTPUT_HPP_

#include <filesystem>
#include <nlohmann/json.hpp>
#include <mutex>
#include <fstream>
#include <boost/hana.hpp>
#include <boost/hana/ext/std/tuple.hpp>

namespace adaptyst {
  namespace fs = std::filesystem;
  namespace hana = boost::hana;

  /**
     This abstract class describes an arbitrary object with attached metadata in
     form of key-value pairs, where the value type is templatised.
   */
  class ObjectWithMetadata {
  protected:
    nlohmann::json metadata;

  public:
    ObjectWithMetadata() {
      this->metadata = nlohmann::json::object();
    }

    /**
       Sets a key-value pair in the metadata.

       @param key   Key in the key-value pair.
       @param value Value in the key-value pair.
       @param save  Whether all metadata should be saved to disk or elsewhere
                    after setting the pair.
    */
    template<class T>
    void set_metadata(std::string key, T value,
                      bool save = true) {
      if (this->metadata[key] != value) {
        this->metadata[key] = value;

        if (save) {
          this->save_metadata();
        }
      }
    }

    /**
       Gets a value from the metadata based on a given key,
       with the default value provided if the key is not found.

       @param key           Key to look for.
       @param default_value Value to return if the key is not found.

       @return Value corresponding to the key or the default value
               if the key is not found.
    */
    template<class T>
    T get_metadata(std::string key, T default_value) {
      return this->metadata.value(key, default_value);
    }

    /**
       Gets a value from the metadata based on a given key,
       throwing an exception if the key is not found.

       @param key Key to look for.

       @return Value corresponding to the key.
    */
    template<class T>
    T get_metadata(std::string key) {
      return this->metadata[key].get<T>();
    }

    /**
       Saves metadata in a way dependent on the implementation,
       e.g. to disk.
    */
    virtual void save_metadata() = 0;
  };

  /**
     This class represents a directory path with metadata attached
     to it thanks to inheriting from ObjectWithMetadata.
  */
  class Path : public ObjectWithMetadata {
    friend class File;

  private:
    fs::path path;

    void setup(fs::path path) {
      this->path = fs::absolute(path);
      try {
        fs::create_directories(this->path);
      } catch (std::exception &e) {
        throw std::runtime_error("Could not create directory " +
                                 this->path.string() + ": " +
                                 std::string(e.what()));
      }

      fs::path metadata_path = this->path / "dirmeta.json";

      if (fs::exists(metadata_path)) {
        std::ifstream metadata_stream(metadata_path);

        if (!metadata_stream) {
          throw std::runtime_error("Could not open " +
                                 (this->path / "dirmeta.json").string() +
                                 " for reading!");
        }

        std::string json_str((std::istreambuf_iterator<char>(metadata_stream)),
                             std::istreambuf_iterator<char>());
        this->metadata = nlohmann::json::parse(json_str);
      }
    }

  public:
    /**
       Constructs a Path object.

       @param path Path the object should be about.
    */
    Path(fs::path path) {
      this->setup(path);
    }

    /**
       Gets the full path name.

       @return Full path name.
    */
    const char *get_path_name() {
      return this->path.c_str();
    }

    /**
       Performs path concatenation.

       @param second String the path should be concatenated with.
    */
    Path operator/(std::string second) {
      return Path(this->path / second);
    }

    /**
       Performs path concatenation.

       @param second String (in form of const char *) the path should be
                     concatenated with.
    */
    Path operator/(const char *second) {
      return Path(this->path / second);
    }

    /**
       Saves metadata to disk.
    */
    void save_metadata() {
      std::ofstream metadata_stream(this->path / "dirmeta.json");

      if (!metadata_stream) {
        throw std::runtime_error("Could not open " +
                                 (this->path / "dirmeta.json").string() +
                                 " for writing!");
      }

      metadata_stream << this->metadata.dump() << std::endl;
    }
  };

  /**
     This class represents a file with metadata attached to it
     (thanks to inheriting from ObjectFromMetadata) and
     saved separately.
  */
  class File : public ObjectWithMetadata {
  protected:
    Path &path;
    std::string name;
    std::ifstream istream;
    std::ofstream ostream;

  public:
    /**
       Constructs a File object.

       @param path      Path to a directory where the file is.
       @param name      Name of the file without any extension.
       @param extension Extension of the file.
       @param truncate  Whether file contents should be truncated if
                        the file already exists.
    */
    File(Path &path, std::string name,
         std::string extension = "",
         bool truncate = true) : path(path) {
      this->name = name;

      fs::path new_path = path.path / (name + extension);

      if (fs::exists(new_path)) {
        if (fs::is_directory(new_path)) {
          throw std::runtime_error(new_path.string() + " is "
                                   "a directory");
        } else {
          this->istream = std::ifstream(new_path);
        }
      }

      this->ostream = std::ofstream(new_path, std::ios_base::out |
                                    (truncate ?
                                     std::ios_base::trunc : std::ios_base::app));

      if (!this->ostream) {
        throw std::runtime_error("Could not open " +
                                 new_path.string() +
                                 " for writing!");
      }

      fs::path metadata_path = path.path / ("meta_" + this->name + ".json");

      if (fs::exists(metadata_path)) {
        std::ifstream metadata_stream(metadata_path);

        if (!metadata_stream) {
          throw std::runtime_error("Could not open " +
                                 (path.path / ("meta_" + this->name + ".json")).string() +
                                 " for reading!");
        }

        std::string json_str((std::istreambuf_iterator<char>(metadata_stream)),
                             std::istreambuf_iterator<char>());
        this->metadata = nlohmann::json::parse(json_str);
      }
    }

    File &operator=(File &&source) {
      this->path = source.path;
      this->name = source.name;
      this->istream = std::move(source.istream);
      this->ostream = std::move(source.ostream);
      this->metadata.swap(source.metadata);
      return *this;
    }

    /**
       Gets an input stream corresponding to the file.

       @return Input stream.
    */
    std::ifstream &get_istream() {
      return this->istream;
    }

    /**
       Gets an output stream corresponding to the file.

       @return Output stream.
    */
    std::ofstream &get_ostream() {
      return this->ostream;
    }

    /**
       Saves metadata to disk, separately from the file itself.
    */
    void save_metadata() {
      fs::path metadata_path = path.path / ("meta_" + this->name + ".json");

      std::ofstream metadata_stream(metadata_path);

      if (!metadata_stream) {
        throw std::runtime_error("Could not open " + metadata_path.string() +
                                 " for writing!");
      }

      metadata_stream << this->metadata.dump() << std::endl;
    }
  };

  template<typename T>
  concept is_pair = requires {
    typename T::first_type;
    typename T::second_type;
  } && std::is_same_v<T, std::pair<typename T::first_type,
                                   typename T::second_type> >;

  template<typename T>
  concept is_tuple = requires {
    std::tuple_size<T>::value;
  };

  /**
     This class represents an array of arbitrary values saved to a file
     and with metadata attached to it (thanks to inheriting from File which
     inherits from ObjectWithMetadata) and saved separately.
  */
  template<class T>
  class Array : public File {
  private:
    std::vector<T> vec;

  public:
    /**
       Constructs an Array object.

       @param path Path to a directory where the array is.
       @param name Name of the array.
    */
    Array(Path &path, std::string name) : File(path, name, ".dat", false) {
      while (this->istream && !this->istream.eof()) {
        T val;

        if constexpr (is_pair<T>) {
          this->istream >> val.first;
          this->istream >> val.second;
        } else if constexpr (is_tuple<T>) {
          hana::for_each(val, [&](auto &item) {
            this->istream >> item;
          });
        } else {
          this->istream >> val;
        }

        if (this->istream) {
          vec.push_back(val);
        }
      }
    }

    /**
       Accesses the index-th element of the array.

       @param index Array index to access.

       @return index-th element of the array.
    */
    T operator[](int index) {
      return this->vec[index];
    }

    /**
       Gets the current array size.

       @return Array size.
    */
    int size() {
      return this->vec.size();
    }

    /**
       Pushes a new element to the end of the array and saves
       the new array to disk.

       @param val Value to push.
    */
    void push_back(T val) {
      this->vec.push_back(val);

      if constexpr (is_pair<T>) {
        this->ostream << val.first << " " << val.second << std::endl;
      } else if constexpr (is_tuple<T>) {
        std::stringstream str_stream;
        hana::for_each(val, [&](auto &item) {
          str_stream << item << " ";
        });

        std::string to_write = str_stream.str();
        to_write = to_write.substr(0, to_write.length() - 1);

        this->ostream << to_write << std::endl;
      } else {
        this->ostream << val << std::endl;
      }
    }
  };
}

#endif
