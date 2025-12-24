#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <expected>
#include <iterator>
#include <numeric>
#include <optional>
#include <queue>
#include <random>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "deck.h"
#include "poker_rules.h"

using PlayerId = uint64_t;

class Player {
public:
  explicit Player(PlayerId id) : id_(id) {}

  PlayerId id() const { return id_; }

  void add_chips(Chips chips) { purse_ += chips; }

private:
  PlayerId id_;
  Chips purse_{0};
};

enum class PlayerMgmtError {
  not_enough_seats,
  invalid_id,
  player_not_found,
  no_players
};

class PlayerManager {
public:
  PlayerManager() : seats_(kMaxPlayers), open_seats_(kMaxPlayers) {
    std::iota(open_seats_.begin(), open_seats_.end(), 0);
  }

  auto add_player(PlayerId id) -> std::expected<void, PlayerMgmtError> {
    if (open_seats_.empty()) {
      return std::unexpected(PlayerMgmtError::not_enough_seats);
    }
    const auto seat = open_seats_.front();
    open_seats_.pop_front();
    holding_.push_back(id);
    index_[id] = seat;
    return {};
  }

  // Stage a player to leave. If they are seated, mark them; if they are still
  // in holding, remove immediately.
  auto remove_player(PlayerId id) -> std::expected<void, PlayerMgmtError> {
    if (!index_.count(id)) {
      return std::unexpected(PlayerMgmtError::invalid_id);
    }
    const auto seat = index_[id];
    auto held_it = std::find(holding_.begin(), holding_.end(), id);
    if (held_it != holding_.end()) {
      holding_.erase(held_it);
      open_seats_.push_back(seat);
      index_.erase(id);
      return {};
    }
    leaving_.insert(id);
    return {};
  }

  // Move players from holding into seats (start of hand).
  void seat_held_players() {
    while (!holding_.empty()) {
      Player cur(holding_.front());
      holding_.pop_front();
      cur.add_chips(kBuyIn);
      seats_[index_[cur.id()]] = cur;
    }
  }

  // Finalize removals (end of hand).
  void finalize_leavers() {
    for (auto id : leaving_) {
      const auto seat = index_[id];
      seats_[seat].reset();
      open_seats_.push_back(seat);
      index_.erase(id);
    }
    leaving_.clear();
  }

  auto get_first_player() const -> std::expected<PlayerId, PlayerMgmtError> {
    for (auto it = seats_.begin(); it != seats_.end(); it = std::next(it)) {
      if (*it && !is_leaving((*it)->id())) {
        return (*it)->id();
      }
    }
    return std::unexpected(PlayerMgmtError::no_players);
  }

  auto next_player(PlayerId p) const
      -> std::expected<PlayerId, PlayerMgmtError> {
    if (!index_.count(p)) {
      return std::unexpected(PlayerMgmtError::invalid_id);
    }
    const auto total = seats_.size();
    auto it = std::next(seats_.begin() + index_.at(p));
    for (std::size_t checked = 0; checked < total;
         ++checked, it = std::next(it)) {
      if (it == seats_.end()) {
        it = seats_.begin();
      }
      if (*it && !is_leaving((*it)->id())) {
        return (*it)->id();
      }
    }
    // No other active players found; return self.
    return p;
  }

  std::vector<PlayerId> active_cycle_from(PlayerId start) const {
    std::vector<PlayerId> ordered;
    if (!index_.count(start) || seats_.empty() || is_leaving(start)) {
      return ordered;
    }
    ordered.push_back(start);
    for (auto nxt = next_player(start); nxt && *nxt != start;
         nxt = next_player(*nxt)) {
      ordered.push_back(*nxt);
    }
    return ordered;
  }

  std::size_t seated_count() const { return kMaxPlayers - open_seats_.size(); }

  bool is_leaving(PlayerId id) const { return leaving_.count(id) > 0; }

private:
  std::vector<std::optional<Player>> seats_;
  std::deque<std::size_t> open_seats_;
  std::unordered_map<PlayerId, std::size_t> index_;
  std::deque<PlayerId> holding_;
  std::unordered_set<PlayerId> leaving_;
};

enum class GameError { not_enough_players, invalid_action };
enum class Phase : uint8_t { holding, preflop, flop, turn, river, showdown };

struct HandState {
  Phase phase{Phase::preflop};
  PlayerId button{0};
  Chips pot{0};
  std::unordered_map<PlayerId, Chips> active_bets{};
  Chips min_bet{0};
  std::array<cards::Card, kBoardSize> table_cards{};
  std::unordered_map<PlayerId, std::array<cards::Card, kHoleSize>>
      player_holes{};
  std::queue<PlayerId> turn_queue{};
  std::vector<PlayerId> participants{};
  std::unordered_set<PlayerId> folded{};
};

class Table {
public:
  explicit Table(std::mt19937_64 &rng) : rng_(rng) {}

  auto add_player(PlayerId id) -> std::expected<void, PlayerMgmtError> {
    return players_.add_player(id);
  }

  auto remove_player(PlayerId id) -> std::expected<void, PlayerMgmtError> {
    return players_.remove_player(id);
  }

  auto handle_start() -> std::expected<void, GameError> {
    players_.finalize_leavers();
    if (players_.seated_count() < 2) {
      return std::unexpected(GameError::not_enough_players);
    }
    if (hand_.has_value()) {
      return std::unexpected(GameError::invalid_action);
    }
    players_.seat_held_players();
    button_ = button_ == 0 ? *players_.get_first_player()
                           : *players_.next_player(button_);
    HandState state{};
    state.button = button_;
    state.participants = players_.active_cycle_from(button_);
    if (state.participants.size() < 2) {
      return std::unexpected(GameError::not_enough_players);
    }
    deal_cards(state);
    hand_ = std::move(state);
    return {};
  }

private:
  void deal_cards(HandState &state) {
    state.player_holes.clear();
    deck_.shuffle(rng_);
    // deal two cards starting from the button
    state.player_holes[state.button] = *deck_.deal_hole();
    for (auto it = std::next(state.participants.begin());
         it != state.participants.end(); ++it) {
      state.player_holes[*it] = *deck_.deal_hole();
    }
    state.table_cards = *deck_.deal_board();
  }

  cards::Deck deck_{};
  std::mt19937_64 &rng_;
  PlayerManager players_{};
  PlayerId button_{0};
  std::optional<HandState> hand_{std::nullopt};
};
