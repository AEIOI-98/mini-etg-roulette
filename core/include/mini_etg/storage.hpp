#pragma once

#include "mini_etg/events.hpp"

namespace mini_etg {

struct JournalCommit {
    Snapshot next{};
    EventBatch events{};
};

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
    // Reserve space to resolve next's outstanding round; ordinary/rejection events
    // cannot consume that reserve. Refuse opening/acceptance if it cannot be kept.
    virtual Error commit(const JournalCommit& transaction) noexcept = 0;
};

} // namespace mini_etg
