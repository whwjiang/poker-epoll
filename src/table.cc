#include "table.h"

#include <iterator>

Table::Table(std::mt19937_64 &rng) : rng_(rng) {}

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
    if (hand_state_->turn_queue.front() == id) {
      // case 3
      hand_state_->turn_queue.pop();
      res.push_back(TurnAdvanced{hand_state_->turn_queue.front()});
    }
  }
  return res;
}

// assume that all actions will happen serially. any driver needs to ensure
// this so as to avoid race conditions or inconsistent state
auto Table::handle_new_hand() -> std::expected<std::vector<Event>, GameError> {
  players_.finalize_leavers();
  if (players_.seated_count() < 2) {
    return std::unexpected(GameError::not_enough_players);
  }
  if (hand_state_.has_value()) {
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
  state.phase = Phase::preflop;
  hand_state_ = std::move(state);
  // needs to send out events for:
  // 1 dealt hands
  // 2 whose turn
  // 3 min bet
  // 4 table cards
  std::vector<Event> events;
  events.resize(12);
  events.push_back(PhaseAdvanced{Phase::preflop});

  return events;
}

auto Table::handle_new_street() -> std::expected<std::vector<Event>, GameError> {
  std::vector<Event> events;
  return events;
}

auto Table::handle_bet() -> std::expected<std::vector<Event>, GameError> {
  return {};
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
