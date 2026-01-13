#include "table.h"
#include "hand_evaluator.h"
#include "player.h"
#include "poker_rules.h"

#include <algorithm>
#include <limits>

namespace poker {

Table::Table(std::mt19937_64 &rng) : rng_(rng) {}

bool Table::has_open_seat() const {
  return players_.num_players() < kMaxPlayers;
}

bool Table::hand_in_progress() const { return hand_state_.has_value(); }

bool Table::can_start_hand() const {
  return !hand_in_progress() && players_.num_players() >= 2;
}

auto Table::add_player(PlayerId id) -> std::expected<Event, PlayerMgmtError> {
  return players_.add_player(id).transform([&] { return PlayerAdded{id}; });
}

auto Table::remove_player(PlayerId id)
    -> std::expected<std::vector<Event>, PlayerMgmtError> {
  // there are four cases:
  // 1. player is active and turn already occurred
  // we just have to mark them as left
  // 2. player is active and his turn hasn't occurred
  // we need to set their state as left, and then when we get to his turn, we
  // will skip
  // 3. player's turn is right now
  // if they are at the front of the turn queue, this method needs to advance
  // the game state and emit an event so that everyone knows
  // 4. no hand in play
  // removal from PlayerManager is sufficient
  if (auto result = players_.remove_player(id); !result) {
    return std::unexpected(result.error()); // case 4 handled
  }
  std::vector<Event> res{PlayerRemoved{id}};
  if (hand_state_.has_value()) {
    hand_state_->player_state[id] = PlayerState::left; // case 1, 2
    std::queue<PlayerId> updated;
    bool removed_front = false;
    while (!hand_state_->turn_queue.empty()) {
      auto cur = hand_state_->turn_queue.front();
      hand_state_->turn_queue.pop();
      if (cur == id) {
        if (updated.empty()) {
          removed_front = true;
        }
        continue;
      }
      updated.push(cur);
    }
    hand_state_->turn_queue = std::move(updated);
    if (removed_front) {
      prune_turn_queue();
      if (!hand_state_->turn_queue.empty()) {
        res.push_back(TurnAdvanced{hand_state_->turn_queue.front()});
      }
    }
  }
  return res;
}

auto Table::on_action(Action action)
    -> std::expected<std::vector<Event>, GameError> {
  auto result = std::visit(
      [&](auto &&a) -> std::expected<std::vector<Event>, GameError> {
        if (!hand_state_) {
          return std::unexpected(GameError::invalid_action);
        }
        if (!players_.is_sat(a.id)) {
          return std::unexpected(GameError::no_such_player);
        }
        prune_turn_queue();
        if (hand_state_->turn_queue.empty()) {
          return std::unexpected(GameError::invalid_action);
        }
        if (a.id != hand_state_->turn_queue.front()) {
          return std::unexpected(GameError::out_of_turn);
        }
        return handle(a);
      },
      action);
  if (!result) {
    return std::unexpected(result.error());
  }
  auto response = *result;
  prune_turn_queue();
  auto remaining = active_players_in_hand();
  if (remaining.size() == 1) {
    award_chips(remaining.front(), total_committed(), response);
    hand_state_.reset();
    return response;
  }
  // advance the hand phase if that was the last player
  if (hand_state_->turn_queue.size() == 0) {
    bool any_active =
        std::any_of(remaining.begin(), remaining.end(), [&](PlayerId id) {
          return hand_state_->player_state.at(id) == PlayerState::active;
        });
    if (!any_active) {
      reveal_remaining_board(response);
      distribute_side_pots(response);
      hand_state_.reset();
      return response;
    }
    if (hand_state_->phase == Phase::river) {
      distribute_side_pots(response);
      hand_state_.reset();
      return response;
    }
    auto advance = handle_new_street();
    if (!advance) {
      return std::unexpected(advance.error());
    }
    response.insert(response.end(), advance->begin(), advance->end());
  } else {
    advance_turn(response);
  }
  return response;
}

// assume that all actions will happen serially. any driver needs to ensure
// this so as to avoid race conditions or inconsistent state
auto Table::handle_new_hand() -> std::expected<std::vector<Event>, GameError> {
  if (players_.num_players() < 2) {
    return std::unexpected(GameError::not_enough_players);
  }
  if (hand_in_progress()) {
    return std::unexpected(GameError::hand_in_play);
  }
  hand_state_.reset();
  players_.seat_held_players();
  button_ = button_ == 0 ? *players_.get_first_player()
                         : *players_.next_player(button_);
  HandState state{};
  state.button = button_;
  state.participants = players_.active_cycle_from(button_);
  if (state.participants.size() < 2) {
    return std::unexpected(GameError::not_enough_players);
  }
  for (auto id : state.participants) {
    state.player_state[id] = PlayerState::active;
    state.active_bets[id] = 0;
    state.committed[id] = 0;
  }
  deal_cards(state);
  state.phase = Phase::preflop;
  state.previous_bet = 0;
  state.min_raise = kBigBlind;
  hand_state_ = std::move(state);

  // prepare response
  std::vector<Event> events;
  events.push_back(HandStarted{});
  events.push_back(PhaseAdvanced{Phase::preflop});
  for (const auto &[id, hole] : hand_state_->player_holes) {
    events.push_back({DealtHole{id, hole}});
  }

  const auto &participants = hand_state_->participants;
  if (participants.size() == 2) {
    PlayerId sb = participants[0];
    PlayerId bb = participants[1];
    post_blind(sb, kSmallBlind, events);
    post_blind(bb, kBigBlind, events);
    hand_state_->turn_queue = build_turn_queue(sb);
  } else {
    PlayerId sb = participants[1 % participants.size()];
    PlayerId bb = participants[2 % participants.size()];
    post_blind(sb, kSmallBlind, events);
    post_blind(bb, kBigBlind, events);
    PlayerId first = participants[3 % participants.size()]; // left of big blind
    hand_state_->turn_queue = build_turn_queue(first);
  }

  prune_turn_queue();
  if (hand_state_->turn_queue.empty()) {
    reveal_remaining_board(events);
    distribute_side_pots(events);
    hand_state_.reset();
    return events;
  }
  events.push_back(TurnAdvanced{hand_state_->turn_queue.front()});

  return events;
}

auto Table::handle_new_street()
    -> std::expected<std::vector<Event>, GameError> {
  // create the queue for the next phase. if the queue only has one entry, then
  // the only player left gets the entire pot, and we start over
  std::vector<Event> events;
  Phase next = hand_state_->phase;
  switch (hand_state_->phase) {
  case Phase::preflop:
    next = Phase::flop;
    break;
  case Phase::flop:
    next = Phase::turn;
    break;
  case Phase::turn:
    next = Phase::river;
    break;
  case Phase::river:
  case Phase::showdown:
  case Phase::holding:
    return std::unexpected(GameError::invalid_action);
  }

  hand_state_->phase = next;
  events.push_back(PhaseAdvanced{next});
  if (next == Phase::flop) {
    std::array<cards::Card, kFlopSize> flop{};
    std::copy_n(hand_state_->table_cards.begin(), kFlopSize, flop.begin());
    events.push_back(DealtFlop{flop});
  } else if (next == Phase::turn) {
    events.push_back(DealtStreet{hand_state_->table_cards[kFlopSize]});
  } else if (next == Phase::river) {
    events.push_back(DealtStreet{hand_state_->table_cards[kFlopSize + 1]});
  }

  for (auto &[id, amount] : hand_state_->active_bets) {
    (void)id;
    amount = 0;
  }
  hand_state_->previous_bet = 0;
  hand_state_->min_raise = kBigBlind;
  if (auto start = first_active_after(hand_state_->button); start) {
    hand_state_->turn_queue = build_turn_queue(*start);
  } else {
    hand_state_->turn_queue = {};
  }
  prune_turn_queue();
  if (!hand_state_->turn_queue.empty()) {
    events.push_back(TurnAdvanced{hand_state_->turn_queue.front()});
  }
  return events;
}

// chips is the amount that the player is betting.
// a "check" is a bet of 0
// a "call" is a bet that is equal to the previous bet
// a "raise" is a bet that must be at least the min bet
auto Table::handle(const Bet &b)
    -> std::expected<std::vector<Event>, GameError> {
  auto [id, bet] = b;
  const Chips previous = hand_state_->previous_bet;
  const Chips current = hand_state_->active_bets[id];
  const Chips chips = players_.get_chips(id);
  // if it is a check, ensure that it is a valid action to take
  bool is_raise = false;
  bool is_all_in = false;
  if (bet >= chips && bet > 0) {
    bet = chips;
    hand_state_->player_state[id] = PlayerState::all_in;
    is_all_in = true;
  }
  const Chips total = current + bet;
  if (bet == 0 && current < previous) {
    return std::unexpected(GameError::bet_too_low);
  } else if (bet > 0) {
    if (total < previous && !is_all_in) {
      return std::unexpected(GameError::bet_too_low);
    } else if (total > previous) {
      if (total - previous < hand_state_->min_raise && !is_all_in) {
        return std::unexpected(GameError::bet_too_low);
      }
      is_raise = (total - previous) >= hand_state_->min_raise;
    }
  }
  // if we get here, then we have a valid action
  hand_state_->turn_queue.pop();
  players_.place_bet(id, bet);
  hand_state_->committed[id] += bet;
  hand_state_->previous_bet = std::max(previous, total);
  hand_state_->active_bets[id] = total;
  if (is_raise) {
    hand_state_->min_raise = total - previous;
    // on raise, we need to requeue all active, non folded players
    auto ring = players_.active_cycle_from(id);
    std::queue<PlayerId> updated;
    for (auto x : ring) {
      if (x == id) {
        continue;
      }
      auto it = hand_state_->player_state.find(x);
      if (it != hand_state_->player_state.end() &&
          it->second == PlayerState::active) {
        updated.push(x);
      }
    }
    hand_state_->turn_queue = std::move(updated);
  }

  // prepare the response
  std::vector<Event> res;
  res.push_back(BetPlaced{id, bet});
  return res;
}

auto Table::handle(const Fold &f)
    -> std::expected<std::vector<Event>, GameError> {
  const auto &[id] = f;
  hand_state_->turn_queue.pop();
  hand_state_->player_state[id] = PlayerState::folded;
  hand_state_->active_bets.erase(id);

  std::vector<Event> res;
  return res;
}

auto Table::handle(const Timeout &t)
    -> std::expected<std::vector<Event>, GameError> {
  const auto &[id] = t;
  if (hand_state_->active_bets[id] < hand_state_->previous_bet) {
    return handle(Fold{id});
  }
  return handle(Bet{id, 0});
}

void Table::deal_cards(HandState &state) {
  state.player_holes.clear();
  deck_.shuffle(rng_);
  // deal two cards starting from the button
  state.player_holes[state.button] = *deck_.deal_hole();
  for (auto it = std::next(state.participants.begin());
       it != state.participants.end(); ++it) {
    state.player_holes[*it] = *deck_.deal_hole();
  }
  // deal the board
  state.table_cards = *deck_.deal_board();
}

void Table::prune_turn_queue() {
  if (!hand_state_) {
    return;
  }
  while (!hand_state_->turn_queue.empty()) {
    auto id = hand_state_->turn_queue.front();
    auto it = hand_state_->player_state.find(id);
    if (it != hand_state_->player_state.end() &&
        it->second == PlayerState::active) {
      break;
    }
    hand_state_->turn_queue.pop();
  }
}

auto Table::build_turn_queue(PlayerId start) const -> std::queue<PlayerId> {
  std::queue<PlayerId> queue;
  if (!hand_state_) {
    return queue;
  }
  const auto &participants = hand_state_->participants;
  auto it = std::find(participants.begin(), participants.end(), start);
  if (it == participants.end()) {
    return queue;
  }
  const std::size_t offset =
      static_cast<std::size_t>(std::distance(participants.begin(), it));
  for (std::size_t i = 0; i < participants.size(); ++i) {
    auto id = participants[(offset + i) % participants.size()];
    auto st_it = hand_state_->player_state.find(id);
    if (st_it != hand_state_->player_state.end() &&
        st_it->second == PlayerState::active) {
      queue.push(id);
    }
  }
  return queue;
}

auto Table::first_active_after(PlayerId start) const
    -> std::optional<PlayerId> {
  if (!hand_state_) {
    return std::nullopt;
  }
  const auto &participants = hand_state_->participants;
  auto it = std::find(participants.begin(), participants.end(), start);
  if (it == participants.end()) {
    return std::nullopt;
  }
  const std::size_t offset =
      static_cast<std::size_t>(std::distance(participants.begin(), it));
  for (std::size_t i = 1; i <= participants.size(); ++i) {
    auto id = participants[(offset + i) % participants.size()];
    auto st_it = hand_state_->player_state.find(id);
    if (st_it != hand_state_->player_state.end() &&
        st_it->second == PlayerState::active) {
      return id;
    }
  }
  return std::nullopt;
}

auto Table::active_players_in_hand() const -> std::vector<PlayerId> {
  std::vector<PlayerId> remaining;
  if (!hand_state_) {
    return remaining;
  }
  for (auto id : hand_state_->participants) {
    auto it = hand_state_->player_state.find(id);
    if (it == hand_state_->player_state.end()) {
      continue;
    }
    if (it->second == PlayerState::active ||
        it->second == PlayerState::all_in) {
      remaining.push_back(id);
    }
  }
  return remaining;
}

void Table::post_blind(PlayerId id, Chips amount, std::vector<Event> &events) {
  const Chips chips = players_.get_chips(id);
  if (chips == 0) {
    hand_state_->player_state[id] = PlayerState::all_in;
    return;
  }
  Chips blind = std::min(amount, chips);
  if (blind >= chips) {
    hand_state_->player_state[id] = PlayerState::all_in;
  }
  players_.place_bet(id, blind);
  hand_state_->committed[id] += blind;
  hand_state_->active_bets[id] += blind;
  hand_state_->previous_bet =
      std::max(hand_state_->previous_bet, hand_state_->active_bets[id]);
  events.push_back(BetPlaced{id, blind});
}

void Table::reveal_remaining_board(std::vector<Event> &events) {
  while (hand_state_ && hand_state_->phase != Phase::river) {
    Phase next = hand_state_->phase;
    switch (hand_state_->phase) {
    case Phase::preflop:
      next = Phase::flop;
      break;
    case Phase::flop:
      next = Phase::turn;
      break;
    case Phase::turn:
      next = Phase::river;
      break;
    case Phase::river:
    case Phase::showdown:
    case Phase::holding:
      return;
    }
    hand_state_->phase = next;
    events.push_back(PhaseAdvanced{next});
    if (next == Phase::flop) {
      std::array<cards::Card, kFlopSize> flop{};
      std::copy_n(hand_state_->table_cards.begin(), kFlopSize, flop.begin());
      events.push_back(DealtFlop{flop});
    } else if (next == Phase::turn) {
      events.push_back(DealtStreet{hand_state_->table_cards[kFlopSize]});
    } else if (next == Phase::river) {
      events.push_back(DealtStreet{hand_state_->table_cards[kFlopSize + 1]});
    }
  }
}

void Table::advance_turn(std::vector<Event> &events) {
  prune_turn_queue();
  if (!hand_state_->turn_queue.empty()) {
    events.push_back(TurnAdvanced{hand_state_->turn_queue.front()});
  }
}

auto Table::build_side_pots() const -> std::vector<SidePot> {
  std::vector<SidePot> pots;
  if (!hand_state_) {
    return pots;
  }
  std::vector<std::pair<PlayerId, Chips>> contributions;
  contributions.reserve(hand_state_->committed.size());
  for (const auto &[id, amount] : hand_state_->committed) {
    if (amount > 0) {
      contributions.emplace_back(id, amount);
    }
  }
  if (contributions.empty()) {
    return pots;
  }
  std::sort(contributions.begin(), contributions.end(),
            [](const auto &a, const auto &b) { return a.second < b.second; });

  std::vector<PlayerId> remaining;
  remaining.reserve(contributions.size());
  for (const auto &[id, amount] : contributions) {
    (void)amount;
    remaining.push_back(id);
  }

  Chips previous = 0;
  std::size_t idx = 0;
  while (idx < contributions.size()) {
    const Chips level = contributions[idx].second;
    if (level > previous) {
      const Chips layer =
          (level - previous) * static_cast<Chips>(remaining.size());
      std::vector<PlayerId> eligible;
      eligible.reserve(remaining.size());
      for (auto id : remaining) {
        auto it = hand_state_->player_state.find(id);
        if (it != hand_state_->player_state.end() &&
            (it->second == PlayerState::active ||
             it->second == PlayerState::all_in)) {
          eligible.push_back(id);
        }
      }
      if (layer > 0) {
        pots.push_back(SidePot{layer, std::move(eligible)});
      }
      previous = level;
    }
    while (idx < contributions.size() && contributions[idx].second == level) {
      auto it = std::find(remaining.begin(), remaining.end(),
                          contributions[idx].first);
      if (it != remaining.end()) {
        remaining.erase(it);
      }
      ++idx;
    }
  }
  return pots;
}

auto Table::total_committed() const -> Chips {
  if (!hand_state_) {
    return 0;
  }
  Chips total = 0;
  for (const auto &[id, amount] : hand_state_->committed) {
    (void)id;
    total += amount;
  }
  return total;
}

auto Table::hand_rank(PlayerId id) const -> uint64_t {
  const auto &hole = hand_state_->player_holes.at(id);
  const auto &board = hand_state_->table_cards;
  std::array<cards::Card, kHoleSize + kBoardSize> cards{};
  cards[0] = hole[0];
  cards[1] = hole[1];
  std::copy(board.begin(), board.end(), cards.begin() + kHoleSize);
  return poker::rank_best_of_seven(cards);
}

void Table::award_chips(PlayerId id, Chips amount, std::vector<Event> &events) {
  if (amount == 0) {
    return;
  }
  players_.award_chips(id, amount);
  events.push_back(WonPot{id, amount});
}

void Table::distribute_side_pots(std::vector<Event> &events) {
  auto pots = build_side_pots();
  for (const auto &pot : pots) {
    if (pot.eligible.empty()) {
      continue;
    }
    uint64_t best = std::numeric_limits<uint64_t>::max();
    std::vector<PlayerId> winners;
    for (auto id : pot.eligible) {
      auto rank = hand_rank(id);
      if (winners.empty() || rank < best) {
        winners.clear();
        winners.push_back(id);
        best = rank;
      } else if (rank == best) {
        winners.push_back(id);
      }
    }
    if (winners.empty()) {
      continue;
    }

    std::vector<PlayerId> ordered;
    ordered.reserve(winners.size());
    for (auto id : hand_state_->participants) {
      if (std::find(winners.begin(), winners.end(), id) != winners.end()) {
        ordered.push_back(id);
      }
    }
    const Chips share = pot.amount / static_cast<Chips>(ordered.size());
    Chips remainder = pot.amount % static_cast<Chips>(ordered.size());
    for (auto id : ordered) {
      Chips payout = share;
      if (remainder > 0) {
        ++payout;
        --remainder;
      }
      award_chips(id, payout, events);
    }
  }
}

} // namespace poker
