#include <gtest/gtest.h>
#include <random>

#include "table.h"

TEST(PlayerManager, SeatAndCycle) {
  PlayerManager pm;
  ASSERT_TRUE(pm.add_player(1));
  ASSERT_TRUE(pm.add_player(2));
  pm.seat_held_players();

  auto first = pm.get_first_player();
  ASSERT_TRUE(first.has_value());

  auto cycle = pm.active_cycle_from(*first);
  EXPECT_EQ(cycle.size(), 2u);
  EXPECT_EQ(pm.next_player(*first).value(), cycle[1]);
}

TEST(Table, StartRequiresTwoPlayers) {
  std::mt19937_64 rng(0);
  Table table(rng);

  EXPECT_TRUE(table.add_player(1));
  auto start = table.handle_start();
  EXPECT_FALSE(start.has_value());
  EXPECT_EQ(start.error(), GameError::not_enough_players);
}

TEST(Table, StartsAndDeals) {
  std::mt19937_64 rng(0);
  Table table(rng);

  ASSERT_TRUE(table.add_player(1));
  ASSERT_TRUE(table.add_player(2));

  auto start = table.handle_start();
  EXPECT_TRUE(start.has_value());
}
