#include "player.h"

Player::Player(PlayerId id) : id_(id) {}

PlayerId Player::id() const { return id_; }

void Player::add_chips(Chips chips) { purse_ += chips; }

bool Player::sufficient_chips(Chips bet) const { return bet <= purse_; }

void Player::place_bet(Chips bet) { purse_ -= bet; }
