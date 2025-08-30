#include "cmp_file.hpp"

#include <optional>

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
}

cmp_file::cmp_file(std::unique_ptr<file> lhs, std::unique_ptr<file> rhs)
    : lhs_(std::move(lhs)), rhs_(std::move(rhs)) {
}

auto cmp_file::read(char* buffer, size_t count) -> void {
  Compare(
      [buffer, count, this] { lhs_->read(buffer, count); },
      [buffer, count, this] { lhs_->read(buffer, count); }
  );
}

auto cmp_file::write(const char* buffer, size_t count) -> void {
  Compare(
      [buffer, count, this] { lhs_->write(buffer, count); },
      [buffer, count, this] { lhs_->write(buffer, count); }
  );
}

auto cmp_file::seek(off_t offset) -> void {
  Compare(
      [offset, this] { lhs_->seek(offset); },
      [offset, this] { lhs_->seek(offset); }
  );
}

auto cmp_file::sync() -> void {
  Compare([this] { lhs_->sync(); }, [this] { rhs_->sync(); });
}

}  // namespace vt
