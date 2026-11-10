#pragma once

#include "mini_etg/types.hpp"

#include <array>

namespace mini_etg {

enum class BetKind : std::uint8_t {
    Straight, Split, Street, Corner, SixLine, Dozen, Column,
    Red, Black, Odd, Even, Low, High
};

// Every bet explicitly lists its covered numbers. Even-money bets need 18 slots.
// Only the first count entries are meaningful; canonical forms sort and zero-fill.
struct BetSelection {
    BetKind kind{BetKind::Straight};
    std::array<Number, 18> numbers{};
    std::uint8_t count{0};
};

struct BetRequest {
    SeatId seat{0};
    BetSelection selection{};
    Credits stake{0};
};

struct AcceptedBet {
    BetId id{0};
    BetRequest request{};
    std::uint8_t profit_odds{0}; // Fixed when accepted: 35 means 35:1 profit.
};

// Validate kind, number range, uniqueness, exact set/geometry; sort out's numbers.
// Includes zero splits, zero trios as Street, and first-four as Corner.
// All Error-returning free functions leave out unchanged on failure.
Error canonicalize(const BetSelection& input, BetSelection& out) noexcept;

// Convenience builders; the same geometry validator is used for all inputs.
Error make_inside_bet(BetKind kind, const Number* numbers,
                      std::size_t count, BetSelection& out) noexcept;
Error make_dozen(std::uint8_t group, BetSelection& out) noexcept; // group 1..3
Error make_column(std::uint8_t group, BetSelection& out) noexcept; // group 1..3
Error make_even_money_bet(BetKind kind, BetSelection& out) noexcept;

Error standard_profit_odds(BetKind kind, std::uint8_t& out) noexcept;
Error covers(const BetSelection& selection, Number result, bool& out) noexcept;

// Gross return, including the stake: stake * (profit_odds + 1), or 0 on a loss.
Error gross_return(const AcceptedBet& bet, Number result, Credits& out) noexcept;

} // namespace mini_etg
