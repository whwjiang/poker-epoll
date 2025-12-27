#pragma once

#include <cstdint>

#include "poker_rules.h"

using PlayerId = uint64_t;

class Player {
public:
  explicit Player(PlayerId id);

  PlayerId id() const;

  void add_chips(Chips chips);

  bool sufficient_chips(Chips bet) const;

  void place_bet(Chips bet);

private:
  PlayerId id_;
  Chips purse_{0};
};
