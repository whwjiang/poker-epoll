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
struct WonPot {
  PlayerId who;
  Chips amount;
};
struct SidePot {
  Chips amount;
  std::vector<PlayerId> eligible;
};
struct HandStarted {};
struct DealtHole {
  PlayerId who;
  std::array<cards::Card, kHoleSize> hole;
};
struct DealtFlop {
  std::array<cards::Card, kFlopSize> flop;
};
struct DealtStreet {
  cards::Card street;
};
using Event = std::variant<PlayerAdded, PlayerRemoved, BetPlaced, TurnAdvanced,
                           PhaseAdvanced, WonPot, HandStarted, DealtHole,
                           DealtFlop, DealtStreet>;

struct Fold {
  PlayerId id;
};
struct Bet {
  PlayerId id;
  Chips amount;
};
struct Timeout {
  PlayerId id;
};

using Action = std::variant<Fold, Bet, Timeout>;

enum class PlayerState { active, all_in, folded, broke, left };

struct HandState {
  Phase phase{Phase::holding};
  PlayerId button{0};
  std::unordered_map<PlayerId, Chips> active_bets{};
  std::unordered_map<PlayerId, Chips> committed{};
  Chips previous_bet{0};
  Chips min_raise{0};
  std::array<cards::Card, kBoardSize> table_cards{};
  std::unordered_map<PlayerId, std::array<cards::Card, kHoleSize>>
      player_holes{};
  std::queue<PlayerId> turn_queue{};
  std::vector<PlayerId> participants{};
  std::unordered_map<PlayerId, PlayerState> player_state;
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

class Table {
public:
  explicit Table(std::mt19937_64 &rng);

  bool hand_in_progress();

  auto add_player(PlayerId id) -> std::expected<Event, PlayerMgmtError>;

  auto remove_player(PlayerId id)
      -> std::expected<std::vector<Event>, PlayerMgmtError>;

  auto on_action(Action action) -> std::expected<std::vector<Event>, GameError>;

  auto handle_new_hand() -> std::expected<std::vector<Event>, GameError>;

  auto handle_new_street() -> std::expected<std::vector<Event>, GameError>;

private:
  void deal_cards(HandState &state);
  void prune_turn_queue();
  auto build_turn_queue(PlayerId start) const -> std::queue<PlayerId>;
  auto first_active_after(PlayerId start) const -> std::optional<PlayerId>;
  auto active_players_in_hand() const -> std::vector<PlayerId>;
  auto build_side_pots() const -> std::vector<SidePot>;
  auto total_committed() const -> Chips;
  auto hand_rank(PlayerId id) const -> uint64_t;
  void award_chips(PlayerId id, Chips amount, std::vector<Event> &events);
  void distribute_side_pots(std::vector<Event> &events);
  void post_blind(PlayerId id, Chips amount, std::vector<Event> &events);
  void reveal_remaining_board(std::vector<Event> &events);
  void advance_turn(std::vector<Event> &events);
  auto handle(const Bet &b) -> std::expected<std::vector<Event>, GameError>;
  auto handle(const Fold &f) -> std::expected<std::vector<Event>, GameError>;
  auto handle(const Timeout &t) -> std::expected<std::vector<Event>, GameError>;

  cards::Deck deck_{};
  std::mt19937_64 &rng_;
  PlayerManager players_{};
  PlayerId button_{0};
  std::optional<HandState> hand_state_{std::nullopt};
};
