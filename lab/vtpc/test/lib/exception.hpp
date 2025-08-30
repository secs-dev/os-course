#pragma once

#include <exception>
#include <sstream>

namespace vt {

class exception : public std::exception {
public:
  exception() = default;

  auto what() const noexcept -> const char* override {
    message_ = buffer_.str();
    return message_.c_str();
  }

  template <class T>
  inline void Append(const T& t) {
    buffer_ << t;
  }

private:
  mutable std::stringstream buffer_;
  mutable std::string message_;
};

template <class E, class T>
static inline auto operator<<(E&& e [[clang::lifetimebound]], const T& t)
    -> std::enable_if_t<std::is_base_of_v<exception, std::decay_t<E>>, E&&> {
  e.Append(t);
  return std::forward<E>(e);
}

}  // namespace vt