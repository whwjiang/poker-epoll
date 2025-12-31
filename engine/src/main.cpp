#include <csignal>
#include <cstdlib>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <print>
#include <string>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unordered_set>
#include <unistd.h>

constexpr int PORT = 65432;
constexpr int MAX_EVENTS = 64;
constexpr int BUF_SIZE = 1024;

int set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

struct Conn {
  int fd;
  std::string out;
};

struct ServerState {
  int epfd;
  int listenfd;
  std::unordered_set<Conn *> conns;
};

volatile sig_atomic_t g_stop = 0;

void handle_sigint(int) {
  g_stop = 1;
}

void close_conn(ServerState &state, Conn *c) {
  epoll_ctl(state.epfd, EPOLL_CTL_DEL, c->fd, nullptr);
  close(c->fd);
  state.conns.erase(c);
  std::println("closed {}", c->fd);
  delete c;
}

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

  ServerState state{epfd, listenfd, {}};

  epoll_event ev{};
  ev.events = EPOLLIN | EPOLLET;
  ev.data.fd = listenfd;
  epoll_ctl(epfd, EPOLL_CTL_ADD, listenfd, &ev);

  epoll_event events[MAX_EVENTS];

  while (!g_stop) {
    int n = epoll_wait(epfd, events, MAX_EVENTS, -1);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      exit(1);
    }
    std::println("new batch");
    for (int i = 0; i < n; ++i) {
      auto &e = events[i];

      /* Error path */
      if (e.events & (EPOLLERR | EPOLLHUP)) {
        if (e.data.fd != listenfd) {
          close(e.data.fd);
        }
        continue;
      }

      /* New connections */
      if (e.data.fd == listenfd) {
        while (true) {
          int cfd = accept(listenfd, nullptr, nullptr);
          if (cfd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
              break;
            break;
          }

          set_nonblocking(cfd);
          Conn *c = new Conn{cfd, {}};
          state.conns.insert(c);

          epoll_event cev{};
          cev.events = EPOLLIN | EPOLLET;
          cev.data.ptr = c;
          epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &cev);
          std::println("connected {}", cfd);
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
            close_conn(state, c);
            goto next_event;
          }
          if (r < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
              break;
            close_conn(state, c);
            goto next_event;
          }
          c->out.append(buf, r);
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
            close_conn(state, c);
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
        if (!c->out.empty())
          nev.events |= EPOLLOUT;
        epoll_ctl(epfd, EPOLL_CTL_MOD, c->fd, &nev);
      }

    next_event:;
    }
  }

  while (!state.conns.empty()) {
    auto it = state.conns.begin();
    close_conn(state, *it);
  }
  close(state.listenfd);
  close(state.epfd);
}
