#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <utility>

#include "cmp_file.hpp"
#include "file.hpp"
#include "log_file.hpp"

auto main() -> int try {
  constexpr size_t seed = 1;
  constexpr size_t steps = (1U << 16U);
  constexpr size_t size = (1U << 12U);
  constexpr size_t interval = 100;

  std::unique_ptr<vt::file> file = [] {
    auto libc = vt::file::open_libc("/tmp/a");
    auto vtpc = vt::file::open_vtpc("/tmp/b");
    auto cmp = std::make_unique<vt::cmp_file>(std::move(libc), std::move(vtpc));
    auto log = std::make_unique<vt::log_file>(std::move(cmp));
    return log;
  }();

  std::default_random_engine random(seed);  // NOLINT

  std::uniform_int_distribution<size_t> action_dist(0, 100);  // NOLINT
  std::uniform_int_distribution<off_t> offset_dist(0, size);
  std::uniform_int_distribution<size_t> batch_dist(0, size / 4);
  std::uniform_int_distribution<uint8_t> char_dist(0);

  const auto random_string = [&](size_t size) {
    std::string string(size, ' ');
    for (char& c : string) {
      c = static_cast<char>(char_dist(random));
    }
    return string;
  };

  file->seek(0);
  file->write(std::string(size, ' '));

  file->seek(0);
  for (size_t i = 0; i < steps; ++i) {
    if (i % interval == 0) {
      std::cerr << "i = " << i << '\n';
    }

    try {
      size_t point = action_dist(random);
      if (point < 40) {  // NOLINT
        size_t batch = batch_dist(random);
        file->read(batch);
      } else if (point < 75) {  // NOLINT
        size_t batch = batch_dist(random);
        file->write(random_string(batch));
      } else if (point < 95) {  // NOLINT
        file->seek(offset_dist(random));
      } else {
        file->sync();
      }
    } catch (vt::file_exception& e) {  // NOLINT
      // Do nothing
    }
  }

  return 0;
} catch (const std::exception& e) {
  std::cerr << "exception: " << e.what() << '\n';
  return 1;
}
