#include <csignal>
#include <cstdlib>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <array>

#include "actions.pb.h"
#include "errors.h"
#include "reactor_epoll.h"
#include "server.h"
#include "spdlog/spdlog.h"

constexpr int PORT = 65432;
constexpr int MAX_EVENTS = 64;
constexpr int BUF_SIZE = 1024;

auto conn_interest(const Conn *conn) -> poker::ReactorEvent {
  return poker::ReactorEvent::read | poker::ReactorEvent::edge_triggered |
         (!conn->out.empty() ? poker::ReactorEvent::write
                             : poker::ReactorEvent::none);
}

int set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

volatile sig_atomic_t g_stop = 0;

bool try_parse_frame(Conn *c, std::string &out_msg) {
  // Step 1: header
  if (c->in_size == 0) {
    if (c->in.size() < sizeof(uint32_t))
      return false;
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
  case Payload::PAYLOAD_NOT_SET:
  default:
    return "unknown";
  }
}

void handle_sigint(int) { g_stop = 1; }

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
  poker::EpollReactor reactor;
  if (!reactor.valid())
    exit(1);
  reactor.add(state.listenfd(),
              poker::ReactorEvent::read | poker::ReactorEvent::edge_triggered,
              nullptr);

  std::array<poker::ReadyEvent, MAX_EVENTS> events;

  spdlog::info("Started server on port {}", PORT);

  while (!g_stop) {
    int n = reactor.wait(events, -1);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      exit(1);
    }
    spdlog::debug("Processing reactor batch with {} events", n);
    for (int i = 0; i < n; ++i) {
      auto &e = events[static_cast<std::size_t>(i)];

      /* Error path */
      if (poker::has_flag(e.events, poker::ReactorEvent::error) ||
          poker::has_flag(e.events, poker::ReactorEvent::hangup)) {
        if (e.user_data != nullptr) {
          auto *conn = static_cast<Conn *>(e.user_data);
          reactor.del(conn->fd);
          state.handle_close(conn->player_id);
        }
        continue;
      }

      /* New connections */
      if (e.user_data == nullptr) {
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
          reactor.add(cfd, conn_interest(cr.conn), cr.conn);
          auto tid = cr.conn->table_id;
          if (cr.result) {
            state.push_table(tid, Outbound{*cr.result});
            if (auto start_result = state.maybe_start_hand(tid)) {
              state.push_table(cr.conn->table_id, Outbound{*start_result});
            }
          } else {
            state.push_one(cr.conn->player_id, Outbound{cr.result.error()});
          }
        }
        continue;
      }

      /* Client socket */
      Conn *c = static_cast<Conn *>(e.user_data);

      /* Read */
      if (poker::has_flag(e.events, poker::ReactorEvent::read)) {
        char buf[BUF_SIZE];
        while (true) {
          ssize_t r = read(c->fd, buf, sizeof(buf));
          if (r == 0) {
            spdlog::info("Peer closed connection for player {}", c->player_id);
            reactor.del(c->fd);
            state.handle_close(c->player_id);
            goto next_event;
          }
          if (r < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
              break;
            spdlog::warn("Read error on fd {}: {}", c->fd, strerror(errno));
            reactor.del(c->fd);
            state.handle_close(c->player_id);
            goto next_event;
          }
          c->in.append(buf, r);
          std::string msg;
          while (try_parse_frame(c, msg)) {
            ::poker::v1::Action action;
            if (!action.ParseFromString(msg)) {
              spdlog::warn("Invalid action payload from player {}",
                           c->player_id);
              state.push_one(c->player_id, poker::GameError::invalid_action);
            } else {
              spdlog::info("Received action from player {}: {}", c->player_id,
                            action_to_string(action));
              auto ar = state.apply_action(action, c->player_id);
              if (!ar) {
                spdlog::info("Action rejected for player {}: {}", c->player_id,
                             poker::to_string(ar.error()));
              }
              if (ar) {
                state.push_table(c->table_id, Outbound{*ar});
                if (auto next = state.maybe_start_hand(c->table_id)) {
                  state.push_table(c->table_id, Outbound{*next});
                }
              } else {
                state.push_one(c->player_id, Outbound{ar.error()});
              }
            }
          }
        }
      }

      /* Write */
      if (poker::has_flag(e.events, poker::ReactorEvent::write)) {
        while (!c->out.empty()) {
          ssize_t w = write(c->fd, c->out.data(), c->out.size());
          if (w < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
              break;
            spdlog::warn("Write error on fd {}: {}", c->fd, strerror(errno));
            reactor.del(c->fd);
            state.handle_close(c->player_id);
            goto next_event;
          }
          c->out.erase(0, w);
          spdlog::debug("Wrote {} bytes to fd {}", w, c->fd);
        }
      }

      /* Update interest mask */
      if (c->is_dead) {
        reactor.del(c->fd);
        state.handle_close(c->player_id);
      } else {
        reactor.mod(c->fd, conn_interest(c), c);
      }

    next_event:;
    }
  }
}
