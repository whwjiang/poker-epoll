#include <csignal>
#include <cstdlib>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <array>
#include <vector>

#include "actions.pb.h"
#include "errors.h"
#include "server.h"
#include "spdlog/spdlog.h"

constexpr int PORT = 65432;
constexpr int MAX_EVENTS = 64;
constexpr int BUF_SIZE = 1024;

auto conn_interest(const Conn *conn) -> uint32_t {
  return static_cast<uint32_t>(EPOLLIN | EPOLLET) |
         (!conn->out.empty() ? static_cast<uint32_t>(EPOLLOUT) : 0U);
}

int set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

volatile sig_atomic_t g_stop = 0;

bool try_parse_frame(Conn *c, std::string &out_msg) {
  // Step 1: header
  if (c->in_size == 0) {
    if (c->in.size() < sizeof(uint32_t)) {
      // don't read the bytes until there is a header
      return false;
    }
    uint32_t net_len = 0;
    std::memcpy(&net_len, c->in.data(), sizeof(net_len));
    c->in_size = ntohl(net_len);
    c->in_off = sizeof(uint32_t);
  }

  // Step 2: body
  if (c->in.size() < c->in_off + c->in_size)
    return false;

  out_msg.assign(c->in.data() + c->in_off, c->in_size);
  c->in.erase(0, c->in_off + c->in_size);
  c->in_off = 0;
  c->in_size = 0;
  return true;
}

std::string action_to_string(const ::poker::v1::Action &action) {
  using Payload = ::poker::v1::Action::PayloadCase;
  switch (action.payload_case()) {
  case Payload::kFold:
    return "fold";
  case Payload::kBet:
    return "bet " + std::to_string(action.bet().amount());
  case Payload::kReady:
    return "ready";
  case Payload::PAYLOAD_NOT_SET:
  default:
    return "unknown";
  }
}

void handle_sigint(int) { g_stop = 1; }

void sync_table_interest(Server &state, int epfd, poker::TableId table_id);

void close_connection(Server &state, int epfd, Conn *conn) {
  const poker::TableId tid = conn->table_id;
  epoll_ctl(epfd, EPOLL_CTL_DEL, conn->fd, nullptr);
  state.handle_close(conn->player_id);
  if (tid != 0) {
    sync_table_interest(state, epfd, tid);
  }
}

void sync_table_interest(Server &state, int epfd, poker::TableId table_id) {
  for (Conn *conn : state.get_table_conns(table_id)) {
    if (conn->is_dead) {
      continue;
    }
    epoll_event event{};
    event.events = conn_interest(conn);
    event.data.ptr = conn;
    epoll_ctl(epfd, EPOLL_CTL_MOD, conn->fd, &event);
  }
}

auto handle_read(Server &state, int epfd, Conn *conn) -> bool {
  char buf[BUF_SIZE];
  while (true) {
    ssize_t r = read(conn->fd, buf, sizeof(buf));
    if (r == 0) {
      spdlog::info("Peer closed connection for player {}", conn->player_id);
      close_connection(state, epfd, conn);
      return false;
    }
    if (r < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break;
      }
      spdlog::warn("Read error on fd {}: {}", conn->fd, strerror(errno));
      close_connection(state, epfd, conn);
      return false;
    }

    conn->in.append(buf, r);
    std::string msg;
    while (try_parse_frame(conn, msg)) {
      ::poker::v1::Action action;
      if (!action.ParseFromString(msg)) {
        spdlog::warn("Invalid action payload from player {}", conn->player_id);
        state.push_one(conn->player_id, poker::GameError::invalid_action);
      } else {
        spdlog::info("Received action from player {}: {}", conn->player_id,
                     action_to_string(action));
        auto ar = state.apply_action(action, conn->player_id);
        if (!ar) {
          spdlog::info("Action rejected for player {}: {}", conn->player_id,
                       poker::to_string(ar.error()));
        }
        if (ar) {
          state.push_table(conn->table_id, Outbound{*ar});
          sync_table_interest(state, epfd, conn->table_id);
        } else {
          state.push_one(conn->player_id, Outbound{ar.error()});
        }
      }
    }
  }

  return true;
}

auto handle_write(Server &state, int epfd, Conn *conn) -> bool {
  while (!conn->out.empty()) {
    ssize_t w = write(conn->fd, conn->out.data(), conn->out.size());
    if (w < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break;
      }
      spdlog::warn("Write error on fd {}: {}", conn->fd, strerror(errno));
      close_connection(state, epfd, conn);
      return false;
    }
    conn->out.erase(0, w);
    spdlog::debug("Wrote {} bytes to fd {}", w, conn->fd);
  }

  return true;
}

void handle_accepts(Server &state, int epfd) {
  while (true) {
    // max players/tables will be limiting factor here
    int cfd = accept(state.listenfd(), nullptr, nullptr);
    if (cfd < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return;
      }
      // potentially handle other errno's
      return;
    }

    set_nonblocking(cfd);
    auto cr = state.handle_connect(cfd);
    epoll_event conn_event{};
    conn_event.events = conn_interest(cr.conn);
    conn_event.data.ptr = cr.conn;
    epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &conn_event);
    auto tid = cr.conn->table_id;
    if (cr.result) {
      state.push_table(tid, Outbound{*cr.result});
      sync_table_interest(state, epfd, tid);

      std::vector<poker::Event> snapshot;
      for (Conn *existing : state.get_table_conns(tid)) {
        if (existing->player_id == cr.conn->player_id) {
          continue;
        }
        snapshot.push_back(poker::PlayerAdded{existing->player_id});
      }
      if (!snapshot.empty()) {
        state.push_one(cr.conn->player_id, Outbound{snapshot});
        sync_table_interest(state, epfd, tid);
      }
    } else {
      state.push_one(cr.conn->player_id, Outbound{cr.result.error()});
    }
  }
}

void handle_client_event(Server &state, int epfd, epoll_event &event) {
  Conn *conn = static_cast<Conn *>(event.data.ptr);
  bool alive = true;

  if ((event.events & EPOLLIN) != 0U) {
    alive = handle_read(state, epfd, conn);
  }
  if (alive && (event.events & EPOLLOUT) != 0U) {
    alive = handle_write(state, epfd, conn);
  }
  if (alive && conn->is_dead) {
    close_connection(state, epfd, conn);
    return;
  }
  if (!alive) {
    return;
  }

  epoll_event conn_event{};
  conn_event.events = conn_interest(conn);
  conn_event.data.ptr = conn;
  epoll_ctl(epfd, EPOLL_CTL_MOD, conn->fd, &conn_event);
}

int main() {
  std::signal(SIGINT, handle_sigint);
  spdlog::set_level(spdlog::level::info);
  spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");

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

  Server state(listenfd);
  int epfd = epoll_create1(0);
  if (epfd < 0)
    exit(1);
  epoll_event listener_event{};
  listener_event.events = EPOLLIN | EPOLLET;
  listener_event.data.ptr = nullptr;
  if (epoll_ctl(epfd, EPOLL_CTL_ADD, state.listenfd(), &listener_event) < 0)
    exit(1);

  std::array<epoll_event, MAX_EVENTS> events;

  spdlog::info("Started server on port {}", PORT);

  while (!g_stop) {
    int n = epoll_wait(epfd, events.data(), static_cast<int>(events.size()), -1);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      exit(1);
    }
    spdlog::debug("Processing epoll batch with {} events", n);
    for (int i = 0; i < n; ++i) {
      auto &e = events[static_cast<std::size_t>(i)];

      if ((e.events & (EPOLLERR | EPOLLHUP)) != 0U && e.data.ptr != nullptr) {
        auto *conn = static_cast<Conn *>(e.data.ptr);
        close_connection(state, epfd, conn);
        continue;
      }

      if (e.data.ptr == nullptr) {
        handle_accepts(state, epfd);
        continue;
      }

      handle_client_event(state, epfd, e);
    }
  }
  close(epfd);
}
