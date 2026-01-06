#include "proto_translate.h"

#include <type_traits>

#include "cards.pb.h"

namespace poker {
namespace {

auto to_proto_server_error(ServerError err) -> ::poker::v1::Error::ServerError {
  using Proto = ::poker::v1::Error::ServerError;
  switch (err) {
  case ServerError::too_many_clients:
    return Proto::Error_ServerError_SERVERERROR_TOO_MANY_CLIENTS;
  case ServerError::all_tables_full:
    return Proto::Error_ServerError_SERVERERROR_ALL_TABLES_FULL;
  case ServerError::unspecified:
  default:
    return Proto::Error_ServerError_SERVERERROR_UNSPECIFIED;
  }
}

auto to_proto_player_mgmt_error(PlayerMgmtError err)
    -> ::poker::v1::Error::PlayerMgmtError {
  using Proto = ::poker::v1::Error::PlayerMgmtError;
  switch (err) {
  case PlayerMgmtError::not_enough_seats:
    return Proto::Error_PlayerMgmtError_PLAYERMGMTERROR_NOTENOUGHSEATS;
  case PlayerMgmtError::invalid_id:
    return Proto::Error_PlayerMgmtError_PLAYERMGMTERROR_INVALIDID;
  case PlayerMgmtError::player_not_found:
    return Proto::Error_PlayerMgmtError_PLAYERMGMTERROR_PLAYERNOTFOUND;
  case PlayerMgmtError::no_players:
    return Proto::Error_PlayerMgmtError_PLAYERMGMTERROR_NOPLAYERS;
  default:
    return Proto::Error_PlayerMgmtError_PLAYERMGMTERROR_UNSPECIFIED;
  }
}

auto to_proto_game_error(GameError err) -> ::poker::v1::Error::GameError {
  using Proto = ::poker::v1::Error::GameError;
  switch (err) {
  case GameError::invalid_action:
    return Proto::Error_GameError_GAMEERROR_INVALIDACTION;
  case GameError::hand_in_play:
    return Proto::Error_GameError_GAMEERROR_HANDINPLAY;
  case GameError::not_enough_players:
    return Proto::Error_GameError_GAMEERROR_NOTENOUGHPLAYERS;
  case GameError::insufficient_funds:
    return Proto::Error_GameError_GAMEERROR_INSUFFICIENTFUNDS;
  case GameError::bet_too_low:
    return Proto::Error_GameError_GAMEERROR_BETTOOLOW;
  case GameError::out_of_turn:
    return Proto::Error_GameError_GAMEERROR_OUTOFTURN;
  case GameError::no_such_player:
    return Proto::Error_GameError_GAMEERROR_NOSUCHPLAYER;
  default:
    return Proto::Error_GameError_GAMEERROR_UNSPECIFIED;
  }
}

auto to_proto_phase(Phase phase) -> ::poker::v1::Event::Phase {
  using Proto = ::poker::v1::Event::Phase;
  switch (phase) {
  case Phase::holding:
    return Proto::Event_Phase_PHASE_HOLDING;
  case Phase::preflop:
    return Proto::Event_Phase_PHASE_PREFLOP;
  case Phase::flop:
    return Proto::Event_Phase_PHASE_FLOP;
  case Phase::turn:
    return Proto::Event_Phase_PHASE_TURN;
  case Phase::river:
    return Proto::Event_Phase_PHASE_RIVER;
  case Phase::showdown:
    return Proto::Event_Phase_PHASE_SHOWDOWN;
  default:
    return Proto::Event_Phase_PHASE_UNSPECIFIED;
  }
}

auto to_proto_rank(cards::Rank rank) -> ::poker::v1::Rank {
  using Proto = ::poker::v1::Rank;
  switch (rank) {
  case cards::Rank::Two:
    return Proto::RANK_TWO;
  case cards::Rank::Three:
    return Proto::RANK_THREE;
  case cards::Rank::Four:
    return Proto::RANK_FOUR;
  case cards::Rank::Five:
    return Proto::RANK_FIVE;
  case cards::Rank::Six:
    return Proto::RANK_SIX;
  case cards::Rank::Seven:
    return Proto::RANK_SEVEN;
  case cards::Rank::Eight:
    return Proto::RANK_EIGHT;
  case cards::Rank::Nine:
    return Proto::RANK_NINE;
  case cards::Rank::Ten:
    return Proto::RANK_TEN;
  case cards::Rank::Jack:
    return Proto::RANK_JACK;
  case cards::Rank::Queen:
    return Proto::RANK_QUEEN;
  case cards::Rank::King:
    return Proto::RANK_KING;
  case cards::Rank::Ace:
    return Proto::RANK_ACE;
  default:
    return Proto::RANK_UNSPECIFIED;
  }
}

auto to_proto_suit(cards::Suit suit) -> ::poker::v1::Suit {
  using Proto = ::poker::v1::Suit;
  switch (suit) {
  case cards::Suit::Clubs:
    return Proto::SUIT_CLUBS;
  case cards::Suit::Diamonds:
    return Proto::SUIT_DIAMONDS;
  case cards::Suit::Hearts:
    return Proto::SUIT_HEARTS;
  case cards::Suit::Spades:
    return Proto::SUIT_SPADES;
  default:
    return Proto::SUIT_UNSPECIFIED;
  }
}

auto to_proto_card(const cards::Card &card) -> ::poker::v1::Card {
  ::poker::v1::Card out;
  out.set_rank(to_proto_rank(card.rank));
  out.set_suit(to_proto_suit(card.suit));
  return out;
}

} // namespace

auto to_proto_error(const Error &err) -> ::poker::v1::Error {
  ::poker::v1::Error out;
  std::visit(
      [&](const auto &e) {
        using T = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<T, ServerError>) {
          out.set_server_error(to_proto_server_error(e));
        } else if constexpr (std::is_same_v<T, PlayerMgmtError>) {
          out.set_player_mgmt_error(to_proto_player_mgmt_error(e));
        } else if constexpr (std::is_same_v<T, GameError>) {
          out.set_game_error(to_proto_game_error(e));
        }
      },
      err);
  return out;
}

auto to_proto_event(const Event &ev) -> ::poker::v1::Event {
  ::poker::v1::Event out;
  std::visit(
      [&](const auto &e) {
        using T = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<T, PlayerAdded>) {
          out.mutable_player_added()->set_who(e.who);
        } else if constexpr (std::is_same_v<T, PlayerRemoved>) {
          out.mutable_player_removed()->set_who(e.who);
        } else if constexpr (std::is_same_v<T, BetPlaced>) {
          auto *msg = out.mutable_bet_placed();
          msg->set_who(e.who);
          msg->set_amount(e.amount);
        } else if constexpr (std::is_same_v<T, TurnAdvanced>) {
          out.mutable_turn_advanced()->set_next(e.next);
        } else if constexpr (std::is_same_v<T, PhaseAdvanced>) {
          out.mutable_phase_advanced()->set_next(to_proto_phase(e.next));
        } else if constexpr (std::is_same_v<T, WonPot>) {
          auto *msg = out.mutable_won_pot();
          msg->set_who(e.who);
          msg->set_amount(e.amount);
        } else if constexpr (std::is_same_v<T, HandStarted>) {
          out.mutable_hand_started();
        } else if constexpr (std::is_same_v<T, DealtHole>) {
          auto *msg = out.mutable_dealt_hole();
          msg->set_who(e.who);
          for (const auto &c : e.hole) {
            *msg->add_hole() = to_proto_card(c);
          }
        } else if constexpr (std::is_same_v<T, DealtFlop>) {
          auto *msg = out.mutable_dealt_flop();
          for (const auto &c : e.flop) {
            *msg->add_flop() = to_proto_card(c);
          }
        } else if constexpr (std::is_same_v<T, DealtStreet>) {
          *out.mutable_dealt_street()->mutable_street() = to_proto_card(e.street);
        }
      },
      ev);
  return out;
}

} // namespace poker
