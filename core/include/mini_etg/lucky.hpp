#pragma once

#include "mini_etg/bet.hpp"
#include "mini_etg/random.hpp"

#include <array>

namespace mini_etg {

inline constexpr std::size_t kMaxLuckyNumbers = 5;
inline constexpr std::size_t kMaxMultiplierEntries = 8;

struct MultiplierWeight {
    std::uint16_t gross_multiplier{0}; // Total stake return, not profit odds.
    std::uint32_t weight{0};
};

struct LuckyConfig {
    bool enabled{false};
    std::uint8_t base_straight_profit_odds{29}; // 0..35, used only when enabled.
    std::uint8_t min_count{1};
    std::uint8_t max_count{5}; // Count is uniform in this inclusive range.
    std::array<MultiplierWeight, kMaxMultiplierEntries> table{{
        {50, 48}, {100, 30}, {200, 20}, {500, 2}
    }};
    std::size_t table_count{4};
};

struct LuckyNumber {
    Number number{};
    std::uint16_t gross_multiplier{0};
};

struct LuckyDraw {
    // Zero count means not drawn/disabled. Nonempty draws have distinct numbers.
    std::array<LuckyNumber, kMaxLuckyNumbers> entries{};
    std::uint8_t count{0};
};

// Validate configuration even when disabled. Active weights must be nonzero,
// total weight must fit uint32, and multipliers must exceed the base gross return.
Error validate_lucky_config(const LuckyConfig& config) noexcept;
// Expects a full draw when enabled, or a zero/default draw when disabled.
Error validate_lucky_draw(const LuckyConfig& config, const LuckyDraw& draw) noexcept;
// Partial Fisher-Yates selection with unbiased bounded draws; no repeated numbers.
// Disabled mode returns an empty draw without consuming randomness.
Error draw_lucky_numbers(IRandom& random, const LuckyConfig& config, LuckyDraw& out) noexcept;

// All outputs stay unchanged on error. Non-straight bets retain standard odds.
Error base_profit_odds(BetKind kind, const LuckyConfig& config, std::uint8_t& out) noexcept;
Error base_gross_return(const AcceptedBet& bet, Number result,
                        const LuckyConfig& config, Credits& out) noexcept;
Error maximum_gross_return(const AcceptedBet& bet, Number result,
                           const LuckyConfig& config, Credits& out) noexcept;
Error lucky_gross_return(const AcceptedBet& bet, Number result,
                         const LuckyConfig& config, const LuckyDraw& draw,
                         Credits& out) noexcept;

// Analysis only: floating point represents probabilities, never ledger credits.
struct LuckyMath {
    double expected_count{0};
    double expected_multiplier{0};
    double straight_rtp{0};
    double straight_return_variance{0}; // One credit on one fixed number per spin.
};
Error analyze_lucky(const LuckyConfig& config, LuckyMath& out) noexcept;

} // namespace mini_etg
