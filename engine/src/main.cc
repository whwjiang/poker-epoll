#include <csignal>
#include <cstdlib>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <print>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "server.h"

constexpr int PORT = 65432;
constexpr int MAX_EVENTS = 64;
constexpr int BUF_SIZE = 1024;

int set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

volatile sig_atomic_t g_stop = 0;

void handle_sigint(int) { g_stop = 1; }

int main() {
  std::signal(SIGINT, handle_sigint);

  int listenfd = socket(AF_INET, SOCK_STREAM, 0);
  if (listenfd < 0)
    exit(1);

  int opt = 1;
  setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(PORT);

  if (bind(listenfd, (sockaddr *)&addr, sizeof(addr)) < 0)
    exit(1);
  if (listen(listenfd, SOMAXCONN) < 0)
    exit(1);

  set_nonblocking(listenfd);

  int epfd = epoll_create1(0);
  if (epfd < 0)
    exit(1);

  Server state(epfd, listenfd);

  epoll_event ev{};
  ev.events = EPOLLIN | EPOLLET;
  ev.data.fd = state.listenfd();
  epoll_ctl(epfd, EPOLL_CTL_ADD, state.listenfd(), &ev);

  epoll_event events[MAX_EVENTS];

  std::println("Started server on port {}", PORT);

  while (!g_stop) {
    int n = epoll_wait(state.epfd(), events, MAX_EVENTS, -1);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      exit(1);
    }
    std::println("New batch of events");
    for (int i = 0; i < n; ++i) {
      auto &e = events[i];

      /* Error path */
      if (e.events & (EPOLLERR | EPOLLHUP)) {
        if (e.data.fd != state.listenfd()) {
          close(e.data.fd);
        }
        continue;
      }

      /* New connections */
      if (e.data.fd == state.listenfd()) {
        while (true) {
          // max players/tables will be limiting factor here
          int cfd = accept(state.listenfd(), nullptr, nullptr);
          if (cfd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
              break;
            // potentially handle other errno's
            break;
          }
          set_nonblocking(cfd);
          auto cr = state.handle_connect(cfd);
          state.push_table(cr.conn->table_id,
                           cr.result ? Outbound{*cr.result}
                                     : Outbound{cr.result.error()});
        }
        continue;
      }

      /* Client socket */
      Conn *c = static_cast<Conn *>(e.data.ptr);

      /* Read */
      if (e.events & EPOLLIN) {
        char buf[BUF_SIZE];
        while (true) {
          ssize_t r = read(c->fd, buf, sizeof(buf));
          if (r == 0) {
            state.handle_close(c->player_id);
            goto next_event;
          }
          if (r < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
              break;
            state.handle_close(c->player_id);
            goto next_event;
          }
          c->in.append(buf, r);
          std::println("read {} bytes from {}", r, c->fd);
        }
      }

      /* Write */
      if (e.events & EPOLLOUT) {
        while (!c->out.empty()) {
          ssize_t w = write(c->fd, c->out.data(), c->out.size());
          if (w < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
              break;
            state.handle_close(c->player_id);
            goto next_event;
          }
          c->out.erase(0, w);
          std::println("wrote {} bytes to {}", w, c->fd);
        }
      }

      /* Update interest mask */
      {
        epoll_event nev{};
        nev.data.ptr = c;
        nev.events = EPOLLIN | EPOLLET;
        if (!c->out.empty()) {
          nev.events |= EPOLLOUT;
        } else if (c->is_dead) {
          // close the connection if we have written all bytes
          // and the connection is dead
          state.handle_close(c->player_id);
          goto next_event;
        }
        epoll_ctl(state.epfd(), EPOLL_CTL_MOD, c->fd, &nev);
      }

    next_event:;
    }
  }
}
