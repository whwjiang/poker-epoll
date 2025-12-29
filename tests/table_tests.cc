#include <gtest/gtest.h>
#include <random>
#include <variant>
#include <vector>

#include "table.h"

namespace {

template <typename T>
auto collect(const std::vector<Event> &events) -> std::vector<T> {
  std::vector<T> out;
  for (const auto &event : events) {
    if (auto value = std::get_if<T>(&event)) {
      out.push_back(*value);
    }
  }
  return out;
}

} // namespace

TEST(Table, StartRequiresTwoPlayers) {
  std::mt19937_64 rng(0);
  Table table(rng);

  EXPECT_TRUE(table.add_player(1));
  auto start = table.handle_new_hand();
  EXPECT_FALSE(start.has_value());
  EXPECT_EQ(start.error(), GameError::not_enough_players);
}

TEST(Table, StartsAndDeals) {
  std::mt19937_64 rng(0);
  Table table(rng);

  ASSERT_TRUE(table.add_player(1));
  ASSERT_TRUE(table.add_player(2));

  auto start = table.handle_new_hand();
  EXPECT_TRUE(start.has_value());
}

TEST(Table, HeadsUpBlindsAndTurn) {
  std::mt19937_64 rng(0);
  Table table(rng);

  ASSERT_TRUE(table.add_player(1));
  ASSERT_TRUE(table.add_player(2));

  auto start = table.handle_new_hand();
  ASSERT_TRUE(start.has_value());

  auto bets = collect<BetPlaced>(*start);
  ASSERT_EQ(bets.size(), 2u);
  EXPECT_EQ(bets[0].who, 1u);
  EXPECT_EQ(bets[0].amount, kSmallBlind);
  EXPECT_EQ(bets[1].who, 2u);
  EXPECT_EQ(bets[1].amount, kBigBlind);

  auto turns = collect<TurnAdvanced>(*start);
  ASSERT_EQ(turns.size(), 1u);
  EXPECT_EQ(turns[0].next, 1u);
}

TEST(Table, TimeoutFoldsWhenBehind) {
  std::mt19937_64 rng(0);
  Table table(rng);

  ASSERT_TRUE(table.add_player(1));
  ASSERT_TRUE(table.add_player(2));

  auto start = table.handle_new_hand();
  ASSERT_TRUE(start.has_value());

  auto res = table.on_action(Timeout{1});
  ASSERT_TRUE(res.has_value());

  auto wins = collect<WonPot>(*res);
  ASSERT_EQ(wins.size(), 1u);
  EXPECT_EQ(wins[0].who, 2u);
  EXPECT_EQ(wins[0].amount, kSmallBlind + kBigBlind);
}

TEST(Table, TimeoutChecksWhenEven) {
  std::mt19937_64 rng(0);
  Table table(rng);

  ASSERT_TRUE(table.add_player(1));
  ASSERT_TRUE(table.add_player(2));

  auto start = table.handle_new_hand();
  ASSERT_TRUE(start.has_value());

  auto call = table.on_action(Bet{1, kBigBlind - kSmallBlind});
  ASSERT_TRUE(call.has_value());

  auto res = table.on_action(Timeout{2});
  ASSERT_TRUE(res.has_value());

  auto bets = collect<BetPlaced>(*res);
  ASSERT_EQ(bets.size(), 1u);
  EXPECT_EQ(bets[0].who, 2u);
  EXPECT_EQ(bets[0].amount, 0u);

  auto phases = collect<PhaseAdvanced>(*res);
  ASSERT_EQ(phases.size(), 1u);
  EXPECT_EQ(phases[0].next, Phase::flop);
}

TEST(Table, HeadsUpAllInCompletesHand) {
  std::mt19937_64 rng(0);
  Table table(rng);

  ASSERT_TRUE(table.add_player(1));
  ASSERT_TRUE(table.add_player(2));

  auto start = table.handle_new_hand();
  ASSERT_TRUE(start.has_value());

  auto shove = table.on_action(Bet{1, kBuyIn});
  ASSERT_TRUE(shove.has_value());
  auto call = table.on_action(Bet{2, kBuyIn});
  ASSERT_TRUE(call.has_value());

  auto wins = collect<WonPot>(*call);
  Chips total = 0;
  for (const auto &win : wins) {
    total += win.amount;
  }
  EXPECT_EQ(total, kBuyIn * 2);
}

TEST(Table, ButtonAdvancesBetweenHands) {
  std::mt19937_64 rng(0);
  Table table(rng);

  ASSERT_TRUE(table.add_player(1));
  ASSERT_TRUE(table.add_player(2));
  ASSERT_TRUE(table.add_player(3));

  auto start1 = table.handle_new_hand();
  ASSERT_TRUE(start1.has_value());

  auto bets1 = collect<BetPlaced>(*start1);
  ASSERT_EQ(bets1.size(), 2u);
  EXPECT_EQ(bets1[0].who, 2u);
  EXPECT_EQ(bets1[1].who, 3u);

  auto end1 = table.on_action(Fold{1});
  ASSERT_TRUE(end1.has_value());
  auto end2 = table.on_action(Timeout{2});
  ASSERT_TRUE(end2.has_value());

  auto start2 = table.handle_new_hand();
  ASSERT_TRUE(start2.has_value());

  auto bets2 = collect<BetPlaced>(*start2);
  ASSERT_EQ(bets2.size(), 2u);
  EXPECT_EQ(bets2[0].who, 3u);
  EXPECT_EQ(bets2[1].who, 1u);
}

TEST(Table, RemovePlayerBeforeHandPreventsStart) {
  std::mt19937_64 rng(0);
  Table table(rng);

  ASSERT_TRUE(table.add_player(1));
  ASSERT_TRUE(table.add_player(2));

  auto removed = table.remove_player(2);
  ASSERT_TRUE(removed.has_value());

  auto start = table.handle_new_hand();
  EXPECT_FALSE(start.has_value());
  EXPECT_EQ(start.error(), GameError::not_enough_players);
}

TEST(Table, RemovePlayerOnTurnAdvancesGame) {
  std::mt19937_64 rng(0);
  Table table(rng);

  ASSERT_TRUE(table.add_player(1));
  ASSERT_TRUE(table.add_player(2));
  ASSERT_TRUE(table.add_player(3));

  auto start = table.handle_new_hand();
  ASSERT_TRUE(start.has_value());

  auto removed = table.remove_player(1);
  ASSERT_TRUE(removed.has_value());

  auto turns = collect<TurnAdvanced>(*removed);
  ASSERT_EQ(turns.size(), 1u);

  auto res = table.on_action(Timeout{turns[0].next});
  EXPECT_TRUE(res.has_value());
}

TEST(Table, RemovePlayerOffTurnStillAllowsProgress) {
  std::mt19937_64 rng(0);
  Table table(rng);

  ASSERT_TRUE(table.add_player(1));
  ASSERT_TRUE(table.add_player(2));
  ASSERT_TRUE(table.add_player(3));

  auto start = table.handle_new_hand();
  ASSERT_TRUE(start.has_value());

  auto removed = table.remove_player(2);
  ASSERT_TRUE(removed.has_value());

  auto res = table.on_action(Timeout{1});
  ASSERT_TRUE(res.has_value());

  auto wins = collect<WonPot>(*res);
  ASSERT_EQ(wins.size(), 1u);
  EXPECT_EQ(wins[0].who, 3u);
}
