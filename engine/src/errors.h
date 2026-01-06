#pragma once

#include <variant>

namespace poker {

enum class ServerError { unspecified, too_many_clients, all_tables_full };

enum class GameError {
  invalid_action,
  hand_in_play,
  not_enough_players,
  insufficient_funds,
  bet_too_low,
  out_of_turn,
  no_such_player
};

enum class PlayerMgmtError {
  not_enough_seats,
  invalid_id,
  player_not_found,
  no_players
};

using Error = std::variant<ServerError, GameError, PlayerMgmtError>;

} // namespace poker
