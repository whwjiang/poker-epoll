#pragma once

#include <algorithm>
#include <array>
#include <expected>
#include <iterator>
#include <numeric>
#include <string>

#include "cards.h"
#include "poker_rules.h"

namespace cards {

inline constexpr std::array<Card, kDeckSize> kCardIdMap = [] {
  std::array<Card, kDeckSize> out{};
  for (uint8_t s = 0; s < 4; ++s)
    for (uint8_t r = 0; r < 13; ++r)
      out[s * 13 + r] = Card{static_cast<Rank>(r), static_cast<Suit>(s)};
  return out;
}();

class Deck {
public:
  Deck() = default;

  template <class URBG> void shuffle(URBG &g) {
    std::ranges::shuffle(cards_, g);
    next = 0;
  }

  // Generates a string representing the remaining cards in the deck.
  std::string to_string() const {
    auto space_fold = [](std::string a, Card c) {
      return std::move(a) + ' ' + c.to_string();
    };

    return std::accumulate(std::next(cards_.begin() + next), cards_.end(),
                           cards_[next].to_string(), space_fold);
  }

  enum class DealError { invalid_amount, out_of_cards };

  auto deal_hole() -> std::expected<std::array<Card, kHoleSize>, DealError> {
    return deal<kHoleSize>();
  }
  auto deal_board() -> std::expected<std::array<Card, kBoardSize>, DealError> {
    return deal<kBoardSize>();
  }

private:
  std::array<Card, kDeckSize> cards_{kCardIdMap};
  unsigned next{0};

  template <std::size_t N>
  auto deal() -> std::expected<std::array<Card, N>, DealError> {
    if (next == kDeckSize) {
      return std::unexpected(DealError::out_of_cards);
    }
    if (next + N > kDeckSize) {
      return std::unexpected(DealError::invalid_amount);
    }
    std::array<Card, N> res;
    std::copy_n(cards_.begin() + next, N, res.begin());
    next += N;
    return res;
  }
};

} // namespace cards
