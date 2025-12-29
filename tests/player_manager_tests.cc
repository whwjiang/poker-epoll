#include <gtest/gtest.h>

#include "player_manager.h"

TEST(PlayerManager, AddPlayerFailsWhenFull) {
  PlayerManager pm;
  for (std::size_t i = 0; i < kMaxPlayers; ++i) {
    ASSERT_TRUE(pm.add_player(i + 1));
  }
  auto extra = pm.add_player(kMaxPlayers + 1);
  ASSERT_FALSE(extra.has_value());
  EXPECT_EQ(extra.error(), PlayerMgmtError::not_enough_seats);
}

TEST(PlayerManager, RemoveHeldPlayerFreesSeat) {
  PlayerManager pm;
  ASSERT_TRUE(pm.add_player(1));
  ASSERT_TRUE(pm.remove_player(1));
  EXPECT_FALSE(pm.is_sat(1));

  for (std::size_t i = 0; i < kMaxPlayers; ++i) {
    ASSERT_TRUE(pm.add_player(100 + i));
  }
  auto extra = pm.add_player(999);
  EXPECT_FALSE(extra.has_value());
  EXPECT_EQ(extra.error(), PlayerMgmtError::not_enough_seats);
}

TEST(PlayerManager, SeatHeldPlayersAndCycle) {
  PlayerManager pm;
  ASSERT_TRUE(pm.add_player(1));
  ASSERT_TRUE(pm.add_player(2));
  EXPECT_EQ(pm.seated_count(), 2u);

  pm.seat_held_players();
  EXPECT_TRUE(pm.is_sat(1));
  EXPECT_TRUE(pm.is_sat(2));

  auto first = pm.get_first_player();
  ASSERT_TRUE(first.has_value());
  auto cycle = pm.active_cycle_from(*first);
  EXPECT_EQ(cycle.size(), 2u);
  EXPECT_EQ(pm.next_player(*first).value(), cycle[1]);
}

TEST(PlayerManager, RemoveSeatedPlayerFreesSeatImmediately) {
  PlayerManager pm;
  ASSERT_TRUE(pm.add_player(1));
  ASSERT_TRUE(pm.add_player(2));
  pm.seat_held_players();

  ASSERT_TRUE(pm.remove_player(1));
  EXPECT_FALSE(pm.is_sat(1));
  EXPECT_EQ(pm.seated_count(), 1u);

  auto first = pm.get_first_player();
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ(*first, 2u);

  EXPECT_EQ(pm.seated_count(), 1u);
}

TEST(PlayerManager, RemoveInvalidPlayerReturnsError) {
  PlayerManager pm;
  auto res = pm.remove_player(42);
  ASSERT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), PlayerMgmtError::invalid_id);
}

TEST(PlayerManager, NextPlayerWrapsAndHandlesInvalid) {
  PlayerManager pm;
  ASSERT_TRUE(pm.add_player(1));
  ASSERT_TRUE(pm.add_player(2));
  ASSERT_TRUE(pm.add_player(3));
  pm.seat_held_players();

  auto next = pm.next_player(3);
  ASSERT_TRUE(next.has_value());
  EXPECT_EQ(*next, 1u);

  auto invalid = pm.next_player(99);
  ASSERT_FALSE(invalid.has_value());
  EXPECT_EQ(invalid.error(), PlayerMgmtError::invalid_id);
}

TEST(PlayerManager, ActiveCycleSkipsRemovedAndInvalid) {
  PlayerManager pm;
  ASSERT_TRUE(pm.add_player(1));
  ASSERT_TRUE(pm.add_player(2));
  ASSERT_TRUE(pm.add_player(3));
  pm.seat_held_players();
  ASSERT_TRUE(pm.remove_player(2));

  auto cycle = pm.active_cycle_from(1);
  ASSERT_EQ(cycle.size(), 2u);
  EXPECT_EQ(cycle[0], 1u);
  EXPECT_EQ(cycle[1], 3u);

  auto removed_cycle = pm.active_cycle_from(2);
  EXPECT_TRUE(removed_cycle.empty());

  auto invalid_cycle = pm.active_cycle_from(99);
  EXPECT_TRUE(invalid_cycle.empty());
}

TEST(PlayerManager, BettingValidationAndPlacement) {
  PlayerManager pm;
  ASSERT_TRUE(pm.add_player(1));
  pm.seat_held_players();

  EXPECT_TRUE(pm.has_enough_chips(1, kBuyIn));
  EXPECT_FALSE(pm.has_enough_chips(1, kBuyIn + 1));

  pm.place_bet(1, kBuyIn);
  EXPECT_FALSE(pm.has_enough_chips(1, 1));
}
