#pragma once

#include <sys/types.h>

#include <cstddef>
#include <memory>

#include "exception.hpp"
#include "file.hpp"

namespace vt {

class cmp_file_exception : public vt::exception {};

class cmp_file final : public file {
public:
  using file::read;
  using file::write;

  cmp_file(std::unique_ptr<file> lhs, std::unique_ptr<file> rhs);
  ~cmp_file() override = default;

  auto read(char* buffer, size_t count) -> void override;
  auto write(const char* buffer, size_t count) -> void override;
  auto seek(off_t offset) -> void override;
  auto sync() -> void override;

private:
  std::unique_ptr<file> lhs_;
  std::unique_ptr<file> file_;
};

}  // namespace vt
