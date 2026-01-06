#include "server.h"

#include <netinet/in.h>
#include <print>
#include <random>
#include <span>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "proto_translate.h"
#include "response.pb.h"

namespace {

constexpr std::size_t kMaxConnections = 102;

void publish_msg(const std::string &msg, Conn *conn) {
  conn->out.push_back(htonl(msg.size()));
  conn->out += msg;
}

::poker::v1::Response make_response(const Outbound &out) {
  ::poker::v1::Response res;
  if (std::holds_alternative<std::vector<poker::Event>>(out)) {
    for (const auto &ev : std::get<std::vector<poker::Event>>(out)) {
      *res.add_messages()->mutable_event() = poker::to_proto_event(ev);
    }
  } else if (std::holds_alternative<poker::Event>(out)) {
    const auto &err = std::get<poker::Event>(out);
    *res.add_messages()->mutable_event() = poker::to_proto_event(err);
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

// TODO: add filtering based on Event. For now, just publish all events
// to conns
void publish(const Outbound &out, std::span<Conn *const> conns) {
  ::poker::v1::Response res = make_response(out);
  std::string msg;
  res.SerializeToString(&msg);
  for (const auto &conn : conns) {
    publish_msg(msg, conn);
  }
}

} // namespace

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

auto Server::handle_connect(int cfd) -> ConnectResult {
  // create a connection object
  poker::PlayerId new_pid = next_player_id_++;
  std::unique_ptr<Conn> c = std::make_unique<Conn>(cfd, new_pid);
  auto conn = c.get();
  connections_[new_pid] = std::move(c);

  // register it with epoll
  epoll_event cev{};
  cev.events = EPOLLIN | EPOLLET;
  cev.data.ptr = conn;
  epoll_ctl(epfd_, EPOLL_CTL_ADD, cfd, &cev);
  std::println("connected {}", cfd);
  // if we exceed max number of connected clients, return an error
  if (connections_.size() > kMaxConnections) {
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
    std::mt19937_64 rng(0);
    it = tables_.emplace(tid, poker::Table(rng)).first;
  }
  // seat the player at the found table or return an error
  auto result = it->second.add_player(new_pid);
  conn->is_dead = result ? false : true;
  return {conn, result};
}

void Server::handle_close(poker::PlayerId id) {
  auto conn = std::move(connections_[id]);
  epoll_ctl(epfd_, EPOLL_CTL_DEL, conn->fd, nullptr);
  close(conn->fd);
  connections_.erase(id);
  if (conn->table_id != 0 && tables_.contains(conn->table_id)) {
    auto result = tables_.at(conn->table_id).remove_player(id);
  }
  std::println("closed {}", conn->fd);
}

void Server::push_one(const poker::PlayerId id, const Outbound &out) {
  publish(out, connections_[id].get());
}

void Server::push_table(const poker::TableId id, const Outbound &out) {
  publish(out, get_table_conns(id));
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
