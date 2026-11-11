#pragma once

#include "mini_etg/storage.hpp"
#include "mini_etg/wheel.hpp"

namespace mini_etg {

enum class RuntimeStatus : std::uint8_t { Uninitialized, Ready, RecoveryRequired };
enum class RecoveryAction : std::uint8_t { None, Refunded, PayoutCompleted, AlreadyPaid };

struct RecoveryReport {
    RecoveryAction action{RecoveryAction::None};
    RoundId round_id{0};
    RoundState restored_state{RoundState::Idle};
};

// One caller/task owns the engine. Never call it from an ISR or concurrently.
class Engine {
public:
    // References are borrowed; wheel and storage must outlive this Engine.
    Engine(IWheel& wheel, IStorage& storage) noexcept;
    Engine(IWheel& wheel, IStorage& storage, IRandom& random) noexcept;
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Only succeeds against empty storage. Initial funding is metered and logged.
    Error initialize(const EngineConfig& config,
                     const std::array<Credits, kSeatCount>& initial_credits) noexcept;

    // Stops/clears wheel first. Resets volatile timing and resolves durable ledger.
    // NoJournal allows initialize(); existing configuration comes from the journal.
    Error recover(RecoveryReport& out) noexcept;

    RuntimeStatus status() const noexcept;
    const Snapshot& snapshot() const noexcept; // Valid after initialize/recover succeeds.

    Error credit_in(SeatId seat, Credits amount) noexcept;  // Idle only.
    Error credit_out(SeatId seat, Credits amount) noexcept; // Idle only.

    Error open_bets() noexcept;       // Idle -> BetsOpen.
    Error begin_closing() noexcept;   // BetsOpen -> BetsClosing; reject new bets.
    // BetsClosing -> BetsClosed; enabled Lucky Numbers are drawn in this commit.
    Error close_bets() noexcept;
    // BetsOpen only. Check canonical repeated exposure and settlement headroom.
    // accepted_id is unchanged on failure; the caller must not blindly retry an
    // uncertain commit (recover first and inspect the journal).
    Error place_bet(const BetRequest& request, BetId& accepted_id) noexcept;
    Error start_spin(TickMs now_ms) noexcept; // BetsClosed -> Spinning, then wheel.start.
    // Spinning only. Process the bounded IWheel batch by observation time before
    // timing out missing events at now_ms. At a deadline, an observation is late.
    // All elapsed intervals must be below 2^31 ms, including gaps between ticks.
    Error tick(TickMs now_ms) noexcept;
    Error settle() noexcept;          // Result -> Payout; pay all seats atomically.
    Error finish_round() noexcept;    // Payout -> Idle; retain last round for display.

    // Idle or a pre-result active state -> Fault, with atomic refund if outstanding.
    // Reject after durable Result: settlement is then compulsory, never a refund.
    Error raise_fault(FaultCode fault) noexcept;
    Error acknowledge_fault() noexcept; // Fault -> Idle, only with zero escrow.

private:
    // Build each change in the preallocated buffer before publishing it.
    void begin_commit() noexcept;
    Error publish() noexcept;
    Error reject(Command command, Error reason) noexcept;
    Error transition(RoundState next, Command command) noexcept;
    Error settle_recorded_result(Command command) noexcept;
    Error refund_pending_round(FaultCode fault, Command command) noexcept;

    IWheel& wheel_;
    IStorage& storage_;
    IRandom* random_{nullptr}; // Borrowed, required to open enabled Lucky rounds.
    Snapshot current_{};
    JournalCommit pending_{}; // Preallocated staging space, not a large tick() stack local.
    RuntimeStatus status_{RuntimeStatus::Uninitialized};
    TickMs spin_started_ms_{0};
    TickMs last_observed_elapsed_ms_{0}; // Enforce ordering across poll batches.
    bool motion_seen_{false};
};

} // namespace mini_etg
