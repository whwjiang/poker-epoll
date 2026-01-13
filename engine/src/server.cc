#include "server.h"

#include <cstdint>
#include <netinet/in.h>
#include <random>
#include <span>
#include <spdlog/spdlog.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "errors.h"
#include "player.h"
#include "proto_translate.h"
#include "response.pb.h"

namespace {

constexpr std::size_t kMaxConnections = 102;

void publish_msg(const std::string &msg, Conn *conn) {
  uint32_t len = htonl(static_cast<uint32_t>(msg.size()));
  spdlog::debug("Going to write {} bytes to fd {}", msg.size(), conn->fd);
  conn->out.append(reinterpret_cast<const char *>(&len), sizeof(len));
  conn->out += msg;
}

void append_event(::poker::v1::Response &res, const poker::Event &ev) {
  *res.add_messages()->mutable_event() = poker::to_proto_event(ev);
}

bool event_visible_to(const poker::Event &ev, const Conn *conn) {
  if (const auto *dealt = std::get_if<poker::DealtHole>(&ev)) {
    return dealt->who == conn->player_id;
  }
  return true;
}

::poker::v1::Response make_response(const Outbound &out) {
  ::poker::v1::Response res;
  if (std::holds_alternative<std::vector<poker::Event>>(out)) {
    for (const auto &ev : std::get<std::vector<poker::Event>>(out)) {
      append_event(res, ev);
    }
  } else if (std::holds_alternative<poker::Event>(out)) {
    const auto &err = std::get<poker::Event>(out);
    append_event(res, err);
  } else {
    const auto &err = std::get<poker::Error>(out);
    *res.add_messages()->mutable_error() = poker::to_proto_error(err);
  }
  return res;
}

void publish(const Outbound &out, Conn *const conn) {
  ::poker::v1::Response res = make_response(out);
  std::string msg;
  res.SerializeToString(&msg);
  publish_msg(msg, conn);
}

void publish(const Outbound &out, std::span<Conn *const> conns) {
  if (std::holds_alternative<poker::Error>(out)) {
    spdlog::warn("Attempted to broadcast error to table; dropping");
    return;
  }
  if (std::holds_alternative<poker::Event>(out)) {
    const auto &ev = std::get<poker::Event>(out);
    for (const auto &conn : conns) {
      if (!event_visible_to(ev, conn)) {
        continue;
      }
      ::poker::v1::Response res;
      append_event(res, ev);
      std::string msg;
      res.SerializeToString(&msg);
      publish_msg(msg, conn);
    }
    return;
  }
  const auto &events = std::get<std::vector<poker::Event>>(out);
  for (const auto &conn : conns) {
    ::poker::v1::Response res;
    for (const auto &ev : events) {
      if (event_visible_to(ev, conn)) {
        append_event(res, ev);
      }
    }
    if (res.messages_size() == 0) {
      continue;
    }
    std::string msg;
    res.SerializeToString(&msg);
    publish_msg(msg, conn);
  }
}

} // namespace

void update_interest(Conn *const c, int epfd) {
  epoll_event nev{};
  nev.data.ptr = c;
  nev.events = EPOLLIN | EPOLLET | (!c->out.empty() * EPOLLOUT);
  spdlog::debug("Conn fd {} EPOLLOUT: {}", c->fd,
                static_cast<int>(nev.events & EPOLLOUT));
  epoll_ctl(epfd, EPOLL_CTL_MOD, c->fd, &nev);
}

Conn::Conn(int cfd, poker::PlayerId id) : fd(cfd), player_id(id) {}

Server::Server(int epfd, int listenfd) : epfd_(epfd), listenfd_(listenfd) {}

Server::~Server() {
  for (auto &[_, conn] : connections_) {
    close(conn->fd);
  }
  close(epfd_);
  close(listenfd_);
}

int Server::epfd() const { return epfd_; }

int Server::listenfd() const { return listenfd_; }

auto Server::handle_connect(const int cfd) -> ConnectResult {
  // create a connection object
  poker::PlayerId new_pid = next_player_id_++;
  std::unique_ptr<Conn> c = std::make_unique<Conn>(cfd, new_pid);
  auto conn = c.get();
  connections_[new_pid] = std::move(c);

  // register it with epoll
  epoll_event cev{};
  cev.events = EPOLLIN | EPOLLOUT | EPOLLET;
  cev.data.ptr = conn;
  epoll_ctl(epfd_, EPOLL_CTL_ADD, cfd, &cev);
  spdlog::info("Accepted connection on fd {}", cfd);
  // if we exceed max number of connected clients, return an error
  if (connections_.size() > kMaxConnections) {
    spdlog::warn("Too many clients connected ({}), rejecting player {}",
                 connections_.size(), new_pid);
    return {conn, std::unexpected(poker::ServerError::too_many_clients)};
  }
  // find a table to seat the player
  poker::TableId tid = 0;
  for (const auto &[id, table] : tables_) {
    if (table.has_open_seat()) {
      tid = id;
      break;
    }
  }
  // if no open tables, create one
  auto it = tables_.find(tid);
  if (it == tables_.end()) {
    tid = next_table_id_++;
    std::mt19937_64 rng(0); // TODO: make this different for each table
    it = tables_.emplace(tid, poker::Table(rng)).first;
    spdlog::info("Created new table {}", tid);
  }
  // seat the player at the found table or return an error
  auto add_result = it->second.add_player(new_pid);
  conn->table_id = tid;
  if (add_result) {
    spdlog::info("Seated player {} at table {}", new_pid, tid);
  } else {
    spdlog::warn("Failed to seat player {} at table {}: {}", new_pid, tid,
                 poker::to_string(add_result.error()));
  }
  conn->is_dead = add_result ? false : true;
  return {conn, add_result};
}

void Server::handle_close(const poker::PlayerId id) {
  if (!connections_.contains(id)) {
    spdlog::warn("Attempted close on player id {} which does not exist", id);
    return;
  }
  auto conn = std::move(connections_[id]);
  epoll_ctl(epfd_, EPOLL_CTL_DEL, conn->fd, nullptr);
  close(conn->fd);
  connections_.erase(id);
  if (conn->table_id != 0 && tables_.contains(conn->table_id)) {
    auto result = tables_.at(conn->table_id).remove_player(id);
    if (!result) {
      spdlog::warn("Failed to remove player {} from table {}: {}", id,
                   conn->table_id, poker::to_string(result.error()));
    }
  }
  spdlog::info("Closed connection on fd {}", conn->fd);
}

auto Server::start_hand(const poker::TableId id)
    -> std::expected<std::vector<poker::Event>, poker::Error> {
  if (!tables_.contains(id)) {
    return std::unexpected(poker::ServerError::illegal_action);
  }
  auto table = tables_.at(id);
  return table.handle_new_hand();
}

auto Server::apply_action(const ::poker::v1::Action a, poker::PlayerId id)
    -> std::expected<std::vector<poker::Event>, poker::Error> {
  auto action = poker::from_proto_action(a, id);
  if (!action) {
    return std::unexpected(action.error());
  }
  auto conn = connections_.at(id).get();
  if (conn->table_id == 0 || !tables_.contains(conn->table_id)) {
    return std::unexpected(poker::ServerError::illegal_action);
  }
  return tables_.at(conn->table_id).on_action(action.value());
}

void Server::push_one(const poker::PlayerId id, const Outbound &out) {
  publish(out, connections_[id].get());
}

void Server::push_table(const poker::TableId id, const Outbound &out) {
  auto conns = get_table_conns(id);
  publish(out, conns);
  for (const auto &conn : conns) {
    update_interest(conn, epfd_);
  }
}

std::vector<Conn *> Server::get_table_conns(const poker::TableId id) const {
  std::vector<Conn *> result;
  for (const auto &[pid, conn] : connections_) {
    if (conn->table_id == id) {
      result.push_back(conn.get());
    }
  }
  return result;
}
