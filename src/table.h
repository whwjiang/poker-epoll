#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <queue>
#include <random>
#include <unordered_map>
#include <variant>
#include <vector>

#include "deck.h"
#include "player_manager.h"
#include "poker_rules.h"

using TableId = uint64_t;

enum class GameError {
  not_enough_players,
  invalid_action,
  insufficient_funds,
  bet_too_low
};

enum class Phase : uint8_t { holding, preflop, flop, turn, river, showdown };

struct PlayerAdded {
  PlayerId who;
};
struct PlayerRemoved {
  PlayerId who;
};
struct BetPlaced {
  PlayerId who;
  Chips amount;
};
struct TurnAdvanced {
  PlayerId next;
};
struct PhaseAdvanced {
  Phase next;
};
struct HandStarted {};
struct DealtHole {
  PlayerId who;
  std::array<cards::Card, kHoleSize> hole;
};
struct DealtFlop {
  std::array<cards::Card, kFlopSize> hole;
};
struct DealtStreet {
  cards::Card street;
};
using Event =
    std::variant<PlayerAdded, PlayerRemoved, BetPlaced, PhaseAdvanced,
                 TurnAdvanced, HandStarted, DealtHole, DealtFlop, DealtStreet>;

struct Fold {};
struct Call {};
struct Raise {
  int amount;
};
struct Timeout {};

using Action = std::variant<Fold, Call, Raise, Timeout>;

enum class PlayerState { active, all_in, folded, left };
struct HandState {
  Phase phase{Phase::holding};
  PlayerId button{0};
  Chips pot{0};
  std::unordered_map<PlayerId, Chips> active_bets{};
  Chips min_bet{0};
  std::array<cards::Card, kBoardSize> table_cards{};
  std::unordered_map<PlayerId, std::array<cards::Card, kHoleSize>>
      player_holes{};
  std::queue<PlayerId> turn_queue{};
  std::vector<PlayerId> participants{};
  std::unordered_map<PlayerId, PlayerState> player_state;
};


class Table {
public:
  explicit Table(std::mt19937_64 &rng);

  auto add_player(PlayerId id) -> std::expected<Event, PlayerMgmtError>;

  auto remove_player(PlayerId id)
      -> std::expected<std::vector<Event>, PlayerMgmtError>;

  auto handle_new_hand() -> std::expected<std::vector<Event>, GameError>;

  auto handle_new_street() -> std::expected<std::vector<Event>, GameError>;

  auto handle_bet() -> std::expected<std::vector<Event>, GameError>;

private:
  void deal_cards(HandState &state);

  cards::Deck deck_{};
  std::mt19937_64 &rng_;
  PlayerManager players_{};
  PlayerId button_{0};
  std::optional<HandState> hand_state_{std::nullopt};
};
