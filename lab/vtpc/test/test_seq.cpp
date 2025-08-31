#include <cstddef>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

#include "cmp_file.hpp"
#include "file.hpp"

auto main() -> int try {
  constexpr size_t count = 1024;

  auto libc = vt::file::open_libc("/tmp/a");
  auto vtpc = vt::file::open_vtpc("/tmp/b");
  vt::cmp_file cmp(std::move(libc), std::move(vtpc));

  cmp.seek(0);
  for (size_t i = 0; i < count; ++i) {
    std::string text = std::to_string(i);
    cmp.write(text);
  }

  cmp.seek(0);
  for (size_t i = 0; i < count; ++i) {
    std::string expected = std::to_string(i);
    std::string actual = cmp.read(expected.size());
    if (expected != actual) {
      throw vt::exception() << "'" << expected << "' != '" << actual << "'";
    }
  }

  cmp.sync();

  return 0;
} catch (const std::exception& e) {
  std::cerr << "exception: " << e.what() << '\n';
  return 1;
}
