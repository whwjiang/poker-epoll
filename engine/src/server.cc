#include "server.h"

#include <cstdint>
#include <netinet/in.h>
#include <random>
#include <span>
#include <spdlog/spdlog.h>
#include <sys/socket.h>
#include <unistd.h>

#include "errors.h"
#include "player.h"
#include "proto_translate.h"
#include "response.pb.h"

namespace {

constexpr std::size_t kMaxConnections = 1000;

template <class... Ts> struct overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

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
  return std::visit(overloaded{
                        [conn](const poker::DealtHole &dealt) {
                          return dealt.who == conn->player_id;
                        },
                        [](const auto &) { return true; },
                    },
                    ev);
}

::poker::v1::Response make_response(const Outbound &out) {
  ::poker::v1::Response res;
  std::visit(
      overloaded{
          [&res](const poker::Event &event) { append_event(res, event); },
          [&res](const std::vector<poker::Event> &events) {
            for (const auto &event : events) {
              append_event(res, event);
            }
          },
          [&res](const poker::Error &error) {
            *res.add_messages()->mutable_error() = poker::to_proto_error(error);
          },
      },
      out);
  return res;
}

void publish(const Outbound &out, Conn *const conn) {
  ::poker::v1::Response res = make_response(out);
  std::string msg;
  res.SerializeToString(&msg);
  publish_msg(msg, conn);
}

void publish(const Outbound &out, std::span<Conn *const> conns) {
  std::visit(overloaded{
                 [](const poker::Error &) {
                   spdlog::warn(
                       "Attempted to broadcast error to table; dropping");
                 },
                 [&conns](const poker::Event &event) {
                   for (const auto &conn : conns) {
                     if (!event_visible_to(event, conn)) {
                       continue;
                     }
                     ::poker::v1::Response res;
                     append_event(res, event);
                     std::string msg;
                     res.SerializeToString(&msg);
                     publish_msg(msg, conn);
                   }
                 },
                 [&conns](const std::vector<poker::Event> &events) {
                   for (const auto &conn : conns) {
                     ::poker::v1::Response res;
                     for (const auto &event : events) {
                       if (event_visible_to(event, conn)) {
                         append_event(res, event);
                       }
                     }
                     if (res.messages_size() == 0) {
                       continue;
                     }
                     std::string msg;
                     res.SerializeToString(&msg);
                     publish_msg(msg, conn);
                   }
                 },
             },
             out);
}

} // namespace

Conn::Conn(int cfd, poker::PlayerId id) : fd(cfd), player_id(id) {}

Server::Server(int listenfd) : listenfd_(listenfd) {}

Server::~Server() {
  for (auto &[_, conn] : connections_) {
    close(conn->fd);
  }
  close(listenfd_);
}

int Server::listenfd() const { return listenfd_; }

auto Server::handle_connect(const int cfd) -> ConnectResult {
  // create a connection object
  poker::PlayerId new_pid = next_player_id_++;
  std::unique_ptr<Conn> c = std::make_unique<Conn>(cfd, new_pid);
  auto conn = c.get();
  connections_[new_pid] = std::move(c);

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
    it = tables_.emplace(tid, poker::Table(std::mt19937_64{0})).first;
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
  close(conn->fd);
  connections_.erase(id);
  std::optional<Outbound> table_update = std::nullopt;
  if (conn->table_id != 0 && tables_.contains(conn->table_id)) {
    auto table_it = tables_.find(conn->table_id);
    auto result = table_it->second.remove_player(id);
    if (!result) {
      spdlog::warn("Failed to remove player {} from table {}: {}", id,
                   conn->table_id, poker::to_string(result.error()));
    } else {
      table_update = Outbound{*result};
      if (table_it->second.is_empty()) {
        tables_.erase(table_it);
        spdlog::info("Removed empty table {}", conn->table_id);
      }
    }
  }
  if (table_update.has_value()) {
    push_table(conn->table_id, *table_update);
  }
  spdlog::info("Closed connection on fd {}", conn->fd);
}

auto Server::start_hand(const poker::TableId id)
    -> std::expected<std::vector<poker::Event>, poker::Error> {
  if (!tables_.contains(id)) {
    return std::unexpected(poker::ServerError::illegal_action);
  }
  return tables_.at(id).handle_new_hand();
}

auto Server::maybe_start_hand(const poker::TableId id)
    -> std::optional<std::vector<poker::Event>> {
  auto it = tables_.find(id);
  if (it == tables_.end()) {
    return std::nullopt;
  }
  auto &table = it->second;
  if (!table.can_start_hand()) {
    spdlog::warn("Couldn't start hand for table yet");
    return std::nullopt;
  }
  auto res = table.handle_new_hand();
  if (!res) {
    spdlog::warn("Failed to auto-start hand at table {}: {}", id,
                 poker::to_string(res.error()));
    return std::nullopt;
  }
  spdlog::info("Started hand for table {}", id);
  return std::move(*res);
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
