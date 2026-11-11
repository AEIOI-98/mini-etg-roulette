#pragma once

#include <cstddef>
#include <cstdint>

namespace mini_etg {

using Credits = std::int64_t;
using SeatId = std::uint8_t;
using RoundId = std::uint64_t;
using BetId = std::uint64_t;
using Sequence = std::uint64_t;
using Revision = std::uint64_t;
using TickMs = std::uint32_t;

inline constexpr std::size_t kPocketCount = 37;
inline constexpr std::size_t kSeatCount = 8;
inline constexpr std::size_t kMaxBetsPerSeat = 16;
inline constexpr std::size_t kMaxBets = kSeatCount * kMaxBetsPerSeat;

// Different types prevent confusing a sensor position with a printed number.
struct PocketIndex { std::uint8_t value{0}; };
struct Number { std::uint8_t value{0}; };

enum class Error : std::uint8_t {
    Ok,
    NotReady,
    AlreadyInitialized,
    InvalidConfig,
    InvalidSeat,
    InvalidNumber,
    InvalidBetKind,
    InvalidGeometry,
    InvalidAmount,
    InvalidBound,
    IllegalTransition,
    InsufficientCredits,
    LimitExceeded,
    BetCapacityReached,
    ArithmeticOverflow,
    CounterExhausted,
    WheelError,
    NoJournal,
    EventNotFound,
    StorageFull,
    StorageIoError,
    StorageCorrupt,
    CommitOutcomeUnknown,
    AccountingMismatch,
    NoBets // No active placement to cancel, or no completed pattern to repeat.
};

enum class RoundState : std::uint8_t {
    Idle, BetsOpen, BetsClosing, BetsClosed, Spinning, Result, Payout, Fault
};

enum class FaultCode : std::uint8_t {
    None,
    NoSpin,
    BallTimeout,
    InvalidSensor,
    StaleWheelReport,
    WheelStartFailed,
    PowerLoss,
    OperatorInjected
};

// Checked helpers leave out unchanged on error. No wrapping or saturation.
Error checked_add(Credits a, Credits b, Credits& out) noexcept;
Error checked_subtract(Credits a, Credits b, Credits& out) noexcept;
Error checked_multiply(Credits a, Credits b, Credits& out) noexcept;

} // namespace mini_etg
