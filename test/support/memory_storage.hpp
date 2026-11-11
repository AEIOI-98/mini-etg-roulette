#pragma once

#include "mini_etg/storage.hpp"

#include <array>
#include <cstddef>
#include <limits>

namespace mini_etg::test {

// Test adapter: storage outlives individual Engine instances to model a reboot.
// Snapshot space is fixed; the event limit models finite append-only storage.
class MemoryStorage final : public IStorage {
public:
    static constexpr std::size_t kEventCapacity = 4096;

    explicit MemoryStorage(std::size_t event_capacity = kEventCapacity) noexcept
        : capacity_(event_capacity < kEventCapacity ? event_capacity : kEventCapacity) {}

    void reset() noexcept {
        latest_ = {};
        count_ = 0;
        base_sequence_ = 0;
        present_ = false;
        corrupt_ = false;
        failure_ = Failure::None;
    }

    void fail_next_before_commit() noexcept { failure_ = Failure::Before; }
    void fail_next_after_commit() noexcept { failure_ = Failure::After; }
    void corrupt() noexcept { corrupt_ = true; }
    std::size_t event_count() const noexcept { return count_; }

    // Boundary fixtures can start near uint64 limits without billions of writes.
    // The synthetic prefix is not retained; read_event reports EventNotFound for it.
    void restore_for_test(const Snapshot& snapshot) noexcept {
        reset();
        latest_ = snapshot;
        base_sequence_ = snapshot.last_sequence;
        present_ = true;
    }

    Error load_latest(Snapshot& out) noexcept override {
        if (corrupt_) {
            return Error::StorageCorrupt;
        }
        if (!present_) {
            return Error::NoJournal;
        }
        if (validate_snapshot(latest_) != Error::Ok) {
            return Error::StorageCorrupt;
        }
        out = latest_;
        return Error::Ok;
    }

    Error read_event(Sequence sequence, Event& out) noexcept override {
        if (corrupt_) {
            return Error::StorageCorrupt;
        }
        if (sequence <= base_sequence_) {
            return Error::EventNotFound;
        }
        const Sequence offset = sequence - base_sequence_ - 1;
        if (offset >= count_) {
            return Error::EventNotFound;
        }
        out = events_[static_cast<std::size_t>(offset)];
        return Error::Ok;
    }

    Error commit(const JournalCommit& transaction) noexcept override {
        if (corrupt_) {
            return Error::StorageCorrupt;
        }
        const Snapshot& next = transaction.next;
        const EventBatch& batch = transaction.events;
        if (validate_snapshot(next) != Error::Ok || batch.count == 0 ||
            batch.count > kMaxEventsPerCommit) {
            return Error::StorageCorrupt;
        }
        const Revision previous_revision = present_ ? latest_.revision : 0;
        const Sequence previous_sequence = present_ ? latest_.last_sequence : 0;
        constexpr auto max_revision = std::numeric_limits<Revision>::max();
        constexpr auto max_sequence = std::numeric_limits<Sequence>::max();
        if (previous_revision == max_revision ||
            batch.count > max_sequence - previous_sequence) {
            return Error::CounterExhausted;
        }
        if (next.revision != previous_revision + 1 ||
            next.last_sequence != previous_sequence + batch.count) {
            return Error::StorageCorrupt;
        }
        for (std::size_t i = 0; i < batch.count; ++i) {
            const Event& event = batch.entries[i];
            if (event.sequence != previous_sequence + i + 1 ||
                event.revision != next.revision || event.round_id != next.round.id ||
                static_cast<unsigned>(event.kind) > static_cast<unsigned>(EventKind::BetCancelled) ||
                static_cast<unsigned>(event.command) > static_cast<unsigned>(Command::RepeatLastBets) ||
                static_cast<unsigned>(event.from) > static_cast<unsigned>(RoundState::Fault) ||
                static_cast<unsigned>(event.to) > static_cast<unsigned>(RoundState::Fault)) {
                return Error::StorageCorrupt;
            }
        }
        ResolutionReserve reserve{};
        const Error reserve_error = required_resolution_reserve(next, reserve);
        if (reserve_error != Error::Ok) {
            return reserve_error;
        }
        if (max_revision - next.revision < reserve.revisions ||
            max_sequence - next.last_sequence < reserve.event_sequences) {
            return Error::CounterExhausted;
        }
        if (batch.count > capacity_ - count_ ||
            reserve.event_sequences > capacity_ - count_ - batch.count) {
            return Error::StorageFull;
        }
        const Failure failure = failure_;
        failure_ = Failure::None;
        if (failure == Failure::Before) {
            return Error::StorageIoError;
        }

        // The failpoints model the two sides of the atomic storage boundary.
        // No callback or partial-failure point exists inside this publication.
        for (std::size_t i = 0; i < batch.count; ++i) {
            events_[count_ + i] = batch.entries[i];
        }
        count_ += batch.count;
        latest_ = next;
        present_ = true;
        return failure == Failure::After ? Error::CommitOutcomeUnknown : Error::Ok;
    }

private:
    enum class Failure { None, Before, After };

    std::array<Event, kEventCapacity> events_{};
    Snapshot latest_{};
    std::size_t capacity_;
    std::size_t count_{0};
    Sequence base_sequence_{0};
    bool present_{false};
    bool corrupt_{false};
    Failure failure_{Failure::None};
};

} // namespace mini_etg::test
