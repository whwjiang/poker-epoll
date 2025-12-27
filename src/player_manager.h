#pragma once

#include <cstddef>
#include <deque>
#include <expected>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "player.h"
#include "poker_rules.h"

enum class PlayerMgmtError {
  not_enough_seats,
  invalid_id,
  player_not_found,
  no_players
};

class PlayerManager {
public:
  PlayerManager();

  auto add_player(PlayerId id) -> std::expected<void, PlayerMgmtError>;

  auto remove_player(PlayerId id) -> std::expected<void, PlayerMgmtError>;

  void seat_held_players();

  void finalize_leavers();

  auto get_first_player() const -> std::expected<PlayerId, PlayerMgmtError>;

  auto next_player(PlayerId p) const
      -> std::expected<PlayerId, PlayerMgmtError>;

  std::vector<PlayerId> active_cycle_from(PlayerId start) const;

  std::size_t seated_count() const;

  bool is_leaving(PlayerId id) const;

private:
  std::vector<std::optional<Player>> seats_;
  std::deque<std::size_t> open_seats_;
  std::unordered_map<PlayerId, std::size_t> index_;
  std::deque<PlayerId> holding_;
  std::unordered_set<PlayerId> leaving_;
};
