#pragma once

#include "mini_etg/events.hpp"

namespace mini_etg {

struct JournalCommit {
    Snapshot next{};
    EventBatch events{};
};

struct ResolutionReserve {
    Revision revisions{0};
    Sequence event_sequences{0};
};

// Required headroom AFTER next is committed, through resolution and return to
// Idle, including a possible recovery event. Phase 1 values for n active bets:
// Idle 0/0; BetsOpen 6/(n+8); BetsClosing 5/(n+7); BetsClosed 4/(n+6);
// Spinning 3/(n+5); Result 2/(n+3); Payout and Fault 1/1.
// Enabled Lucky mode adds one reveal event to BetsOpen/BetsClosing reserves.
// Validate state/count first; leave out unchanged on error. Future event/state
// additions must update this budget and its path-coverage tests together.
Error required_resolution_reserve(const Snapshot& next,
                                  ResolutionReserve& out) noexcept;

class IStorage {
public:
    virtual ~IStorage() = default;

    // NoJournal only means genuinely empty; corruption must never look empty.
    // Both read methods leave out unchanged on failure.
    virtual Error load_latest(Snapshot& out) noexcept = 0;
    virtual Error read_event(Sequence sequence, Event& out) noexcept = 0;

    // ATOMIC CONTRACT: commit the next ledger AND append its event batch together.
    // Crash recovery sees either the complete previous commit or this complete one.
    // Ok means durable. An uncertain outcome returns CommitOutcomeUnknown.
    // Require next.revision == prior.revision + 1, contiguous event sequences,
    // and no edits/deletions of earlier committed events. Initial revision is 1.
    // Report StorageFull instead of wrapping the append-only event history.
    // EVERY commit, including rejected commands, preserves next's resolution
    // reserve: MAX_REVISION - next.revision >= reserve.revisions and
    // MAX_SEQUENCE - next.last_sequence >= reserve.event_sequences. Use
    // subtraction so the checks themselves cannot overflow. CounterExhausted
    // means no commit was made. Reserve equivalent bytes/records for the path.
    // Resolution steps consume protected resources only as the required reserve
    // shrinks. Rejection logging cannot spend them or recursively log failure.
    virtual Error commit(const JournalCommit& transaction) noexcept = 0;
};

} // namespace mini_etg
