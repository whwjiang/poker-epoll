#include "reactor_epoll.h"

#include <sys/epoll.h>
#include <unistd.h>

namespace poker {

namespace {

auto to_epoll_events(ReactorEvent events) -> uint32_t {
  uint32_t native_events = 0;
  if (has_flag(events, ReactorEvent::read)) {
    native_events |= EPOLLIN;
  }
  if (has_flag(events, ReactorEvent::write)) {
    native_events |= EPOLLOUT;
  }
  if (has_flag(events, ReactorEvent::edge_triggered)) {
    native_events |= EPOLLET;
  }
  return native_events;
}

auto from_epoll_events(uint32_t native_events) -> ReactorEvent {
  ReactorEvent events = ReactorEvent::none;
  if ((native_events & EPOLLIN) != 0U) {
    events = events | ReactorEvent::read;
  }
  if ((native_events & EPOLLOUT) != 0U) {
    events = events | ReactorEvent::write;
  }
  if ((native_events & EPOLLET) != 0U) {
    events = events | ReactorEvent::edge_triggered;
  }
  if ((native_events & EPOLLERR) != 0U) {
    events = events | ReactorEvent::error;
  }
  if ((native_events & EPOLLHUP) != 0U) {
    events = events | ReactorEvent::hangup;
  }
  return events;
}

} // namespace

EpollReactor::EpollReactor() : epoll_fd_(epoll_create1(0)) {}

EpollReactor::~EpollReactor() {
  if (epoll_fd_ >= 0) {
    close(epoll_fd_);
  }
}

auto EpollReactor::valid() const -> bool { return epoll_fd_ >= 0; }

auto EpollReactor::add_impl(int fd, ReactorEvent events, void *user_data)
    -> int {
  epoll_event event{};
  event.events = to_epoll_events(events);
  event.data.ptr = user_data;
  return epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &event);
}

auto EpollReactor::mod_impl(int fd, ReactorEvent events, void *user_data)
    -> int {
  epoll_event event{};
  event.events = to_epoll_events(events);
  event.data.ptr = user_data;
  return epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &event);
}

auto EpollReactor::del_impl(int fd) -> int {
  return epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
}

auto EpollReactor::wait_impl(std::span<ReadyEvent> events, int timeout_ms)
    -> int {
  std::vector<epoll_event> native_events(events.size());
  const int count =
      epoll_wait(epoll_fd_, native_events.data(), native_events.size(), timeout_ms);
  if (count <= 0) {
    return count;
  }
  for (int i = 0; i < count; ++i) {
    events[static_cast<std::size_t>(i)] = ReadyEvent{
        .events = from_epoll_events(native_events[static_cast<std::size_t>(i)].events),
        .user_data = native_events[static_cast<std::size_t>(i)].data.ptr,
    };
  }
  return count;
}

} // namespace poker
