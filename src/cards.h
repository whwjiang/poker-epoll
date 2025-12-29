#pragma once

// #include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace cards {
// value enums
enum class Rank : uint8_t {
  Two,
  Three,
  Four,
  Five,
  Six,
  Seven,
  Eight,
  Nine,
  Ten,
  Jack,
  Queen,
  King,
  Ace
};

enum class Suit : uint8_t { Clubs, Diamonds, Hearts, Spades };

constexpr char to_char(Suit s) {
  switch (s) {
  case Suit::Clubs:
    return 'c';
  case Suit::Diamonds:
    return 'd';
  case Suit::Hearts:
    return 'h';
  case Suit::Spades:
    return 's';
  default:
    return 'e';
  }
}

constexpr char to_char(Rank r) {
  switch (r) {
  case Rank::Ten:
    return 'T';
  case Rank::Jack:
    return 'J';
  case Rank::Queen:
    return 'Q';
  case Rank::King:
    return 'K';
  case Rank::Ace:
    return 'A';
  default:
    return static_cast<char>(std::to_underlying(r) + '2');
  }
}

using CardId = uint8_t;
struct Card {
  Rank rank;
  Suit suit;

  inline std::string to_string() const {
    return std::string{""} + to_char(rank) + to_char(suit);
  }
};

// CardId cardToId(Card c) { return 13 * std::to_underlying(c.suit) + std::to_underlying(c.rank); }

} // namespace cards
