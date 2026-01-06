#pragma once

#include <cstddef>
#include <deque>
#include <expected>
#include <optional>
#include <unordered_map>
#include <vector>

#include "errors.h"
#include "player.h"
#include "poker_rules.h"

namespace poker {

class PlayerManager {
public:
  PlayerManager();

  auto add_player(PlayerId id) -> std::expected<void, PlayerMgmtError>;

  auto remove_player(PlayerId id) -> std::expected<void, PlayerMgmtError>;

  void seat_held_players();

  auto get_first_player() const -> std::expected<PlayerId, PlayerMgmtError>;

  auto next_player(PlayerId p) const
      -> std::expected<PlayerId, PlayerMgmtError>;

  std::vector<PlayerId> active_cycle_from(PlayerId start) const;

  std::size_t seated_count() const;

  bool is_sat(PlayerId id) const;

  // caller is responsible for validating id
  bool has_enough_chips(PlayerId id, Chips bet) const;

  Chips get_chips(PlayerId id) const;

  void place_bet(PlayerId id, Chips bet);
  void award_chips(PlayerId id, Chips amount);

private:
  std::vector<std::optional<Player>> seats_;
  std::deque<std::size_t> open_seats_;
  std::unordered_map<PlayerId, std::size_t> index_;
  std::deque<PlayerId> holding_;
};

} // namespace poker
