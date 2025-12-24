#include <gtest/gtest.h>
#include <random>

#include "cards.h"
#include "deck.h"

TEST(Cards, ToChar) {
  EXPECT_EQ(cards::to_char(cards::Rank::Ace), 'A');
  EXPECT_EQ(cards::to_char(cards::Rank::Two), '2');
  EXPECT_EQ(cards::to_char(cards::Suit::Hearts), 'h');
}

TEST(Deck, DealsSequentially) {
  std::mt19937_64 rng(0);
  cards::Deck deck;
  deck.shuffle(rng);

  auto hole = deck.deal_hole();
  ASSERT_TRUE(hole.has_value());
  EXPECT_EQ(hole->at(0).rank, cards::Rank::Ace);
  EXPECT_EQ(hole->at(0).suit, cards::Suit::Spades);

  EXPECT_EQ(hole->at(1).rank, cards::Rank::King);
  EXPECT_EQ(hole->at(1).suit, cards::Suit::Spades);
}

TEST(Deck, ResetDeal) {
  cards::Deck deck;
  {
    std::mt19937_64 rng(0);
    deck.shuffle(rng);
  }

  auto hole = deck.deal_hole();
  ASSERT_TRUE(hole.has_value());
  EXPECT_EQ(hole->at(0).rank, cards::Rank::Ace);
  EXPECT_EQ(hole->at(0).suit, cards::Suit::Spades);

  deck.reset();
  {
    std::mt19937_64 rng(0);
    deck.shuffle(rng);
  }
  hole = deck.deal_hole();
  ASSERT_TRUE(hole.has_value());
  EXPECT_EQ(hole->at(0).rank, cards::Rank::Ace);
  EXPECT_EQ(hole->at(0).suit, cards::Suit::Spades);
}

TEST(Deck, OutOfCards) {
  std::mt19937_64 rng(0);
  cards::Deck deck;
  deck.shuffle(rng);

  for (int i = 0; i < 26; ++i) {
    deck.deal_hole();
  }

  auto res = deck.deal_hole();
  EXPECT_FALSE(res.has_value());
  EXPECT_EQ(res.error(), cards::Deck::DealError::out_of_cards);
}
