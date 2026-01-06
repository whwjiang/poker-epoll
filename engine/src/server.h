#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "errors.h"
#include "player.h"
#include "table.h"

struct Conn {
  Conn(int cfd, poker::PlayerId id);
  int fd;
  std::string in;
  uint32_t in_off{0};
  uint32_t in_size{0};
  std::string out;
  uint32_t out_off{0};
  poker::TableId table_id{0};
  poker::PlayerId player_id{0};
  bool is_dead{true};
};

using Outbound =
    std::variant<poker::Event, std::vector<poker::Event>, poker::Error>;

struct ConnectResult {
  Conn *conn;
  std::expected<poker::Event, poker::Error> result;
};

class Server {
public:
  Server(int epfd, int listenfd);
  ~Server();

  int epfd() const;
  int listenfd() const;

  // the caller is responsible for publishing events produced by this
  // method to the appropriate audience
  // returning a raw Conn* inside the ConnectResult isn't great, but the server
  // is single threaded so we don't risk much
  auto handle_connect(int cfd) -> ConnectResult;
  void handle_close(poker::PlayerId id);
  void push_one(poker::PlayerId id, const Outbound &out);
  void push_table(poker::TableId id, const Outbound &out);

private:
  int epfd_;
  int listenfd_;
  std::unordered_map<poker::PlayerId, std::unique_ptr<Conn>> connections_;
  std::unordered_map<poker::TableId, poker::Table> tables_;
  poker::PlayerId next_player_id_{1};
  poker::TableId next_table_id_{1};

  std::vector<Conn *> get_table_conns(poker::TableId id) const;
};
