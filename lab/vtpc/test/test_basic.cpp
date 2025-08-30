#include <exception>
#include <iostream>
#include <memory>

#include "cmp_file.hpp"
#include "file.hpp"

auto write(vt::file& file, std::string_view text) -> void {
  file.write(text.data(), text.size());
}

auto read(vt::file& file, size_t size) -> std::string {
  std::string text(size, 0);
  file.read(text.data(), size);
  return text;
}

auto main() -> int try {
  auto libc = vt::file::open_libc("/tmp/a");
  auto vtpc = vt::file::open_vtpc("/tmp/b");
  vt::cmp_file cmp(std::move(libc), std::move(vtpc));

  std::string_view message = "Hello, World!";
  write(cmp, message);
  cmp.sync();
  cmp.seek(0);
  std::cout << read(cmp, message.size()) << '\n';

  return 0;
} catch (const std::exception& e) {
  std::cerr << "exception: " << e.what() << '\n';
  return 1;
}
