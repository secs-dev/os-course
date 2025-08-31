#pragma once

#include <exception>
#include <sstream>
#include <type_traits>

namespace vt {

class exception : public std::exception {
public:
  exception() = default;

  [[nodiscard]]
  auto what() const noexcept -> const char* override {
    message_ = buffer_.str();
    return message_.c_str();
  }

  template <class T>
  void Append(const T& t) {
    buffer_ << t;
  }

private:
  mutable std::stringstream buffer_;
  mutable std::string message_;
};

template <class E, class T>
  requires std::is_base_of_v<exception, std::decay_t<E>>
static auto operator<<(E&& e [[clang::lifetimebound]], const T& t) -> E&& {
  e.Append(t);
  return std::forward<E>(e);
}

}  // namespace vt