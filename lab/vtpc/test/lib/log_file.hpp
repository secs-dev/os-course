#pragma once

#include <sys/types.h>

#include <cstddef>
#include <memory>

#include "file.hpp"

namespace vt {

class log_file final : public file {
public:
  using file::read;
  using file::write;

  explicit log_file(std::unique_ptr<file> file);
  ~log_file() override = default;

  auto read(char* buffer, size_t count) -> void override;
  auto write(const char* buffer, size_t count) -> void override;
  auto seek(off_t offset) -> void override;
  auto sync() -> void override;

private:
  std::unique_ptr<file> file_;
};

}  // namespace vt
