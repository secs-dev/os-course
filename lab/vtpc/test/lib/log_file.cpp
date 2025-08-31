#include "log_file.hpp"

#include <sys/types.h>

#include <cstddef>
#include <iostream>
#include <memory>
#include <utility>

#include "file.hpp"

namespace vt {

log_file::log_file(std::unique_ptr<file> file) : file_(std::move(file)) {
}

auto log_file::read(char* buffer, size_t count) -> void {
  std::cerr << "[vt] read count " << count << "\n";
  file_->read(buffer, count);
}

auto log_file::write(const char* buffer, size_t count) -> void {
  std::cerr << "[vt] write count " << count << "\n";
  file_->write(buffer, count);
}

auto log_file::seek(off_t offset) -> void {
  std::cerr << "[vt] seek offset " << offset << "\n";
  file_->seek(offset);
}

auto log_file::sync() -> void {
  std::cerr << "[vt] sync\n";
  file_->sync();
}

}  // namespace vt
