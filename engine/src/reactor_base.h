#pragma once

#include <cstdint>
#include <span>

namespace poker {

enum class ReactorEvent : uint32_t {
  none = 0,
  read = 1U << 0,
  write = 1U << 1,
  edge_triggered = 1U << 2,
  error = 1U << 3,
  hangup = 1U << 4,
};

constexpr auto operator|(ReactorEvent lhs, ReactorEvent rhs) -> ReactorEvent {
  return static_cast<ReactorEvent>(static_cast<uint32_t>(lhs) |
                                   static_cast<uint32_t>(rhs));
}

constexpr auto operator&(ReactorEvent lhs, ReactorEvent rhs) -> ReactorEvent {
  return static_cast<ReactorEvent>(static_cast<uint32_t>(lhs) &
                                   static_cast<uint32_t>(rhs));
}

constexpr auto has_flag(ReactorEvent events, ReactorEvent flag) -> bool {
  return (events & flag) != ReactorEvent::none;
}

struct ReadyEvent {
  ReactorEvent events{ReactorEvent::none};
  void *user_data{nullptr};
};

template <typename Derived> class ReactorBase {
public:
  auto add(int fd, ReactorEvent events, void *user_data) -> int {
    return derived().add_impl(fd, events, user_data);
  }

  auto mod(int fd, ReactorEvent events, void *user_data) -> int {
    return derived().mod_impl(fd, events, user_data);
  }

  auto del(int fd) -> int { return derived().del_impl(fd); }

  auto wait(std::span<ReadyEvent> events, int timeout_ms) -> int {
    return derived().wait_impl(events, timeout_ms);
  }

private:
  auto derived() -> Derived & { return static_cast<Derived &>(*this); }
};

} // namespace poker
