#include "mini_etg/storage.hpp"

#include "mini_etg/wheel.hpp"
#include <limits>

namespace mini_etg {

Error required_resolution_reserve(const Snapshot& next,
                                  ResolutionReserve& out) noexcept {
    if (next.round.bet_count > kMaxBets) {
        return Error::StorageCorrupt;
    }
    const Sequence bets = static_cast<Sequence>(next.round.bet_count);
    ResolutionReserve reserve{};
    switch (next.round.state) {
    case RoundState::Idle:
        break;
    case RoundState::BetsOpen:
        reserve = {6, bets + 8 + (next.config.lucky.enabled ? 1 : 0)};
        break;
    case RoundState::BetsClosing:
        reserve = {5, bets + 7 + (next.config.lucky.enabled ? 1 : 0)};
        break;
    case RoundState::BetsClosed:
        reserve = {4, bets + 6};
        break;
    case RoundState::Spinning:
        reserve = {3, bets + 5};
        break;
    case RoundState::Result:
        reserve = {2, bets + 3};
        break;
    case RoundState::Payout:
    case RoundState::Fault:
        reserve = {1, 1};
        break;
    default:
        return Error::StorageCorrupt;
    }
    out = reserve;
    return Error::Ok;
}

bool valid_event_metadata(const Event& event) noexcept {
    return static_cast<unsigned>(event.kind) <= static_cast<unsigned>(EventKind::BetCancelled)
        && static_cast<unsigned>(event.command) <= static_cast<unsigned>(Command::RepeatLastBets)
        && static_cast<unsigned>(event.from) <= static_cast<unsigned>(RoundState::Fault)
        && static_cast<unsigned>(event.to) <= static_cast<unsigned>(RoundState::Fault)
        && static_cast<unsigned>(event.error) <= static_cast<unsigned>(Error::NoBets)
        && static_cast<unsigned>(event.fault) <= static_cast<unsigned>(FaultCode::OperatorInjected)
        && event.seat < kSeatCount;
}

// These fields are used as array indices and monetary amounts while projecting
// history. A checksum detects a damaged write; it does not validate a schema.
bool valid_event_payload(const Event& event, const Snapshot& latest) noexcept {
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

} // namespace mini_etg
