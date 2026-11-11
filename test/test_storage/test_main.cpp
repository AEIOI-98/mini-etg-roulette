#include <unity.h>

#include "mini_etg/model.hpp"
#include "mini_etg/storage.hpp"
#include "memory_storage.hpp"

#include <limits>

using namespace mini_etg;
using mini_etg::test::MemoryStorage;

namespace {

MemoryStorage storage;
MemoryStorage small_storage(10);

void expect(Error expected, Error actual) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(actual));
}

Snapshot funded(Credits amount = 100) {
    Snapshot snapshot{};
    snapshot.revision = 1;
    snapshot.last_sequence = 1;
    snapshot.seats[0].credits = amount;
    snapshot.seats[0].meters.credits_in = amount;
    return snapshot;
}

Snapshot opened(const Snapshot& base) {
    Snapshot next = base;
    next.round = {};
    next.round.id = base.round.id + 1;
    next.round.state = RoundState::BetsOpen;
    next.round.resolution = Resolution::Pending;
    for (const Seat& seat : next.seats) {
        next.round.opening_total += seat.credits;
    }
    next.round.opening_total += next.house_result;
    return next;
}

Snapshot single_bet() {
    Snapshot snapshot = opened(funded());
    snapshot.round.bet_count = 1;
    snapshot.round.bets[0].id = 1;
    snapshot.round.bets[0].request.seat = 0;
    snapshot.round.bets[0].request.stake = 10;
    snapshot.round.bets[0].request.selection.kind = BetKind::Straight;
    snapshot.round.bets[0].request.selection.count = 1;
    snapshot.round.bets[0].request.selection.numbers[0] = Number{1};
    snapshot.round.bets[0].profit_odds = 35;
    snapshot.next_bet_id = 2;
    snapshot.seats[0].credits = 90;
    snapshot.round.escrow = 10;
    return snapshot;
}

JournalCommit first_commit() {
    JournalCommit commit{};
    commit.next = funded();
    commit.events.count = 1;
    commit.events.entries[0].sequence = 1;
    commit.events.entries[0].revision = 1;
    commit.events.entries[0].kind = EventKind::Initialized;
    return commit;
}

JournalCommit next_commit(const Snapshot& previous, Snapshot next,
                         EventKind kind = EventKind::StateChanged,
                         std::size_t count = 1) {
    JournalCommit commit{};
    commit.next = next;
    commit.next.revision = previous.revision + 1;
    commit.next.last_sequence = previous.last_sequence + count;
    commit.events.count = count;
    for (std::size_t i = 0; i < count; ++i) {
        Event& event = commit.events.entries[i];
        event.sequence = previous.last_sequence + i + 1;
        event.revision = commit.next.revision;
        event.round_id = commit.next.round.id;
        event.kind = kind;
        event.from = previous.round.state;
        event.to = commit.next.round.state;
    }
    return commit;
}

void test_config_rejects_invalid_limits_and_deadlines() {
    EngineConfig config{};
    expect(Error::Ok, validate_config(config));
    config.limits.min_per_bet = 0;
    expect(Error::InvalidConfig, validate_config(config));
    config = {};
    config.limits.max_per_bet = 0;
    expect(Error::InvalidConfig, validate_config(config));
    config = {};
    config.limits.max_total_per_seat = 0;
    expect(Error::InvalidConfig, validate_config(config));
    config = {};
    config.no_spin_timeout_ms = 0;
    expect(Error::InvalidConfig, validate_config(config));
    config.no_spin_timeout_ms = config.ball_timeout_ms;
    expect(Error::InvalidConfig, validate_config(config));
    config = {};
    config.ball_timeout_ms = TickMs{1} << 31;
    expect(Error::InvalidConfig, validate_config(config));
    config.ball_timeout_ms -= 1;
    expect(Error::Ok, validate_config(config));
}

void test_accounting_conserves_escrow_payout_refund_and_idle_transfers() {
    Snapshot snapshot = single_bet();
    expect(Error::Ok, check_accounting(snapshot));
    expect(Error::Ok, validate_snapshot(snapshot));

    snapshot.round.state = RoundState::Result;
    snapshot.round.has_result = true;
    snapshot.round.result_index = PocketIndex{23};
    snapshot.round.result_number = Number{1};
    expect(Error::Ok, validate_snapshot(snapshot));

    snapshot.round.state = RoundState::Payout;
    snapshot.round.resolution = Resolution::Paid;
    snapshot.round.escrow = 0;
    snapshot.seats[0].credits = 450;
    snapshot.seats[0].meters.games_played = 1;
    snapshot.seats[0].meters.games_won = 1;
    snapshot.house_result = -350;
    expect(Error::Ok, check_accounting(snapshot));
    expect(Error::Ok, validate_snapshot(snapshot));

    snapshot.round.state = RoundState::Idle;
    snapshot.seats[0].credits += 25;
    snapshot.seats[0].meters.credits_in += 25;
    expect(Error::Ok, validate_snapshot(snapshot)); // Old opening total remains 100.

    snapshot = single_bet();
    snapshot.round.state = RoundState::Fault;
    snapshot.round.resolution = Resolution::Refunded;
    snapshot.round.fault = FaultCode::PowerLoss;
    snapshot.round.escrow = 0;
    snapshot.seats[0].credits = 100;
    expect(Error::Ok, validate_snapshot(snapshot));

    snapshot = funded();
    snapshot.round.state = RoundState::Fault;
    snapshot.round.fault = FaultCode::OperatorInjected;
    expect(Error::Ok, validate_snapshot(snapshot)); // Fault from Idle has no baseline.
}

void test_accounting_detects_missing_money_bad_escrow_and_overflow() {
    Snapshot snapshot = single_bet();
    snapshot.seats[0].credits -= 1;
    expect(Error::AccountingMismatch, check_accounting(snapshot));
    snapshot = single_bet();
    snapshot.round.escrow = 9;
    expect(Error::AccountingMismatch, check_accounting(snapshot));
    snapshot = single_bet();
    snapshot.round.opening_total = 99;
    expect(Error::AccountingMismatch, check_accounting(snapshot));
    snapshot = funded();
    snapshot.seats[0].meters.games_won = 1;
    expect(Error::AccountingMismatch, check_accounting(snapshot));
    snapshot = funded();
    snapshot.seats[0].credits = -1;
    expect(Error::AccountingMismatch, check_accounting(snapshot));
    snapshot = funded(std::numeric_limits<Credits>::max());
    snapshot.seats[1].credits = 1;
    snapshot.seats[1].meters.credits_in = 1;
    expect(Error::ArithmeticOverflow, check_accounting(snapshot));
}

void test_snapshot_rejects_schema_metadata_and_state_corruption() {
    Snapshot snapshot = funded();
    snapshot.format_version = 2;
    expect(Error::StorageCorrupt, validate_snapshot(snapshot));
    snapshot = funded();
    snapshot.revision = 0;
    expect(Error::StorageCorrupt, validate_snapshot(snapshot));
    snapshot = funded();
    snapshot.last_sequence = 0;
    expect(Error::StorageCorrupt, validate_snapshot(snapshot));
    snapshot = funded();
    snapshot.next_bet_id = 0;
    expect(Error::StorageCorrupt, validate_snapshot(snapshot));
    snapshot = funded();
    snapshot.round.state = static_cast<RoundState>(255);
    expect(Error::StorageCorrupt, validate_snapshot(snapshot));
    snapshot = single_bet();
    snapshot.round.resolution = Resolution::Paid;
    expect(Error::StorageCorrupt, validate_snapshot(snapshot));
    snapshot = single_bet();
    snapshot.round.fault = FaultCode::NoSpin;
    expect(Error::StorageCorrupt, validate_snapshot(snapshot));
    snapshot = single_bet();
    snapshot.round.state = RoundState::Idle;
    expect(Error::StorageCorrupt, validate_snapshot(snapshot));
}

void test_snapshot_rejects_bet_and_result_corruption() {
    Snapshot snapshot = single_bet();
    snapshot.round.bets[0].request.seat = 8;
    expect(Error::StorageCorrupt, validate_snapshot(snapshot));
    snapshot = single_bet();
    snapshot.round.bets[0].profit_odds = 36;
    expect(Error::StorageCorrupt, validate_snapshot(snapshot));
    snapshot = single_bet();
    snapshot.round.bets[0].request.selection.numbers[17] = Number{1};
    expect(Error::StorageCorrupt, validate_snapshot(snapshot));
    snapshot = single_bet();
    snapshot.next_bet_id = 1;
    expect(Error::StorageCorrupt, validate_snapshot(snapshot));
    snapshot = single_bet();
    snapshot.round.bet_count = kMaxBets + 1;
    expect(Error::StorageCorrupt, validate_snapshot(snapshot));
    snapshot = single_bet();
    snapshot.round.state = RoundState::Result;
    snapshot.round.has_result = true;
    snapshot.round.result_index = PocketIndex{23};
    snapshot.round.result_number = Number{2};
    expect(Error::StorageCorrupt, validate_snapshot(snapshot));
    snapshot.round.result_index = PocketIndex{37};
    expect(Error::StorageCorrupt, validate_snapshot(snapshot));
}

void test_snapshot_rechecks_canonical_exposure_and_unique_ids() {
    Snapshot snapshot = single_bet();
    snapshot.round.bet_count = 2;
    snapshot.round.bets[1] = snapshot.round.bets[0];
    snapshot.seats[0].credits = 80;
    snapshot.round.escrow = 20;
    expect(Error::StorageCorrupt, validate_snapshot(snapshot)); // Duplicate ID.
    snapshot.round.bets[1].id = 2;
    snapshot.next_bet_id = 3;
    expect(Error::Ok, validate_snapshot(snapshot));
    snapshot.config.limits.max_per_bet = 15;
    expect(Error::StorageCorrupt, validate_snapshot(snapshot)); // Combined exposure 20.
    snapshot.config.limits.max_per_bet = 100;
    snapshot.config.limits.max_total_per_seat = 15;
    expect(Error::StorageCorrupt, validate_snapshot(snapshot));
}

void test_reserve_table_and_invalid_output_unchanged() {
    const RoundState states[] = {
        RoundState::Idle, RoundState::BetsOpen, RoundState::BetsClosing,
        RoundState::BetsClosed, RoundState::Spinning, RoundState::Result,
        RoundState::Payout, RoundState::Fault};
    const Revision revisions[] = {0, 6, 5, 4, 3, 2, 1, 1};
    const Sequence overhead[] = {0, 8, 7, 6, 5, 3, 1, 1};
    for (std::size_t count : {std::size_t{0}, std::size_t{1}, kMaxBets}) {
        for (std::size_t i = 0; i < 8; ++i) {
            Snapshot snapshot{};
            snapshot.round.state = states[i];
            snapshot.round.bet_count = count;
            ResolutionReserve reserve{};
            expect(Error::Ok, required_resolution_reserve(snapshot, reserve));
            TEST_ASSERT_EQUAL_UINT64(revisions[i], reserve.revisions);
            const Sequence expected_events = overhead[i] + (i >= 1 && i <= 5 ? count : 0);
            TEST_ASSERT_EQUAL_UINT64(expected_events, reserve.event_sequences);
        }
    }
    Snapshot bad{};
    ResolutionReserve sentinel{123, 456};
    bad.round.state = static_cast<RoundState>(255);
    expect(Error::StorageCorrupt, required_resolution_reserve(bad, sentinel));
    TEST_ASSERT_EQUAL_UINT64(123, sentinel.revisions);
    TEST_ASSERT_EQUAL_UINT64(456, sentinel.event_sequences);
    bad.round.state = RoundState::Idle;
    bad.round.bet_count = kMaxBets + 1;
    expect(Error::StorageCorrupt, required_resolution_reserve(bad, sentinel));
    TEST_ASSERT_EQUAL_UINT64(123, sentinel.revisions);
}

void test_empty_storage_and_missing_events_leave_outputs_unchanged() {
    Snapshot snapshot = funded();
    snapshot.revision = 777;
    expect(Error::NoJournal, storage.load_latest(snapshot));
    TEST_ASSERT_EQUAL_UINT64(777, snapshot.revision);
    Event event{};
    event.sequence = 888;
    expect(Error::EventNotFound, storage.read_event(0, event));
    expect(Error::EventNotFound, storage.read_event(1, event));
    TEST_ASSERT_EQUAL_UINT64(888, event.sequence);
}

void test_commits_append_events_without_changing_prior_records() {
    const JournalCommit first = first_commit();
    expect(Error::Ok, storage.commit(first));
    const JournalCommit second = next_commit(first.next, opened(first.next));
    expect(Error::Ok, storage.commit(second));
    TEST_ASSERT_EQUAL_UINT32(2, storage.event_count());
    Snapshot latest{};
    expect(Error::Ok, storage.load_latest(latest));
    TEST_ASSERT_EQUAL_UINT64(2, latest.revision);
    TEST_ASSERT_EQUAL_UINT64(2, latest.last_sequence);
    Event event{};
    expect(Error::Ok, storage.read_event(1, event));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EventKind::Initialized), static_cast<int>(event.kind));
    TEST_ASSERT_EQUAL_UINT64(1, event.revision);
    expect(Error::Ok, storage.read_event(2, event));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(RoundState::BetsOpen), static_cast<int>(event.to));
}

void test_incomplete_or_inconsistent_commits_do_not_publish() {
    const JournalCommit first = first_commit();
    expect(Error::Ok, storage.commit(first));
    const JournalCommit valid = next_commit(first.next, opened(first.next));
    JournalCommit bad = valid;
    bad.next.revision += 1;
    bad.next.last_sequence += 1;
    expect(Error::StorageCorrupt, storage.commit(bad));
    bad = valid;
    bad.events.entries[0].sequence += 1;
    expect(Error::StorageCorrupt, storage.commit(bad));
    bad = valid;
    bad.events.entries[0].revision += 1;
    expect(Error::StorageCorrupt, storage.commit(bad));
    bad = valid;
    bad.events.entries[0].kind = static_cast<EventKind>(255);
    expect(Error::StorageCorrupt, storage.commit(bad));
    bad = valid;
    bad.events.count = 0;
    expect(Error::StorageCorrupt, storage.commit(bad));
    bad = valid;
    bad.events.count = kMaxEventsPerCommit + 1;
    expect(Error::StorageCorrupt, storage.commit(bad));
    TEST_ASSERT_EQUAL_UINT32(1, storage.event_count());
    Snapshot latest{};
    expect(Error::Ok, storage.load_latest(latest));
    TEST_ASSERT_EQUAL_UINT64(1, latest.revision);
}

void test_crash_before_commit_preserves_previous_snapshot_and_log() {
    const JournalCommit first = first_commit();
    expect(Error::Ok, storage.commit(first));
    const JournalCommit second = next_commit(first.next, opened(first.next));
    storage.fail_next_before_commit();
    expect(Error::StorageIoError, storage.commit(second));
    TEST_ASSERT_EQUAL_UINT32(1, storage.event_count());
    Snapshot latest{};
    expect(Error::Ok, storage.load_latest(latest));
    TEST_ASSERT_EQUAL_UINT64(1, latest.revision);
    expect(Error::Ok, storage.commit(second)); // The injected failure was one-shot.
}

void test_crash_after_commit_exposes_complete_new_snapshot_and_log() {
    const JournalCommit first = first_commit();
    expect(Error::Ok, storage.commit(first));
    const JournalCommit second = next_commit(first.next, opened(first.next));
    storage.fail_next_after_commit();
    expect(Error::CommitOutcomeUnknown, storage.commit(second));
    TEST_ASSERT_EQUAL_UINT32(2, storage.event_count());
    Snapshot latest{};
    expect(Error::Ok, storage.load_latest(latest));
    TEST_ASSERT_EQUAL_UINT64(2, latest.revision);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(RoundState::BetsOpen), static_cast<int>(latest.round.state));
    Event event{};
    expect(Error::Ok, storage.read_event(2, event));
    expect(Error::StorageCorrupt, storage.commit(second)); // Blind retry cannot duplicate.
    TEST_ASSERT_EQUAL_UINT32(2, storage.event_count());
}

void test_log_full_preserves_terminal_reserve_for_recovery_and_idle() {
    const JournalCommit first = first_commit();
    expect(Error::Ok, small_storage.commit(first));
    const JournalCommit open = next_commit(first.next, opened(first.next));
    expect(Error::Ok, small_storage.commit(open)); // 8 slots remain, all reserved.
    const JournalCommit rejection = next_commit(open.next, open.next, EventKind::CommandRejected);
    expect(Error::StorageFull, small_storage.commit(rejection));
    TEST_ASSERT_EQUAL_UINT32(2, small_storage.event_count());

    Snapshot fault = open.next;
    fault.round.state = RoundState::Fault;
    fault.round.resolution = Resolution::Refunded;
    fault.round.fault = FaultCode::PowerLoss;
    JournalCommit refund = next_commit(open.next, fault, EventKind::StateChanged, 2);
    refund.events.entries[1].kind = EventKind::RecoveryApplied;
    expect(Error::Ok, small_storage.commit(refund));
    Snapshot idle = refund.next;
    idle.round.state = RoundState::Idle;
    const JournalCommit finish = next_commit(refund.next, idle);
    expect(Error::Ok, small_storage.commit(finish));
    TEST_ASSERT_EQUAL_UINT32(5, small_storage.event_count());
}

void test_counter_reserves_reject_logging_and_allow_final_resolution() {
    constexpr auto max = std::numeric_limits<std::uint64_t>::max();
    Snapshot pending = opened(funded());
    pending.round.state = RoundState::Spinning;
    pending.revision = max - 5;
    pending.last_sequence = max - 5;
    storage.restore_for_test(pending);
    const JournalCommit rejection = next_commit(pending, pending, EventKind::CommandRejected);
    expect(Error::CounterExhausted, storage.commit(rejection));
    TEST_ASSERT_EQUAL_UINT32(0, storage.event_count());

    Snapshot fault = funded();
    fault.round.state = RoundState::Fault;
    fault.round.fault = FaultCode::OperatorInjected;
    fault.revision = max - 1;
    fault.last_sequence = max - 1;
    storage.restore_for_test(fault);
    const JournalCommit rejected_at_fault = next_commit(fault, fault, EventKind::CommandRejected);
    expect(Error::CounterExhausted, storage.commit(rejected_at_fault));

    Snapshot idle = fault;
    idle.round.state = RoundState::Idle;
    const JournalCommit final = next_commit(fault, idle);
    expect(Error::Ok, storage.commit(final));
    Snapshot loaded{};
    expect(Error::Ok, storage.load_latest(loaded));
    TEST_ASSERT_EQUAL_UINT64(max, loaded.revision);
    TEST_ASSERT_EQUAL_UINT64(max, loaded.last_sequence);
    Event event{};
    expect(Error::Ok, storage.read_event(max, event));
    TEST_ASSERT_EQUAL_UINT64(max, event.sequence);
    expect(Error::CounterExhausted, storage.commit(final));
}

void test_corrupt_storage_never_looks_empty() {
    expect(Error::Ok, storage.commit(first_commit()));
    storage.corrupt();
    Snapshot sentinel = funded();
    sentinel.revision = 777;
    expect(Error::StorageCorrupt, storage.load_latest(sentinel));
    TEST_ASSERT_EQUAL_UINT64(777, sentinel.revision);
    Event event{};
    event.sequence = 888;
    expect(Error::StorageCorrupt, storage.read_event(1, event));
    TEST_ASSERT_EQUAL_UINT64(888, event.sequence);
    expect(Error::StorageCorrupt, storage.commit(first_commit()));

    storage.reset();
    Snapshot malformed = funded();
    malformed.round.state = static_cast<RoundState>(255);
    storage.restore_for_test(malformed);
    expect(Error::StorageCorrupt, storage.load_latest(sentinel));
    TEST_ASSERT_EQUAL_UINT64(777, sentinel.revision);
}

} // namespace

void setUp() {
    storage.reset();
    small_storage.reset();
}

void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_config_rejects_invalid_limits_and_deadlines);
    RUN_TEST(test_accounting_conserves_escrow_payout_refund_and_idle_transfers);
    RUN_TEST(test_accounting_detects_missing_money_bad_escrow_and_overflow);
    RUN_TEST(test_snapshot_rejects_schema_metadata_and_state_corruption);
    RUN_TEST(test_snapshot_rejects_bet_and_result_corruption);
    RUN_TEST(test_snapshot_rechecks_canonical_exposure_and_unique_ids);
    RUN_TEST(test_reserve_table_and_invalid_output_unchanged);
    RUN_TEST(test_empty_storage_and_missing_events_leave_outputs_unchanged);
    RUN_TEST(test_commits_append_events_without_changing_prior_records);
    RUN_TEST(test_incomplete_or_inconsistent_commits_do_not_publish);
    RUN_TEST(test_crash_before_commit_preserves_previous_snapshot_and_log);
    RUN_TEST(test_crash_after_commit_exposes_complete_new_snapshot_and_log);
    RUN_TEST(test_log_full_preserves_terminal_reserve_for_recovery_and_idle);
    RUN_TEST(test_counter_reserves_reject_logging_and_allow_final_resolution);
    RUN_TEST(test_corrupt_storage_never_looks_empty);
    return UNITY_END();
}
