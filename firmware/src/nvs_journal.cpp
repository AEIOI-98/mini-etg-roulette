#include "nvs_journal.hpp"
#include "mini_etg/journal_wire.hpp"
#include <algorithm>
#include <cstdio>
#include <limits>

namespace mini_etg::firmware {
namespace {
namespace wire = journal_wire;
constexpr std::uint32_t magic_v1 = 0x314a564e; // "NVJ1"

struct Writer {
    std::uint8_t* data;
    std::size_t remaining;
    bool ok{true};

    template<class T> void scalar(T value) {
        if (remaining < sizeof(T)) { ok = false; return; }
        std::uint64_t bits = static_cast<std::uint64_t>(value);
        for (std::size_t i = 0; i < sizeof(T); ++i) {
            *data++ = static_cast<std::uint8_t>(bits);
            bits >>= 8;
        }
        remaining -= sizeof(T);
    }
};

void key_for(std::size_t index, char (&key)[16]) {
    std::snprintf(key, sizeof(key), "block%03u", static_cast<unsigned>(index));
}

void write_checksum(std::uint8_t* data, std::size_t length) {
    Writer tail{data + length, sizeof(std::uint32_t)};
    tail.scalar(wire::checksum(data, length));
}

bool checksum_matches(const std::uint8_t* data, std::size_t length) {
    if (length < sizeof(std::uint32_t)) return false;
    const auto payload = length - sizeof(std::uint32_t);
    wire::Reader tail{data + payload, sizeof(std::uint32_t)};
    std::uint32_t saved{};
    tail.scalar(saved);
    return saved == wire::checksum(data, payload);
}
} // namespace

NvsJournal::NvsJournal(BlobStore& store, std::size_t capacity) noexcept
    : store_(store), capacity_(capacity) {}

Error NvsJournal::read_block(std::size_t index) noexcept {
    char key[16];
    key_for(index, key);
    std::size_t length{};
    const Error error = store_.get(key, block_.data(), block_.size(), length);
    if (error == Error::NoJournal) return Error::StorageCorrupt; // head promised this block
    if (error != Error::Ok) return error;
    return length == block_.size() && checksum_matches(block_.data(), length)
        ? Error::Ok : Error::StorageCorrupt;
}

Error NvsJournal::load_latest(Snapshot& out) noexcept {
    state_ = Error::NotReady;
    if (capacity_ == 0 || capacity_ > max_events) return Error::InvalidConfig;
    std::size_t length{};
    const Error loaded = store_.get("head", head_.data(), head_.size(), length);
    if (loaded == Error::NoJournal) {
        // Initial funding has at most nine events and fits entirely in 'head'.
        // An existing archive with a missing head is damage, never a new table.
        const Error archive = store_.get("block000", block_.data(), block_.size(), length);
        if (archive != Error::NoJournal)
            return archive == Error::Ok ? Error::StorageCorrupt : archive;
        last_ = revision_ = 0;
        state_ = Error::NoJournal;
        return state_;
    }
    if (loaded != Error::Ok) return loaded;
    if (!checksum_matches(head_.data(), length)) return Error::StorageCorrupt;
    wire::Reader input{head_.data(), length - sizeof(std::uint32_t)};
    std::uint32_t magic{};
    input.scalar(magic);
    if (magic != magic_v1) return Error::StorageCorrupt;
    // Both compact arrays omit inactive slots. Reset them in place on each read.
    for (auto& b : scratch_.round.bets) b = {};
    for (auto& p : scratch_.last_bets) for (auto& b : p.bets) b = {};
    wire::snapshot<true>(input, scratch_);
    if (!input.ok || validate_snapshot(scratch_) != Error::Ok ||
        scratch_.last_sequence > capacity_) return Error::StorageCorrupt;
    ResolutionReserve reserve{};
    if (required_resolution_reserve(scratch_, reserve) != Error::Ok ||
        reserve.event_sequences > capacity_ - scratch_.last_sequence ||
        reserve.revisions > std::numeric_limits<Revision>::max() - scratch_.revision ||
        input.remaining != (scratch_.last_sequence % per_block) * event_bytes)
        return Error::StorageCorrupt;

    std::copy_n(input.data, input.remaining, tail_.data());
    Revision previous = 0;
    const auto archived = scratch_.last_sequence / per_block * per_block;
    for (Sequence seq = 1; seq <= scratch_.last_sequence; ++seq) {
        const auto slot = static_cast<std::size_t>((seq - 1) % per_block);
        if (slot == 0 && seq <= archived) {
            const Error error = read_block(static_cast<std::size_t>((seq - 1) / per_block));
            if (error != Error::Ok) return error;
        }
        const auto* bytes = seq <= archived ? block_.data() : tail_.data();
        wire::Reader record{bytes + slot * event_bytes, event_bytes};
        Event e{};
        wire::event(record, e);
        if (!record.ok || record.remaining != 0 || !valid_event_metadata(e) ||
            !valid_event_payload(e, scratch_) || e.sequence != seq || e.revision == 0 ||
            e.revision < previous || e.revision - previous > 1 || e.round_id > scratch_.round.id ||
            e.selection.count > e.selection.numbers.size() || e.lucky.count > kMaxLuckyNumbers)
            return Error::StorageCorrupt;
        previous = e.revision;
    }
    if (previous != scratch_.revision) return Error::StorageCorrupt;
    revision_ = scratch_.revision;
    last_ = scratch_.last_sequence;
    out = scratch_;
    state_ = Error::Ok;
    return state_;
}

Error NvsJournal::read_event(Sequence sequence, Event& out) noexcept {
    if (state_ != Error::Ok) return state_;
    if (sequence == 0 || sequence > last_) return Error::EventNotFound;
    const auto index = static_cast<std::size_t>((sequence - 1) / per_block);
    const auto slot = static_cast<std::size_t>((sequence - 1) % per_block);
    const auto* bytes = tail_.data();
    if (index < last_ / per_block) {
        const Error error = read_block(index);
        if (error != Error::Ok) return error;
        bytes = block_.data();
    }
    wire::Reader reader{bytes + slot * event_bytes, event_bytes};
    Event e{};
    wire::event(reader, e);
    if (!reader.ok || reader.remaining || e.sequence != sequence ||
        !valid_event_metadata(e)) return Error::StorageCorrupt;
    out = e;
    return Error::Ok;
}

Error NvsJournal::commit(const JournalCommit& tx) noexcept {
    if (state_ != Error::Ok && state_ != Error::NoJournal) return state_;
    const Error valid = validate_journal_append(revision_, last_, capacity_, tx);
    if (valid != Error::Ok) return valid;
    for (std::size_t i = 0; i < tx.events.count; ++i) {
        if (!valid_event_payload(tx.events.entries[i], tx.next)) return Error::StorageCorrupt;
    }

    scratch_ = tx.next;
    Writer head{head_.data(), head_.size() - sizeof(std::uint32_t)};
    head.scalar(magic_v1);
    wire::snapshot<true>(head, scratch_);
    const std::size_t new_tail_bytes = (tx.next.last_sequence % per_block) * event_bytes;
    if (!head.ok || head.remaining < new_tail_bytes) return Error::StorageCorrupt;

    std::size_t used = (last_ % per_block) * event_bytes;
    std::copy_n(tail_.data(), used, block_.data());
    std::size_t index = last_ / per_block;
    for (std::size_t i = 0; i < tx.events.count; ++i) {
        Writer record{block_.data() + used, event_bytes};
        Event e = tx.events.entries[i];
        wire::event(record, e);
        if (!record.ok || record.remaining != 0) return Error::StorageCorrupt;
        used += event_bytes;
        if (used == block_bytes) {
            write_checksum(block_.data(), used);
            char key[16];
            key_for(index++, key);
            const Error error = store_.put(key, block_.data(), block_.size());
            // Even if the archive key reached flash, its index is outside the
            // current head. Retrying overwrites only this uncommitted suffix.
            if (error != Error::Ok)
                return error == Error::CommitOutcomeUnknown ? Error::StorageIoError : error;
            used = 0;
        }
    }
    std::copy_n(block_.data(), used, head.data);
    const std::size_t length = static_cast<std::size_t>(head.data - head_.data()) + used;
    write_checksum(head_.data(), length);
    const Error error = store_.put("head", head_.data(), length + sizeof(std::uint32_t));
    if (error != Error::Ok) {
        if (error == Error::CommitOutcomeUnknown) state_ = error;
        return error;
    }
    std::copy_n(block_.data(), used, tail_.data());
    revision_ = tx.next.revision;
    last_ = tx.next.last_sequence;
    state_ = Error::Ok;
    return state_;
}
} // namespace mini_etg::firmware
