#pragma once

#include "mini_etg/model.hpp"
#include "mini_etg/random.hpp"

namespace mini_etg::sim {

struct BotDecision {
    bool place{false};
    BetRequest request{}; // Meaningful only when place is true.
};

// A fixed strategy per seat: zero, random straight, red, alternating odd/even,
// rotating dozen, rotating column, random horizontal split, cycling inside bet.
// Stakes are 5 credits for inside bets and 10 for the other selections.
const char* bot_name(SeatId seat) noexcept;

// BetsOpen only. Choose at most one placement per seat per round; a seat with
// an existing bet is skipped. Reduce a fixed stake to affordable table limits,
// or skip if it falls below the minimum. Skips consume no random words.
// This proposes a request, never changes credits or decides a payout. Engine
// remains responsible for accepting/rejecting it. On error out is unchanged.
Error choose_bot_bet(const Snapshot& snapshot, SeatId seat,
                     IRandom& random, BotDecision& out) noexcept;

} // namespace mini_etg::sim
