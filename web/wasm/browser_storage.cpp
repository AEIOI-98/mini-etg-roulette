#include "browser_storage.hpp"
#include "mini_etg/sim/journal.hpp"
#include "mini_etg/wheel.hpp"
#include <emscripten.h>
#include <algorithm>

namespace mini_etg::web {
namespace {
constexpr std::uint32_t magic = 0x31475445; // "ETG1", little endian.
constexpr std::size_t capacity = 8192;
constexpr int maximum_bytes = 1024 * 1024;

// These fields are used as array indices and monetary amounts while projecting
// history. A checksum detects a damaged write; it does not validate a schema.
bool valid_payload(const Event& event, const Snapshot& latest) {
    if (event.kind == EventKind::BetAccepted) {
        BetSelection canonical{};
        std::uint8_t odds{};
        const TableLimits& limits = latest.config.limits;
        if (canonicalize(event.selection, canonical) != Error::Ok ||
            event.amount < limits.min_per_bet || event.amount > limits.max_per_bet ||
            base_profit_odds(canonical.kind, latest.config.lucky, odds) != Error::Ok ||
            odds != event.profit_odds) return false;
        for (std::size_t i = 0; i < canonical.numbers.size(); ++i) {
            if (canonical.numbers[i].value != event.selection.numbers[i].value) return false;
        }
    }
    if (event.kind == EventKind::BetAccepted || event.kind == EventKind::BetSettled ||
        event.kind == EventKind::BetCancelled || event.kind == EventKind::BetRefunded) {
        if (event.bet_id == 0 || event.bet_id >= latest.next_bet_id || event.amount < 0) return false;
    }
    if (event.kind == EventKind::BetCancelled || event.kind == EventKind::BetRefunded ||
        event.kind == EventKind::CreditLoaded || event.kind == EventKind::CreditWithdrawn) {
        if (event.amount <= 0) return false;
    }
    if (event.kind == EventKind::ResultRecorded || event.kind == EventKind::BetSettled) {
        Number number{};
        if (number_at(event.pocket, number) != Error::Ok || number.value != event.number.value)
            return false;
    }
    return event.kind != EventKind::LuckyNumbersDrawn ||
        (latest.config.lucky.enabled && validate_lucky_draw(latest.config.lucky, event.lucky) == Error::Ok);
}

// Read and decode once while the caller holds the origin's exclusive Web Lock.
// A missing key, malformed data and a blocked storage API have different outcomes.
EM_JS(int, read_envelope, (std::uint8_t* out, int capacity_bytes), {
    try {
        const value = localStorage.getItem('mini-etg-roulette.journal.v1');
        if (value === null) return -1;
        if (!value.length || value.length > 4 * Math.ceil(capacity_bytes / 3)) return -2;
        let binary;
        try { binary = atob(value); } catch (_) { return -2; }
        if (binary.length > capacity_bytes || btoa(binary) !== value) return -2;
        for (let i = 0; i < binary.length; ++i) HEAPU8[out + i] = binary.charCodeAt(i);
        return binary.length;
    } catch (_) { return -3; }
});

EM_JS(int, replace_envelope, (const std::uint8_t* data, int length), {
    try {
        let binary = "";
        for (let i = 0; i < length; i += 16384)
            binary += String.fromCharCode(...HEAPU8.subarray(data + i, data + Math.min(i + 16384, length)));
        localStorage.setItem('mini-etg-roulette.journal.v1', btoa(binary));
        return 0;
    } catch (e) { return e && e.name === 'QuotaExceededError' ? 1 : 2; }
});
} // namespace

BrowserStorage::BrowserStorage() {
    Event empty{};
    wire::Bytes sample;
    wire::Writer writer{sample};
    wire::event(writer, empty);
    event_bytes_ = sample.size();
    state_ = restore();
}

Error BrowserStorage::restore() {
    wire::Bytes bytes(maximum_bytes);
    const int length = read_envelope(bytes.data(), maximum_bytes);
    if (length == -1) return Error::NoJournal;
    if (length == -3) return Error::StorageIoError;
    if (length < 12) return Error::StorageCorrupt;
    bytes.resize(length);
    std::uint32_t stored_checksum{};
    wire::Reader tail{bytes.data() + length - 4, 4};
    tail.scalar(stored_checksum);
    if (stored_checksum != wire::checksum(bytes, bytes.size() - 4)) return Error::StorageCorrupt;

    wire::Reader input{bytes.data(), bytes.size() - 4};
    std::uint32_t tag{};
    input.scalar(tag);
    wire::snapshot(input, latest_);
    if (tag != magic || !input.ok || validate_snapshot(latest_) != Error::Ok ||
        latest_.last_sequence > capacity) return Error::StorageCorrupt;
    ResolutionReserve reserve{};
    if (required_resolution_reserve(latest_, reserve) != Error::Ok ||
        reserve.event_sequences > capacity - latest_.last_sequence ||
        input.remaining != event_bytes_ * (latest_.last_sequence + reserve.event_sequences))
        return Error::StorageCorrupt;

    const auto* first_event = input.data;
    Revision previous = 0;
    for (Sequence sequence = 1; sequence <= latest_.last_sequence; ++sequence) {
        Event e{};
        wire::event(input, e);
        if (!input.ok || !sim::valid_event_metadata(e) || !valid_payload(e, latest_) ||
            e.sequence != sequence ||
            e.revision < previous || e.revision - previous > 1 || e.revision == 0 ||
            e.round_id > latest_.round.id || e.lucky.count > kMaxLuckyNumbers ||
            e.selection.count > e.selection.numbers.size()) return Error::StorageCorrupt;
        previous = e.revision;
    }
    if (previous != latest_.revision ||
        !std::all_of(input.data, input.data + input.remaining, [](auto b){ return b == 0; }))
        return Error::StorageCorrupt;
    history_.assign(first_event, input.data);
    return Error::Ok;
}

Error BrowserStorage::load_latest(Snapshot& out) noexcept {
    if (state_ == Error::Ok) out = latest_;
    return state_;
}

Error BrowserStorage::read_event(Sequence sequence, Event& out) noexcept {
    if (state_ != Error::Ok) return state_;
    if (!sequence || sequence > latest_.last_sequence) return Error::EventNotFound;
    wire::Reader reader{history_.data() + (sequence - 1) * event_bytes_, event_bytes_};
    wire::event(reader, out);
    return Error::Ok;
}

Error BrowserStorage::commit(const JournalCommit& transaction) noexcept {
    if (state_ != Error::Ok && state_ != Error::NoJournal) return state_;
    const Error valid = sim::validate_journal_append(latest_.revision, latest_.last_sequence,
                                                     capacity, transaction);
    if (valid != Error::Ok) return valid;
    for (std::size_t i = 0; i < transaction.events.count; ++i) {
        if (!valid_payload(transaction.events.entries[i], transaction.next)) return Error::StorageCorrupt;
    }
    Snapshot next = transaction.next;
    ResolutionReserve reserve{};
    required_resolution_reserve(next, reserve);
    wire::Bytes envelope;
    envelope.reserve(16384 + (next.last_sequence + reserve.event_sequences) * event_bytes_);
    wire::Writer writer{envelope};
    auto tag = magic;
    writer.scalar(tag);
    wire::snapshot(writer, next);
    envelope.insert(envelope.end(), history_.begin(), history_.end());
    const auto append_offset = envelope.size();
    for (std::size_t i = 0; i < transaction.events.count; ++i) {
        Event e = transaction.events.entries[i];
        wire::event(writer, e);
    }
    const auto history_end = envelope.size();
    // Paid-for padding shrinks as a closing/refund path consumes its reserved events.
    envelope.resize(history_end + reserve.event_sequences * event_bytes_, 0);
    auto hash = wire::checksum(envelope, envelope.size());
    writer.scalar(hash);
    if (envelope.size() > maximum_bytes) return Error::StorageFull;
    const int result = replace_envelope(envelope.data(), static_cast<int>(envelope.size()));
    if (result != 0) return result == 1 ? Error::StorageFull : Error::StorageIoError;
    history_.insert(history_.end(), envelope.begin() + append_offset, envelope.begin() + history_end);
    latest_ = next;
    state_ = Error::Ok;
    return Error::Ok;
}
} // namespace mini_etg::web
