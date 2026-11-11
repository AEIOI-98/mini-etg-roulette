#pragma once

#include "mini_etg/bet.hpp"
#include "mini_etg/lucky.hpp"

#include <array>

namespace mini_etg {

struct TableLimits {
    Credits min_per_bet{1};
    Credits max_per_bet{100};
    Credits max_total_per_seat{1000};
};

struct EngineConfig {
    TableLimits limits{};
    TickMs no_spin_timeout_ms{2000};
    TickMs ball_timeout_ms{30000}; // 0 < no-spin < ball < 2^31; from start_spin.
    LuckyConfig lucky{};
};

struct Meters {
    Credits credits_in{0};   // External credit loads, including initial funding.
    Credits credits_out{0};  // External withdrawals only.
    std::uint64_t games_played{0}; // Non-void completed rounds with active stakes at close.
    std::uint64_t games_won{0}; // Participating, completed rounds with net profit > 0.
};

struct Seat {
    Credits credits{0};
    Meters meters{};
};

enum class Resolution : std::uint8_t { None, Pending, Paid, Refunded };

struct Round {
    RoundId id{0};
    RoundState state{RoundState::Idle};
    Resolution resolution{Resolution::None};
    FaultCode fault{FaultCode::None};
    std::array<AcceptedBet, kMaxBets> bets{};
    std::size_t bet_count{0};
    bool has_result{false};
    PocketIndex result_index{};
    Number result_number{};
    Credits escrow{0};
    Credits opening_total{0}; // All seat balances + cumulative house result at open.
    LuckyDraw lucky{}; // Drawn and journalled atomically with BetsClosed.
};

// This is the complete durable ledger, not a raw binary storage format.
struct Snapshot {
    std::uint32_t format_version{2}; // Version 1 had no multiplier config/draw.
    Revision revision{0};
    Sequence last_sequence{0};
    BetId next_bet_id{1};
    EngineConfig config{};
    std::array<Seat, kSeatCount> seats{};
    Credits house_result{0}; // Cumulative signed net house profit.
    Round round{};
};

Error validate_config(const EngineConfig& config) noexcept;

// Validate a complete durable snapshot before use. Malformed schema, metadata,
// states, bets, result or accounting returns StorageCorrupt; never repair money.
Error validate_snapshot(const Snapshot& snapshot) noexcept;

// Checks global conservation including escrow against credits_in - credits_out.
// Also checks opening_total for the current round, including payout/refund.
// Idle retains historical round data; its old opening_total is not compared.
Error check_accounting(const Snapshot& snapshot) noexcept;

} // namespace mini_etg
