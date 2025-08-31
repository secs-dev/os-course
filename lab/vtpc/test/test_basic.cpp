#include <exception>
#include <iostream>
#include <memory>
#include <utility>

#include "cmp_file.hpp"
#include "file.hpp"

auto main() -> int try {
  auto libc = vt::file::open_libc("/tmp/a");
  auto vtpc = vt::file::open_vtpc("/tmp/b");
  vt::cmp_file cmp(std::move(libc), std::move(vtpc));

  std::string_view message = "Hello, World!";
  cmp.write(message);
  cmp.sync();
  cmp.seek(0);
  std::cout << cmp.read(message.size()) << '\n';

  return 0;
} catch (const std::exception& e) {
  std::cerr << "exception: " << e.what() << '\n';
  return 1;
}
