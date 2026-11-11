#pragma once

#include "mini_etg/storage.hpp"

#include <cstddef>

namespace mini_etg::sim {

// Shared by the RAM and browser journals before publishing an append.
bool valid_event_metadata(const Event& event) noexcept;
Error validate_journal_append(Revision previous_revision, Sequence previous_sequence,
                              std::size_t capacity, const JournalCommit& transaction) noexcept;

// A session-local journal using memory supplied by the caller. The buffer must
// remain alive and exclusively owned by this journal until the journal is gone.
// It survives Engine recreation, but not process exit or physical power loss.
class MemoryJournal final : public IStorage {
public:
    MemoryJournal(Event* buffer, std::size_t capacity) noexcept;
    MemoryJournal(const MemoryJournal&) = delete;
    MemoryJournal& operator=(const MemoryJournal&) = delete;

    Error status() const noexcept;
    std::size_t event_count() const noexcept;
    std::size_t capacity() const noexcept;

    Error load_latest(Snapshot& out) noexcept override;
    Error read_event(Sequence sequence, Event& out) noexcept override;
    Error commit(const JournalCommit& transaction) noexcept override;

private:
    Event* events_;
    std::size_t capacity_;
    std::size_t count_{0};
    Snapshot latest_{};
    Error status_{Error::Ok};
    bool present_{false};
};

} // namespace mini_etg::sim
