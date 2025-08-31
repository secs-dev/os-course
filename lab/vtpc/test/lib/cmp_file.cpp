#include "cmp_file.hpp"

#include <sys/types.h>

#include <cstddef>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "exception.hpp"
#include "file.hpp"

namespace vt {

template <class A, class B>
void Compare(A lhs, B rhs) {
  std::optional<vt::file_exception> lhs_ex;
  std::optional<vt::file_exception> rhs_ex;

  try {
    lhs();
  } catch (const vt::file_exception& e) {
    lhs_ex = vt::file_exception(e.code()) << e.what();
  }

  try {
    rhs();
  } catch (const vt::file_exception& e) {
    rhs_ex = vt::file_exception(e.code()) << e.what();
  }

  if (lhs_ex && !rhs_ex) {
    throw vt::cmp_file_exception() << "(FAIL, OK): " << lhs_ex->what();
  }
  if (!lhs_ex && rhs_ex) {
    throw vt::cmp_file_exception() << "(OK, FAIL): " << rhs_ex->what();
  }
  if (lhs_ex && rhs_ex && lhs_ex->code() != rhs_ex->code()) {
    throw vt::cmp_file_exception()
        << "(FAIL, FAIL), but codes differ: " << lhs_ex->what() << ", "
        << rhs_ex->what();
  }
  if (lhs_ex && rhs_ex) {
    const vt::file_exception& e = *lhs_ex;
    throw vt::file_exception(e.code()) << e.what();
  }
}

cmp_file::cmp_file(std::unique_ptr<file> lhs, std::unique_ptr<file> rhs)
    : lhs_(std::move(lhs)), file_(std::move(rhs)) {
}

auto cmp_file::read(char* buffer, size_t count) -> void {
  std::string lhs(count, ' ');
  std::string rhs(count, ' ');
  Compare(
      [&] { lhs_->read(lhs.data(), count); },
      [&] { file_->read(rhs.data(), count); }
  );
  if (lhs != rhs) {
    throw vt::cmp_file_exception() << "'" << lhs << "' != '" << rhs << "'";
  }
  memcpy(buffer, lhs.data(), count);
}

auto cmp_file::write(const char* buffer, size_t count) -> void {
  Compare(
      [&] { lhs_->write(buffer, count); }, [&] { file_->write(buffer, count); }
  );
}

auto cmp_file::seek(off_t offset) -> void {
  Compare([&] { lhs_->seek(offset); }, [&] { file_->seek(offset); });
}

auto cmp_file::sync() -> void {
  Compare([&] { lhs_->sync(); }, [this] { file_->sync(); });
}

}  // namespace vt
