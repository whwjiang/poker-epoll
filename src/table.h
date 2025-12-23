#pragma once

#include <cstdint>
#include <deque>
#include <expected>
#include <optional>
#include <unordered_map>
#include <vector>

#include "deck.h"
#include <random>

using PlayerId = uint64_t;

class Player {
public:
  Player(PlayerId id) : id_(id) {}

  PlayerId id() { return id_; }

  void add_chips(Chips chips) { purse_ += chips; }

private:
  std::size_t id_;
  Chips purse_{0};
};

class Table {
public:
  explicit Table(std::mt19937_64 &rng)
      : rng_(rng), open_seats_(kMaxPlayers), seats_(kMaxPlayers) {
    std::iota(open_seats_.begin(), open_seats_.end(), 0);
  };
  enum class PlayerMgmtError {
    not_enough_seats,
    invalid_id,
    player_not_found,
    no_players
  };
  enum class GameError { not_enough_players, invalid_action };
  enum class Phase : uint8_t { holding, preflop, flop, turn, river, showdown };

private:
  // card management
  cards::Deck deck_{};
  std::mt19937_64 &rng_;
  // player management
  std::vector<std::optional<Player>> seats_;
  std::deque<std::size_t> open_seats_;
  std::unordered_map<PlayerId, std::size_t> index_;
  std::deque<PlayerId> holding_;
  // game state
  PlayerId button_{0};
  Chips pot_{0};
  Chips bet_{0};
  std::array<cards::Card, kBoardSize> table_cards_;
  Phase phase_{Phase::holding};
  std::unordered_map<PlayerId, std::array<cards::Card, kHoleSize>>
      player_holes_;

  // player management methods
  // edge cases to consider:
  // 1. what if player joins/leaves during in progress hand?
  // -> if player joins, we need to stage them for joining (use a queue)
  // -> if player leaves, we need to:
  //    1. remove them immediately (advance play to the next hand - we can
  //         just fold for them and call remove_player)
  //    2.
  auto add_player(PlayerId id) -> std::expected<void, PlayerMgmtError> {
    if (open_seats_.size() == 0) {
      return std::unexpected(PlayerMgmtError::not_enough_seats);
    }
    auto seat = open_seats_.front();
    open_seats_.pop_front();
    holding_.push_back(id);
    index_[id] = seat;
    return {};
  }

  auto remove_player(PlayerId id) -> std::expected<void, PlayerMgmtError> {
    if (!index_.count(id)) {
      return std::unexpected(PlayerMgmtError::invalid_id);
    }
    // pass the button to the next active player
    if (button_ = index_[id]) {
      if (const auto next = next_player(id); *next == id) {
        // id is the last active player at the table, so reset button
        button_ = 0;
      } else {
        button_ = index_[*next];
      }
    }

    auto seat = index_[id];
    // player is either in the holding pen or sitting
    if (seats_[seat]) {
      seats_[seat].reset();
    } else {
      auto it = std::find(holding_.begin(), holding_.end(), id);
      if (it == holding_.end()) {
        return std::unexpected(PlayerMgmtError::player_not_found);
      }
      holding_.erase(it);
    }
    open_seats_.push_back(seat);
    index_.erase(id);
    return {};
  }

  auto get_first_player() -> std::expected<PlayerId, PlayerMgmtError> {
    // if (open_seats_.size() == kMaxPlayers) {
    //   return std::unexpected(PlayerMgmtError::no_players);
    // }
    for (auto it = seats_.begin(); it != seats_.end(); it = next(it)) {
      if (*it) {
        return (*it)->id();
      }
    }
    return std::unexpected(PlayerMgmtError::no_players);
  }

  // Gets the next player clockwise to player p.
  // This equates to the next non-empty index in seats_, wrapping
  // around as appropriate. If p is the only seated player, return p
  auto next_player(PlayerId p) -> std::expected<PlayerId, PlayerMgmtError> {
    if (!index_.count(p)) {
      return std::unexpected(PlayerMgmtError::invalid_id);
    }
    if (open_seats_.size() == kMaxPlayers - 1) {
      return p;
    }
    auto next_it = next(seats_.begin() + index_[p]);
    for (;;) {
      if (next_it == seats_.end()) {
        next_it = seats_.begin();
      }
      if (*next_it) {
        break;
      }
    }
    return (*next_it)->id();
  }

  // action handlers
  auto handle_start() -> std::expected<void, GameError> {
    if (kMaxPlayers - open_seats_.size() < 2) {
      return std::unexpected(GameError::not_enough_players);
    }
    if (phase_ != Phase::holding) {
      return std::unexpected(GameError::invalid_action);
    }
    phase_ = Phase::preflop;
    return {};
  }

  void init_held_players() {
    while (!holding_.empty()) {
      Player cur(holding_.front());
      holding_.pop_front();
      cur.add_chips(kBuyIn);
      seats_[index_[cur.id()]] = cur;
    }
  }

  void deal_cards() {
    player_holes_.clear();
    deck_.shuffle(rng_);
    // we are just going to deal two cards starting from the button
    // since we will seat at most 10 players, assume there will be enough cards
    player_holes_[button_] = *deck_.deal_hole();
    for (auto cur = *next_player(button_); cur != button_;
         cur = *next_player(cur)) {
      player_holes_[cur] = *deck_.deal_hole();
    }
    table_cards_ = *deck_.deal_board();
  }

  auto handle_preflop() -> std::expected<void, GameError> {
    // init any players that were being held
    init_held_players();
    // advance the button
    button_ = button_ == 0 ? *get_first_player() : *next_player(button_);
    deal_cards();
  }
};
