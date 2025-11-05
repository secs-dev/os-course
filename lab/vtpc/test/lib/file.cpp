#include "file.hpp"

#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string_view>
#include <utility>

#include "exception.hpp"

extern "C" {
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>

#include "vtpc.h"
}

namespace vt {

constexpr auto flags = O_RDWR | O_CREAT;
constexpr auto access = 0777;

file_exception::file_exception(ssize_t code) : code_(code) {
}

auto file_exception::code() const -> ssize_t {
  return code_;
}

struct io {
  std::function<int(const char* path, int mode, int access)> open;
  std::function<int(int fd)> close;
  std::function<ssize_t(int fd, void* buf, size_t count)> read;
  std::function<ssize_t(int fd, const void* buf, size_t count)> write;
  std::function<off_t(int fd, off_t offset, int whence)> lseek;
  std::function<int(int fd)> fsync;
};

template <class A, class T>
void robust_do(A action, int fd, T* buf, size_t count) {
  using B = std::conditional_t<
      std::is_const_v<std::remove_pointer_t<T>>,
      const char,
      char>;

  size_t total = 0;
  while (total < count) {
    const size_t tail_count = count - total;
    B* tail_buf = reinterpret_cast<B*>(buf) + total;  // NOLINT
    const ssize_t local = action(fd, tail_buf, tail_count);
    if (local < 0) {
      throw vt::file_exception(local)
          << "failed to read/write " << count << " bytes from file with fd "
          << fd << ": " << strerror(errno);  // NOLINT(concurrency-mt-unsafe);
    }
    if (local == 0) {
      throw vt::file_exception(0)
          << "failed to read/write " << count << " bytes from file with fd "
          << fd << ": " << "EOF after reading " << total << " bytes";
    }

    total += local;
  }
}

class io_file final : public file {
public:
  explicit io_file(std::string_view path, io io)
      : fd_(io.open(path.data(), flags, access)), io_(std::move(io)) {
    if (fd_ < 0) {
      throw vt::file_exception(fd_)
          << "failed to open file '" << path << "'" << ": "
          << strerror(errno);  // NOLINT(concurrency-mt-unsafe);
    }
  }

  ~io_file() override {
    (void)io_.close(fd_);
  }

  void read(char* buffer, size_t count) override {
    robust_do(io_.read, fd_, buffer, count);
  }

  void write(const char* buffer, size_t count) override {
    robust_do(io_.write, fd_, buffer, count);
  }

  void seek(off_t offset) override {
    if (io_.lseek(fd_, offset, SEEK_SET) == -1) {
      throw vt::file_exception(-1)
          << "failed to seek to offset " << offset << "file with fd " << fd_
          << ": " << strerror(errno);  // NOLINT(concurrency-mt-unsafe)
    }
  }

  void sync() override {
    if (io_.fsync(fd_) == -1) {
      throw vt::file_exception(-1)
          << "failed to fsync file with fd " << fd_ << ": "
          << strerror(errno);  // NOLINT(concurrency-mt-unsafe)
    }
  }

private:
  int fd_;
  io io_;
};

auto file::open_libc(std::string_view path) -> std::unique_ptr<file> {
  io io = {
      .open = ::open,
      .close = ::close,
      .read = ::read,
      .write = ::write,
      .lseek = ::lseek,
      .fsync = ::fsync,
  };

  return std::make_unique<io_file>(path, std::move(io));
}

auto file::open_vtpc(std::string_view path) -> std::unique_ptr<file> {
  io io = {
      .open = ::vtpc_open,
      .close = ::vtpc_close,
      .read = ::vtpc_read,
      .write = ::vtpc_write,
      .lseek = ::vtpc_lseek,
      .fsync = ::vtpc_fsync,
  };

  return std::make_unique<io_file>(path, std::move(io));
}

}  // namespace vt
