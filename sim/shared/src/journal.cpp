#include "mini_etg/sim/journal.hpp"

#include <limits>

namespace mini_etg::sim {

bool valid_event_metadata(const Event& event) noexcept {
    return static_cast<unsigned>(event.kind) <= static_cast<unsigned>(EventKind::BetCancelled)
        && static_cast<unsigned>(event.command) <= static_cast<unsigned>(Command::RepeatLastBets)
        && static_cast<unsigned>(event.from) <= static_cast<unsigned>(RoundState::Fault)
        && static_cast<unsigned>(event.to) <= static_cast<unsigned>(RoundState::Fault)
        && static_cast<unsigned>(event.error) <= static_cast<unsigned>(Error::NoBets)
        && static_cast<unsigned>(event.fault) <= static_cast<unsigned>(FaultCode::OperatorInjected)
        && event.seat < kSeatCount;
}


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

Error validate_journal_append(Revision previous_revision, Sequence previous_sequence,
                              std::size_t capacity, const JournalCommit& transaction) noexcept {
    const Snapshot& next = transaction.next;
    const EventBatch& batch = transaction.events;
    if (validate_snapshot(next) != Error::Ok || batch.count == 0 ||
        batch.count > kMaxEventsPerCommit) return Error::StorageCorrupt;

    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    if (previous_revision == maximum || batch.count > maximum - previous_sequence) {
        return Error::CounterExhausted;
    }
    if (next.revision != previous_revision + 1 ||
        next.last_sequence != previous_sequence + batch.count) return Error::StorageCorrupt;

    for (std::size_t i = 0; i < batch.count; ++i) {
        const Event& event = batch.entries[i];
        if (event.sequence != previous_sequence + i + 1 ||
            event.revision != next.revision || event.round_id != next.round.id ||
            !valid_event_metadata(event)) return Error::StorageCorrupt;
    }
    ResolutionReserve reserve{};
    const Error reserve_error = required_resolution_reserve(next, reserve);
    if (reserve_error != Error::Ok) return reserve_error;
    if (maximum - next.revision < reserve.revisions ||
        maximum - next.last_sequence < reserve.event_sequences) return Error::CounterExhausted;
    if (previous_sequence > capacity || batch.count > capacity - previous_sequence ||
        reserve.event_sequences > capacity - previous_sequence - batch.count) return Error::StorageFull;
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
