#include "hand_evaluator.h"

#include <algorithm>
#include <array>
#include <functional>
#include <vector>

namespace poker {

namespace {

constexpr int kCategoryShift = 60;
constexpr int kNibbleBits = 4;

int rank_value(cards::Rank r) {
  return static_cast<int>(r) + 2;
}

HandRank make_rank(int category, const std::vector<int> &kickers) {
  HandRank out = static_cast<HandRank>(category) << kCategoryShift;
  int shift = kCategoryShift - kNibbleBits;
  for (int r : kickers) {
    out |= static_cast<HandRank>(r) << shift;
    shift -= kNibbleBits;
  }
  return out;
}

HandRank rank_five(const std::array<cards::Card, 5> &cards) {
  std::array<int, 5> ranks{};
  std::array<int, 5> suits{};
  for (std::size_t i = 0; i < cards.size(); ++i) {
    ranks[i] = rank_value(cards[i].rank);
    suits[i] = static_cast<int>(cards[i].suit);
  }

  std::array<int, 15> counts{};
  for (int r : ranks) {
    ++counts[r];
  }

  bool is_flush = std::all_of(suits.begin() + 1, suits.end(),
                              [&](int s) { return s == suits[0]; });

  int mask = 0;
  for (int r : ranks) {
    mask |= 1 << r;
  }

  int straight_high = 0;
  for (int high = 14; high >= 5; --high) {
    int seq = 0x1F << (high - 4);
    if ((mask & seq) == seq) {
      straight_high = high;
      break;
    }
  }
  if (straight_high == 0) {
    int wheel = (1 << 14) | (1 << 5) | (1 << 4) | (1 << 3) | (1 << 2);
    if ((mask & wheel) == wheel) {
      straight_high = 5;
    }
  }

  std::vector<std::pair<int, int>> grouped;
  grouped.reserve(5);
  for (int r = 14; r >= 2; --r) {
    if (counts[r] > 0) {
      grouped.emplace_back(counts[r], r);
    }
  }
  std::sort(grouped.begin(), grouped.end(), [](const auto &a, const auto &b) {
    if (a.first != b.first) {
      return a.first > b.first;
    }
    return a.second > b.second;
  });

  if (straight_high > 0 && is_flush) {
    return make_rank(8, {straight_high});
  }
  if (grouped[0].first == 4) {
    int quad = grouped[0].second;
    int kicker = grouped[1].second;
    return make_rank(7, {quad, kicker});
  }
  if (grouped[0].first == 3 && grouped[1].first == 2) {
    return make_rank(6, {grouped[0].second, grouped[1].second});
  }
  if (is_flush) {
    std::vector<int> ordered(ranks.begin(), ranks.end());
    std::sort(ordered.begin(), ordered.end(), std::greater<>());
    return make_rank(5, ordered);
  }
  if (straight_high > 0) {
    return make_rank(4, {straight_high});
  }
  if (grouped[0].first == 3) {
    std::vector<int> kickers = {grouped[0].second};
    for (std::size_t i = 1; i < grouped.size(); ++i) {
      if (grouped[i].first == 1) {
        kickers.push_back(grouped[i].second);
      }
    }
    return make_rank(3, kickers);
  }
  if (grouped[0].first == 2 && grouped[1].first == 2) {
    int high_pair = grouped[0].second;
    int low_pair = grouped[1].second;
    int kicker = grouped[2].second;
    return make_rank(2, {high_pair, low_pair, kicker});
  }
  if (grouped[0].first == 2) {
    std::vector<int> kickers = {grouped[0].second};
    for (std::size_t i = 1; i < grouped.size(); ++i) {
      if (grouped[i].first == 1) {
        kickers.push_back(grouped[i].second);
      }
    }
    return make_rank(1, kickers);
  }

  std::vector<int> ordered(ranks.begin(), ranks.end());
  std::sort(ordered.begin(), ordered.end(), std::greater<>());
  return make_rank(0, ordered);
}

} // namespace

auto rank_best_of_seven(const std::array<cards::Card, 7> &cards) -> HandRank {
  HandRank best = 0;
  for (std::size_t a = 0; a < cards.size(); ++a) {
    for (std::size_t b = a + 1; b < cards.size(); ++b) {
      for (std::size_t c = b + 1; c < cards.size(); ++c) {
        for (std::size_t d = c + 1; d < cards.size(); ++d) {
          for (std::size_t e = d + 1; e < cards.size(); ++e) {
            std::array<cards::Card, 5> five{cards[a], cards[b], cards[c],
                                            cards[d], cards[e]};
            best = std::max(best, rank_five(five));
          }
        }
      }
    }
  }
  return best;
}

} // namespace poker
