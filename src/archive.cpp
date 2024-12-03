// AdaptivePerf: comprehensive profiling tool based on Linux perf
// Copyright (C) CERN. See LICENSE for details.

#include "archive.hpp"
#include <fstream>

namespace aperf {
  Archive::Archive(fs::path path, unsigned int buf_size) {
    this->arch = nullptr;
    this->arch_entry = nullptr;
    this->buf_size = buf_size;

    if (fs::exists(path)) {
      throw Archive::FileExistsException();
    }

    this->arch = archive_write_new();

    if (!this->arch) {
      throw Archive::InitException();
    }

    this->arch_entry = archive_entry_new();

    if (!this->arch_entry) {
      throw Archive::InitException();
    }

    if (archive_write_set_format_zip(this->arch) != ARCHIVE_OK) {
      throw Archive::InitException();
    }

    if (archive_write_set_options(this->arch, "compression-level=9") !=
        ARCHIVE_OK) {
      throw Archive::InitException();
    }

    if (archive_write_open_filename(this->arch, path.c_str()) != ARCHIVE_OK) {
      throw Archive::FileOpenException(this->arch);
    }
  }

  Archive::Archive(std::unique_ptr<Connection> &conn, bool padding,
                   unsigned int buf_size) {
    this->arch = nullptr;
    this->arch_entry = nullptr;
    this->buf_size = buf_size;
    this->conn = std::move(conn);

    this->arch = archive_write_new();

    if (!this->arch) {
      throw Archive::InitException();
    }

    this->arch_entry = archive_entry_new();

    if (!this->arch_entry) {
      throw Archive::InitException();
    }

    if (archive_write_set_format_zip(this->arch) != ARCHIVE_OK) {
      throw Archive::InitException();
    }

    if (archive_write_set_options(this->arch, "compression-level=9") !=
        ARCHIVE_OK) {
      throw Archive::InitException();
    }

    auto archive_open_padding =
      [](struct archive *arch, void *data) -> int {
        return archive_write_set_bytes_in_last_block(arch, 0);
      };

    auto archive_open_no_padding =
      [](struct archive *arch, void *data) -> int {
        return archive_write_set_bytes_in_last_block(arch, 1);
      };

    auto archive_return_ok =
      [](struct archive *arch, void *data) -> int {
        return ARCHIVE_OK;
      };

    auto archive_write =
      [](struct archive *arch, void *data, const void *buf,
         size_t len) -> la_ssize_t {
        try {
          ((Connection *)data)->write(len, (char *)buf);
          return len;
        } catch (std::exception &e) {
          archive_set_error(arch, ARCHIVE_FAILED, "%s", e.what());
          return -1;
        }
      };

    if (archive_write_open2(this->arch, this->conn.get(),
                            padding ?
                            archive_open_padding : archive_open_no_padding,
                            archive_write,
                            archive_return_ok,
                            archive_return_ok) != ARCHIVE_OK) {
      throw Archive::FileOpenException(this->arch);
    }
  }

  void Archive::add_file(std::string filename, fs::path path) {
    if (!this->arch) {
      throw Archive::AlreadyClosedException();
    }

    if (!fs::exists(path)) {
      throw Archive::FileDoesNotExistException();
    }

    if (!fs::is_regular_file(path)) {
      throw Archive::NotRegularFileException();
    }

    archive_entry_clear(this->arch_entry);

    std::ifstream stream(path);

    if (!stream) {
      throw Archive::FileOpenException();
    }

    archive_entry_set_pathname(this->arch_entry, filename.c_str());
    archive_entry_set_size(this->arch_entry, fs::file_size(path));
    archive_entry_set_filetype(this->arch_entry, AE_IFREG);
    archive_entry_set_perm(this->arch_entry, 0644);

    auto now = std::chrono::system_clock::now().time_since_epoch();
    time_t secs = std::chrono::duration_cast<std::chrono::seconds>(now).count();

    archive_entry_set_atime(this->arch_entry, secs, 0);
    archive_entry_set_birthtime(this->arch_entry, secs, 0);
    archive_entry_set_ctime(this->arch_entry, secs, 0);
    archive_entry_set_mtime(this->arch_entry, secs, 0);

    if (archive_write_header(this->arch, this->arch_entry) != ARCHIVE_OK) {
      throw Archive::FileIOException(this->arch);
    }

    char buf[this->buf_size];
    int bytes_read;

    while (!stream.fail() &&
           (bytes_read = stream.readsome(buf, this->buf_size)) > 0) {
      int bytes_written = archive_write_data(this->arch, buf, bytes_read);
      if (bytes_written == -1) {
        throw Archive::FileIOException(this->arch);
      } else if (bytes_written != bytes_read) {
        throw Archive::FileIOException();
      }
    }

    if (stream.fail()) {
      throw Archive::FileIOException();
    }
  }

  void Archive::add_file_stream(std::string filename, std::istream &stream,
                                unsigned int size) {
    if (!this->arch) {
      throw Archive::AlreadyClosedException();
    }

    archive_entry_clear(this->arch_entry);

    archive_entry_set_pathname(this->arch_entry, filename.c_str());
    archive_entry_set_size(this->arch_entry, size);
    archive_entry_set_filetype(this->arch_entry, AE_IFREG);
    archive_entry_set_perm(this->arch_entry, 0644);

    auto now = std::chrono::system_clock::now().time_since_epoch();
    time_t secs = std::chrono::duration_cast<std::chrono::seconds>(now).count();

    archive_entry_set_atime(this->arch_entry, secs, 0);
    archive_entry_set_birthtime(this->arch_entry, secs, 0);
    archive_entry_set_ctime(this->arch_entry, secs, 0);
    archive_entry_set_mtime(this->arch_entry, secs, 0);

    if (archive_write_header(this->arch, this->arch_entry) != ARCHIVE_OK) {
      throw Archive::FileIOException(this->arch);
    }

    char buf[this->buf_size];
    int bytes_read;
    int total_bytes = 0;

    while (!stream.fail() &&
           (bytes_read = stream.readsome(buf, std::min(std::max(0, (int)size - total_bytes), (int)this->buf_size))) > 0) {
      int bytes_written = archive_write_data(this->arch, buf, bytes_read);
      if (bytes_written == -1) {
        throw Archive::FileIOException(this->arch);
      } else if (bytes_written != bytes_read) {
        throw Archive::FileIOException();
      }

      total_bytes += bytes_read;
    }

    if (stream.fail()) {
      throw Archive::FileIOException();
    }

    if (total_bytes < size) {
      int zeroes_written = 0;

      while (zeroes_written < size - total_bytes) {
        int to_write = (size - total_bytes - zeroes_written) % (this->buf_size + 1);

        for (int i = 0; i < to_write; i++) {
          buf[i] = 0;
        }

        int bytes_written = archive_write_data(this->arch, buf, to_write);
        if (bytes_written == -1) {
          throw Archive::FileIOException(this->arch);
        } else if (bytes_written != to_write) {
          throw Archive::FileIOException();
        }

        zeroes_written += to_write;
      }
    }
  }

  void Archive::close() {
    if (this->arch) {
      if (archive_write_close(this->arch) != ARCHIVE_OK) {
        throw Archive::CloseException(this->arch);
      }

      archive_write_free(this->arch);

      this->arch = nullptr;
    }
  }

  Archive::~Archive() {
    if (this->arch_entry) {
      archive_entry_free(this->arch_entry);
    }

    if (this->arch) {
      archive_write_free(this->arch);
    }
  }
};
