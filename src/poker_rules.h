#pragma once

#include <cstddef>
#include <cstdint>


inline constexpr std::size_t kStreetSize = 1;
inline constexpr std::size_t kHoleSize = 2;
inline constexpr std::size_t kFlopSize = 3;
inline constexpr std::size_t kBoardSize = 5;
inline constexpr std::size_t kDeckSize = 52;

inline constexpr std::size_t kMaxPlayers = 10;

// TODO: make this an abstract factory
using Chips = uint64_t;
inline constexpr Chips kBuyIn = 1000;
inline constexpr Chips kBigBlind = 10;
inline constexpr Chips kSmallBlind = 5;
