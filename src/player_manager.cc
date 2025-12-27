#include "player_manager.h"

#include <algorithm>
#include <iterator>
#include <numeric>

PlayerManager::PlayerManager() : seats_(kMaxPlayers), open_seats_(kMaxPlayers) {
  std::iota(open_seats_.begin(), open_seats_.end(), 0);
}

auto PlayerManager::add_player(PlayerId id)
    -> std::expected<void, PlayerMgmtError> {
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
auto PlayerManager::remove_player(PlayerId id)
    -> std::expected<void, PlayerMgmtError> {
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
void PlayerManager::seat_held_players() {
  while (!holding_.empty()) {
    Player cur(holding_.front());
    holding_.pop_front();
    cur.add_chips(kBuyIn);
    seats_[index_[cur.id()]] = cur;
  }
}

// Finalize removals (end of hand).
void PlayerManager::finalize_leavers() {
  for (auto id : leaving_) {
    const auto seat = index_[id];
    seats_[seat].reset();
    open_seats_.push_back(seat);
    index_.erase(id);
  }
  leaving_.clear();
}

auto PlayerManager::get_first_player() const
    -> std::expected<PlayerId, PlayerMgmtError> {
  for (auto it = seats_.begin(); it != seats_.end(); it = std::next(it)) {
    if (*it && !is_leaving((*it)->id())) {
      return (*it)->id();
    }
  }
  return std::unexpected(PlayerMgmtError::no_players);
}

auto PlayerManager::next_player(PlayerId p) const
    -> std::expected<PlayerId, PlayerMgmtError> {
  if (!index_.count(p)) {
    return std::unexpected(PlayerMgmtError::invalid_id);
  }
  const auto total = seats_.size();
  auto it = std::next(seats_.begin() + index_.at(p));
  for (std::size_t checked = 0; checked < total; ++checked, it = std::next(it)) {
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

std::vector<PlayerId> PlayerManager::active_cycle_from(PlayerId start) const {
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

std::size_t PlayerManager::seated_count() const {
  return kMaxPlayers - open_seats_.size();
}

bool PlayerManager::is_leaving(PlayerId id) const { return leaving_.count(id) > 0; }
