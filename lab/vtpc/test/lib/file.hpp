#pragma once

#include <sys/types.h>

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

#include "exception.hpp"

namespace vt {

class file_exception : public vt::exception {
public:
  explicit file_exception(ssize_t code);

  [[nodiscard]] auto code() const -> ssize_t;

private:
  ssize_t code_;
};

class file {
public:
  virtual ~file() = default;
  virtual auto read(char* buffer, size_t count) -> void = 0;
  virtual auto write(const char* buffer, size_t count) -> void = 0;
  virtual auto seek(off_t offset) -> void = 0;
  virtual auto sync() -> void = 0;

  auto write(std::string_view text) -> void {
    write(text.data(), text.size());
  }

  auto read(size_t size) -> std::string {
    std::string text(size, 0);
    read(text.data(), size);
    return text;
  }

  static auto open_libc(std::string_view path) -> std::unique_ptr<file>;
  static auto open_vtpc(std::string_view path) -> std::unique_ptr<file>;
};

}  // namespace vt
