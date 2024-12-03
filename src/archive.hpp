// AdaptivePerf: comprehensive profiling tool based on Linux perf
// Copyright (C) CERN. See LICENSE for details.

#ifndef ARCHIVE_HPP_
#define ARCHIVE_HPP_

#include "server/socket.hpp"
#include <filesystem>
#include <istream>
#include <archive.h>
#include <archive_entry.h>

namespace aperf {
  namespace fs = std::filesystem;

  /**
     A class describing an archive file to be written to.
  */
  class Archive {
  private:
    struct archive *arch;
    struct archive_entry *arch_entry;
    unsigned int buf_size;
    std::unique_ptr<Connection> conn;

  public:
    /**
       Constructs an Archive object and opens a file for writing.

       When the object is destructed, the file is guaranteed to be properly
       closed, but without any promises about whether all remaining data
       have been written. Therefore, you should explicitly call close()
       (which may throw an exception) before the object goes out of scope.

       @param path     The path to an archive file to be created. The file
                       must not exist yet.
       @param buf_size A number of bytes of the internal buffers.
    */
    Archive(fs::path path, unsigned int buf_size = 1024);

    /**
       Constructs an Archive object with all archive file data to be sent
       through a Connection object.

       When the object is destructed, the connection is guaranteed to be
       properly closed and all remaining data are guaranteed to have been written.
       Therefore, you *DO NOT* need to call close() before the object goes out of
       scope or anywhere else.

       @param conn     A Connection object which all archive file data
                       will be sent through.
       @param padding  Whether padding is allowed to be added to the last block
                       of the archive file data if necessary. If you are not
                       sure, this should be set to true (default).
       @param buf_size A number of bytes of the internal buffers.
    */
    Archive(std::unique_ptr<Connection> &conn, bool padding = true,
            unsigned int buf_size = 1024);

    /**
       Adds a file to the root of the archive file.

       @param filename The name of a file that will appear inside the archive.
       @param path     The path to a file to be added to the archive. The file
                       must be a regular file and not a directory.
    */
    void add_file(std::string filename, fs::path path);

    /**
       Saves data extracted from a stream to the root of the archive file
       as a regular file.

       @param filename The name of a file that will appear inside the archive.
       @param stream   A stream containing data to be saved to the archive.
       @param size     A number of bytes to save to the archive. If it's larger
                       than the actual file size, the entire file will be added,
                       followed by padding with zeroes.
    */
    void add_file_stream(std::string filename, std::istream &stream,
                         unsigned int size);

    /**
       Closes the file/connection and makes sure that all remaining data have
       been written.
    */
    void close();

    ~Archive();

    class Exception : public std::exception {
    private:
      const char *message;

    public:
      Exception(struct archive *arch) {
        this->message = archive_error_string(arch);
      }

      Exception() {
        this->message = typeid(*this).name();
      }

      const char *what() const noexcept override {
        return this->message;
      }
    };

    class InitException : public Exception { using Exception::Exception; };
    class FileExistsException : public Exception { using Exception::Exception; };
    class FileOpenException : public Exception { using Exception::Exception; };
    class FileIOException : public Exception { using Exception::Exception; };
    class CloseException : public Exception { using Exception::Exception; };
    class AlreadyClosedException : public Exception { using Exception::Exception; };
    class FileDoesNotExistException : public Exception { using Exception::Exception; };
    class NotRegularFileException : public Exception { using Exception::Exception; };
  };
};

#endif
