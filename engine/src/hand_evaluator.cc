#include "hand_evaluator.h"

#include <array>
#include <phevaluator/phevaluator.h>

namespace poker {

auto rank_best_of_seven(const std::array<cards::Card, 7> &cards) -> HandRank {
  auto rank = phevaluator::EvaluateCards(
      cards[0].to_string(), cards[1].to_string(), cards[2].to_string(),
      cards[3].to_string(), cards[4].to_string(), cards[5].to_string(),
      cards[6].to_string());
  return static_cast<HandRank>(rank.value());
}

} // namespace poker
