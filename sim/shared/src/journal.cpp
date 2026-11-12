#include "mini_etg/sim/journal.hpp"

namespace mini_etg::sim {

MemoryJournal::MemoryJournal(Event* buffer, std::size_t capacity) noexcept
    : events_(buffer), capacity_(capacity) {
    if (buffer == nullptr || capacity == 0) status_ = Error::InvalidConfig;
}

Error MemoryJournal::status() const noexcept { return status_; }
std::size_t MemoryJournal::event_count() const noexcept { return count_; }
std::size_t MemoryJournal::capacity() const noexcept { return capacity_; }

Error MemoryJournal::load_latest(Snapshot& out) noexcept {
    if (status_ != Error::Ok) return status_;
    if (!present_) return Error::NoJournal;
    const Error error = validate_snapshot(latest_);
    if (error != Error::Ok) return error;
    out = latest_;
    return Error::Ok;
}

Error MemoryJournal::read_event(Sequence sequence, Event& out) noexcept {
    if (status_ != Error::Ok) return status_;
    if (sequence == 0 || sequence - 1 >= count_) return Error::EventNotFound;
    out = events_[static_cast<std::size_t>(sequence - 1)];
    return Error::Ok;
}

Error MemoryJournal::commit(const JournalCommit& transaction) noexcept {
    if (status_ != Error::Ok) return status_;
    const Error check = validate_journal_append(latest_.revision, count_, capacity_, transaction);
    if (check != Error::Ok) return check;
    const auto& batch = transaction.events;

    // All validation precedes publication. Copies cannot fail, call back, or
    // allocate. One caller owns this adapter, so a reader sees the whole commit.
    for (std::size_t i = 0; i < batch.count; ++i) events_[count_ + i] = batch.entries[i];
    count_ += batch.count;
    latest_ = transaction.next;
    present_ = true;
    return Error::Ok;
}

} // namespace mini_etg::sim
