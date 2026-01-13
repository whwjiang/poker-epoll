#pragma once

#include <string_view>
#include <variant>

namespace poker {

enum class ServerError {
  unspecified,
  too_many_clients,
  all_tables_full,
  illegal_action
};

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

inline auto to_string(ServerError err) -> std::string_view {
  switch (err) {
  case ServerError::too_many_clients:
    return "too_many_clients";
  case ServerError::all_tables_full:
    return "all_tables_full";
  case ServerError::illegal_action:
    return "illegal_action";
  case ServerError::unspecified:
  default:
    return "unspecified_server_error";
  }
}

inline auto to_string(GameError err) -> std::string_view {
  switch (err) {
  case GameError::invalid_action:
    return "invalid_action";
  case GameError::hand_in_play:
    return "hand_in_play";
  case GameError::not_enough_players:
    return "not_enough_players";
  case GameError::insufficient_funds:
    return "insufficient_funds";
  case GameError::bet_too_low:
    return "bet_too_low";
  case GameError::out_of_turn:
    return "out_of_turn";
  case GameError::no_such_player:
    return "no_such_player";
  default:
    return "unspecified_game_error";
  }
}

inline auto to_string(PlayerMgmtError err) -> std::string_view {
  switch (err) {
  case PlayerMgmtError::not_enough_seats:
    return "not_enough_seats";
  case PlayerMgmtError::invalid_id:
    return "invalid_id";
  case PlayerMgmtError::player_not_found:
    return "player_not_found";
  case PlayerMgmtError::no_players:
    return "no_players";
  default:
    return "unspecified_player_mgmt_error";
  }
}

inline auto to_string(const Error &err) -> std::string_view {
  return std::visit(
      [](const auto &e) -> std::string_view { return to_string(e); }, err);
}

} // namespace poker
