#pragma once

#include <array>
#include <cstdint>

#include "cards.h"

namespace poker {

using HandRank = uint64_t;

auto rank_best_of_seven(const std::array<cards::Card, 7> &cards) -> HandRank;

} // namespace poker
