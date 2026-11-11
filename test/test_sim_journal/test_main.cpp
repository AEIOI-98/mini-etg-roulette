#include <unity.h>

#include "mini_etg/engine.hpp"
#include "mini_etg/sim/journal.hpp"
#include "fake_wheel.hpp"

#include <array>
#include <limits>

using namespace mini_etg;
using mini_etg::sim::MemoryJournal;
using mini_etg::test::FakeWheel;

namespace {

std::array<Event, 128> buffer{};

void expect(Error expected, Error actual) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(actual));
}

std::array<Credits, kSeatCount> funding() {
    std::array<Credits, kSeatCount> credits{};
    credits[0] = 100;
    return credits;
}

JournalCommit first_commit() {
    JournalCommit transaction{};
    transaction.next.revision = 1;
    transaction.next.last_sequence = 1;
    transaction.next.seats[0].credits = 100;
    transaction.next.seats[0].meters.credits_in = 100;
    transaction.events.count = 1;
    Event& event = transaction.events.entries[0];
    event.sequence = 1;
    event.revision = 1;
    event.kind = EventKind::Initialized;
    return transaction;
}

JournalCommit rejection_after(const Snapshot& previous, std::size_t count = 1) {
    JournalCommit transaction{};
    transaction.next = previous;
    ++transaction.next.revision;
    transaction.next.last_sequence += count;
    transaction.events.count = count;
    for (std::size_t i = 0; i < count; ++i) {
        Event& event = transaction.events.entries[i];
        event.sequence = previous.last_sequence + i + 1;
        event.revision = transaction.next.revision;
        event.round_id = previous.round.id;
        event.kind = EventKind::CommandRejected;
        event.command = Command::CloseBets;
        event.error = Error::IllegalTransition;
    }
    return transaction;
}

void open_with_bet(Engine& engine) {
    expect(Error::Ok, engine.open_bets());
    BetRequest request{};
    request.seat = 0;
    request.stake = 10;
    const Number number{32};
    expect(Error::Ok, make_inside_bet(BetKind::Straight, &number, 1, request.selection));
    BetId id{};
    expect(Error::Ok, engine.place_bet(request, id));
    TEST_ASSERT_EQUAL_UINT64(1, id);
}

void reach(Engine& engine, FakeWheel& wheel, RoundState state) {
    expect(Error::Ok, engine.initialize({}, funding()));
    if (state == RoundState::Idle) return;
    open_with_bet(engine);
    if (state == RoundState::BetsOpen) return;
    expect(Error::Ok, engine.begin_closing());
    if (state == RoundState::BetsClosing) return;
    expect(Error::Ok, engine.close_bets());
    if (state == RoundState::BetsClosed) return;
    expect(Error::Ok, engine.start_spin(0));
    if (state == RoundState::Spinning) return;
    if (state == RoundState::Fault) {
        expect(Error::Ok, engine.raise_fault(FaultCode::OperatorInjected));
        return;
    }
    wheel.motion(10);
    wheel.ball(20, 1); // Pocket 1 contains the selected number 32.
    expect(Error::Ok, engine.tick(20));
    if (state == RoundState::Result) return;
    expect(Error::Ok, engine.settle());
}

void test_invalid_buffer_rejects_every_operation_without_writing_outputs() {
    MemoryJournal null_buffer(nullptr, 128);
    MemoryJournal zero_capacity(buffer.data(), 0);
    for (MemoryJournal* journal : {&null_buffer, &zero_capacity}) {
        expect(Error::InvalidConfig, journal->status());
        TEST_ASSERT_EQUAL_UINT64(0, journal->event_count());
        Snapshot snapshot{};
        snapshot.revision = 777;
        Event event{};
        event.sequence = 888;
        expect(Error::InvalidConfig, journal->load_latest(snapshot));
        expect(Error::InvalidConfig, journal->read_event(1, event));
        expect(Error::InvalidConfig, journal->commit(first_commit()));
        TEST_ASSERT_EQUAL_UINT64(777, snapshot.revision);
        TEST_ASSERT_EQUAL_UINT64(888, event.sequence);
    }
}

void test_empty_journal_and_out_of_range_reads_preserve_outputs() {
    MemoryJournal journal(buffer.data(), buffer.size());
    expect(Error::Ok, journal.status());
    TEST_ASSERT_EQUAL_UINT64(buffer.size(), journal.capacity());
    Snapshot snapshot{};
    snapshot.revision = 777;
    Event event{};
    event.sequence = 888;
    expect(Error::NoJournal, journal.load_latest(snapshot));
    expect(Error::EventNotFound, journal.read_event(0, event));
    expect(Error::EventNotFound, journal.read_event(1, event));
    expect(Error::EventNotFound, journal.read_event(std::numeric_limits<Sequence>::max(), event));
    TEST_ASSERT_EQUAL_UINT64(777, snapshot.revision);
    TEST_ASSERT_EQUAL_UINT64(888, event.sequence);
}

void test_commits_copy_snapshot_and_preserve_event_prefix() {
    MemoryJournal journal(buffer.data(), buffer.size());
    JournalCommit first = first_commit();
    expect(Error::Ok, journal.commit(first));
    JournalCommit second = rejection_after(first.next, 2);
    expect(Error::Ok, journal.commit(second));
    first.next.seats[0].credits = 0;
    first.events.entries[0].kind = EventKind::BetRefunded;
    second.events.entries[0].sequence = 777;
    Snapshot snapshot{};
    expect(Error::Ok, journal.load_latest(snapshot));
    TEST_ASSERT_EQUAL_INT64(100, snapshot.seats[0].credits);
    TEST_ASSERT_EQUAL_UINT64(2, snapshot.revision);
    TEST_ASSERT_EQUAL_UINT64(3, snapshot.last_sequence);
    TEST_ASSERT_EQUAL_UINT64(3, journal.event_count());
    Event event{};
    for (Sequence sequence = 1; sequence <= 3; ++sequence) {
        expect(Error::Ok, journal.read_event(sequence, event));
        TEST_ASSERT_EQUAL_UINT64(sequence, event.sequence);
        TEST_ASSERT_EQUAL_UINT64(sequence == 1 ? 1 : 2, event.revision);
    }
    expect(Error::Ok, journal.read_event(1, event));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EventKind::Initialized), static_cast<int>(event.kind));
    event.sequence = 999;
    expect(Error::EventNotFound, journal.read_event(4, event));
    TEST_ASSERT_EQUAL_UINT64(999, event.sequence);
}

void test_invalid_batch_metadata_never_partially_publishes() {
    MemoryJournal journal(buffer.data(), buffer.size());
    const JournalCommit first = first_commit();
    expect(Error::Ok, journal.commit(first));
    buffer[1].sequence = 777; // An unused slot must not be touched on failure.
    const JournalCommit valid = rejection_after(first.next, 2);
    for (unsigned variant = 0; variant < 13; ++variant) {
        JournalCommit bad = valid;
        Event& last = bad.events.entries[1];
        switch (variant) {
        case 0: ++bad.next.revision; break;
        case 1: ++bad.next.last_sequence; break;
        case 2: ++last.sequence; break;
        case 3: ++last.revision; break;
        case 4: ++last.round_id; break;
        case 5: last.kind = static_cast<EventKind>(255); break;
        case 6: last.command = static_cast<Command>(255); break;
        case 7: last.from = static_cast<RoundState>(255); break;
        case 8: last.to = static_cast<RoundState>(255); break;
        case 9: last.error = static_cast<Error>(255); break;
        case 10: last.fault = static_cast<FaultCode>(255); break;
        case 11: last.seat = kSeatCount; break;
        case 12: --bad.next.seats[0].credits; break;
        }
        expect(Error::StorageCorrupt, journal.commit(bad));
        TEST_ASSERT_EQUAL_UINT64(1, journal.event_count());
        TEST_ASSERT_EQUAL_UINT64(777, buffer[1].sequence);
        Snapshot snapshot{};
        expect(Error::Ok, journal.load_latest(snapshot));
        TEST_ASSERT_EQUAL_UINT64(1, snapshot.revision);
        TEST_ASSERT_EQUAL_INT64(100, snapshot.seats[0].credits);
    }
    expect(Error::Ok, journal.commit(valid));
    TEST_ASSERT_EQUAL_UINT64(3, journal.event_count());
}

void test_empty_oversized_and_replayed_batches_are_rejected() {
    MemoryJournal journal(buffer.data(), buffer.size());
    const JournalCommit first = first_commit();
    JournalCommit bad = first;
    bad.events.count = 0;
    expect(Error::StorageCorrupt, journal.commit(bad));
    bad = first;
    bad.events.count = kMaxEventsPerCommit + 1;
    expect(Error::StorageCorrupt, journal.commit(bad));
    TEST_ASSERT_EQUAL_UINT64(0, journal.event_count());
    expect(Error::Ok, journal.commit(first));
    expect(Error::StorageCorrupt, journal.commit(first));
    TEST_ASSERT_EQUAL_UINT64(1, journal.event_count());
}

void test_exact_idle_capacity_never_overwrites_history() {
    MemoryJournal journal(buffer.data(), 2);
    const JournalCommit first = first_commit();
    expect(Error::Ok, journal.commit(first));
    const JournalCommit second = rejection_after(first.next);
    expect(Error::Ok, journal.commit(second));
    const JournalCommit third = rejection_after(second.next);
    expect(Error::StorageFull, journal.commit(third));
    TEST_ASSERT_EQUAL_UINT64(2, journal.event_count());
    Snapshot latest{};
    expect(Error::Ok, journal.load_latest(latest));
    TEST_ASSERT_EQUAL_UINT64(2, latest.revision);
    Event event{};
    expect(Error::Ok, journal.read_event(1, event));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EventKind::Initialized), static_cast<int>(event.kind));
    expect(Error::Ok, journal.read_event(2, event));
    TEST_ASSERT_EQUAL_UINT64(2, event.sequence);
}

void test_full_rejection_log_keeps_reserved_refund_and_idle_capacity() {
    MemoryJournal journal(buffer.data(), 13);
    FakeWheel wheel(&journal);
    {
        Engine engine(wheel, journal);
        expect(Error::Ok, engine.initialize({}, funding()));
        open_with_bet(engine);
        TEST_ASSERT_EQUAL_UINT64(4, journal.event_count());
        TEST_ASSERT_EQUAL_INT64(90, engine.snapshot().seats[0].credits);
        // Four events plus the nine-event terminal reserve fill the budget.
        expect(Error::StorageFull, engine.credit_in(0, 1));
        TEST_ASSERT_EQUAL_UINT64(4, journal.event_count());
        TEST_ASSERT_EQUAL_INT(static_cast<int>(RuntimeStatus::RecoveryRequired), static_cast<int>(engine.status()));
    }
    Engine restarted(wheel, journal);
    RecoveryReport recovery{};
    expect(Error::Ok, restarted.recover(recovery));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(RecoveryAction::Refunded), static_cast<int>(recovery.action));
    TEST_ASSERT_EQUAL_INT64(100, restarted.snapshot().seats[0].credits);
    expect(Error::Ok, restarted.acknowledge_fault());
    expect(Error::Ok, check_accounting(restarted.snapshot()));
    TEST_ASSERT_EQUAL_UINT64(8, journal.event_count());
}

void test_recreated_engine_recovers_every_round_state_exactly_once() {
    constexpr std::array<RoundState, 8> states{{
        RoundState::Idle, RoundState::BetsOpen, RoundState::BetsClosing,
        RoundState::BetsClosed, RoundState::Spinning, RoundState::Result,
        RoundState::Payout, RoundState::Fault
    }};
    for (RoundState state : states) {
        MemoryJournal journal(buffer.data(), buffer.size());
        FakeWheel wheel(&journal);
        {
            Engine original(wheel, journal);
            reach(original, wheel, state);
        }
        Engine restarted(wheel, journal);
        RecoveryReport recovery{};
        expect(Error::Ok, restarted.recover(recovery));
        const bool paid = state == RoundState::Result || state == RoundState::Payout;
        const bool refunded = state == RoundState::BetsOpen || state == RoundState::BetsClosing ||
                              state == RoundState::BetsClosed || state == RoundState::Spinning;
        TEST_ASSERT_EQUAL_INT64(paid ? 450 : 100, restarted.snapshot().seats[0].credits);
        TEST_ASSERT_EQUAL_INT64(paid ? -350 : 0, restarted.snapshot().house_result);
        TEST_ASSERT_EQUAL_UINT64(paid ? 1 : 0, restarted.snapshot().seats[0].meters.games_played);
        const RecoveryAction expected = refunded ? RecoveryAction::Refunded :
            state == RoundState::Result ? RecoveryAction::PayoutCompleted :
            state == RoundState::Payout ? RecoveryAction::AlreadyPaid : RecoveryAction::None;
        TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(recovery.action));
        const Sequence resolved_sequence = restarted.snapshot().last_sequence;
        expect(Error::Ok, restarted.recover(recovery));
        TEST_ASSERT_EQUAL_UINT64(resolved_sequence, restarted.snapshot().last_sequence);
        TEST_ASSERT_EQUAL_INT64(paid ? 450 : 100, restarted.snapshot().seats[0].credits);
        expect(Error::Ok, check_accounting(restarted.snapshot()));
    }
}

} // namespace

void setUp() { buffer = {}; }
void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_invalid_buffer_rejects_every_operation_without_writing_outputs);
    RUN_TEST(test_empty_journal_and_out_of_range_reads_preserve_outputs);
    RUN_TEST(test_commits_copy_snapshot_and_preserve_event_prefix);
    RUN_TEST(test_invalid_batch_metadata_never_partially_publishes);
    RUN_TEST(test_empty_oversized_and_replayed_batches_are_rejected);
    RUN_TEST(test_exact_idle_capacity_never_overwrites_history);
    RUN_TEST(test_full_rejection_log_keeps_reserved_refund_and_idle_capacity);
    RUN_TEST(test_recreated_engine_recovers_every_round_state_exactly_once);
    return UNITY_END();
}
