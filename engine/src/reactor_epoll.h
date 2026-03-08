#pragma once

#include <vector>

#include "reactor_base.h"

namespace poker {

class EpollReactor : public ReactorBase<EpollReactor> {
public:
  EpollReactor();
  ~EpollReactor();

  auto valid() const -> bool;

  auto add_impl(int fd, ReactorEvent events, void *user_data) -> int;
  auto mod_impl(int fd, ReactorEvent events, void *user_data) -> int;
  auto del_impl(int fd) -> int;
  auto wait_impl(std::span<ReadyEvent> events, int timeout_ms) -> int;

private:
  int epoll_fd_;
};

} // namespace poker
