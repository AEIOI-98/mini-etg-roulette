#include <unity.h>

#include "mini_etg/engine.hpp"
#include "../support/fake_wheel.hpp"
#include "../support/memory_storage.hpp"

#include <array>
#include <cstdlib>
#include <limits>
#include <new>

using namespace mini_etg;
using mini_etg::test::FakeWheel;
using mini_etg::test::MemoryStorage;

namespace {
bool track_allocations = false;
std::size_t allocation_count = 0;
}

void* operator new(std::size_t size) {
    if (track_allocations) ++allocation_count;
    if (void* memory = std::malloc(size == 0 ? 1 : size)) return memory;
    std::abort();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

namespace {

// A scripted source makes number zero the first selected number: 37 is above
// the rejection threshold and maps to zero for a bound of 37.
class CountingRandom final : public IRandom {
public:
    std::uint32_t next_u32() noexcept override {
        ++calls;
        return word;
    }
    std::uint32_t word{37};
    std::size_t calls{0};
};

MemoryStorage storage;
FakeWheel wheel{&storage};
CountingRandom rng;

#define EXPECT_ERROR(expected, expression) \
    TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(expression))
#define EXPECT_STATE(expected, engine) \
    TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>((engine).snapshot().round.state))

EngineConfig lucky_config(std::uint16_t multiplier = 50, std::uint8_t count = 1) {
    EngineConfig config{};
    config.lucky.enabled = true;
    config.lucky.min_count = count;
    config.lucky.max_count = count;
    config.lucky.table = {};
    config.lucky.table[0] = {multiplier, 1};
    config.lucky.table_count = 1;
    return config;
}

std::array<Credits, kSeatCount> funding(Credits amount = 1000) {
    std::array<Credits, kSeatCount> result{};
    result.fill(amount);
    return result;
}

BetRequest straight(SeatId seat = 0, std::uint8_t number = 0, Credits stake = 5) {
    BetRequest request{};
    request.seat = seat;
    request.stake = stake;
    request.selection.kind = BetKind::Straight;
    request.selection.count = 1;
    request.selection.numbers[0] = Number{number};
    return request;
}

void place(Engine& engine, const BetRequest& request = straight()) {
    BetId id = 0;
    EXPECT_ERROR(Error::Ok, engine.place_bet(request, id));
    TEST_ASSERT_TRUE(id != 0);
}

void initialize_open(Engine& engine, const EngineConfig& config = lucky_config()) {
    EXPECT_ERROR(Error::Ok, engine.initialize(config, funding()));
    EXPECT_ERROR(Error::Ok, engine.open_bets());
}

void close(Engine& engine) {
    EXPECT_ERROR(Error::Ok, engine.begin_closing());
    EXPECT_ERROR(Error::Ok, engine.close_bets());
}

void start(Engine& engine) {
    close(engine);
    EXPECT_ERROR(Error::Ok, engine.start_spin(1000));
}

Error record_result(Engine& engine, std::uint8_t pocket = 0) {
    wheel.motion(1100);
    wheel.ball(1200, pocket);
    return engine.tick(1200);
}

void assert_same_draw(const LuckyDraw& expected, const LuckyDraw& actual) {
    TEST_ASSERT_EQUAL_UINT8(expected.count, actual.count);
    for (std::size_t i = 0; i < kMaxLuckyNumbers; ++i) {
        TEST_ASSERT_EQUAL_UINT8(expected.entries[i].number.value, actual.entries[i].number.value);
        TEST_ASSERT_EQUAL_UINT16(expected.entries[i].gross_multiplier, actual.entries[i].gross_multiplier);
    }
}

void assert_conserved(const Engine& engine, Credits total = 8000) {
    EXPECT_ERROR(Error::Ok, check_accounting(engine.snapshot()));
    Credits available = 0;
    for (const Seat& seat : engine.snapshot().seats) available += seat.credits;
    TEST_ASSERT_EQUAL_INT64(total, available + engine.snapshot().round.escrow + engine.snapshot().house_result);
}

void test_lucky_initialization_requires_rng_but_standard_does_not() {
    Engine engine(wheel, storage);
    EXPECT_ERROR(Error::InvalidConfig, engine.initialize(lucky_config(), funding()));
    TEST_ASSERT_EQUAL_UINT64(0, storage.event_count());
    EXPECT_ERROR(Error::Ok, engine.initialize({}, funding()));
    EXPECT_ERROR(Error::Ok, engine.open_bets());
    place(engine);
    start(engine);
    EXPECT_ERROR(Error::Ok, record_result(engine));
    EXPECT_ERROR(Error::Ok, engine.settle());
    TEST_ASSERT_EQUAL_INT64(1175, engine.snapshot().seats[0].credits);
    TEST_ASSERT_EQUAL_UINT8(0, engine.snapshot().round.lucky.count);
}

void test_draw_happens_only_on_close_and_is_journalled_once() {
    Engine engine(wheel, storage, rng);
    initialize_open(engine);
    place(engine);
    TEST_ASSERT_EQUAL_UINT64(0, rng.calls);
    TEST_ASSERT_EQUAL_UINT8(29, engine.snapshot().round.bets[0].profit_odds);
    EXPECT_ERROR(Error::IllegalTransition, engine.close_bets());
    TEST_ASSERT_EQUAL_UINT64(0, rng.calls);
    EXPECT_ERROR(Error::Ok, engine.begin_closing());
    TEST_ASSERT_EQUAL_UINT64(0, rng.calls);
    const Sequence prior_sequence = engine.snapshot().last_sequence;
    EXPECT_ERROR(Error::Ok, engine.close_bets());
    TEST_ASSERT_TRUE(rng.calls > 0);
    const auto calls_after_draw = rng.calls;
    TEST_ASSERT_EQUAL_UINT8(1, engine.snapshot().round.lucky.count);
    TEST_ASSERT_EQUAL_UINT8(0, engine.snapshot().round.lucky.entries[0].number.value);
    TEST_ASSERT_EQUAL_UINT16(50, engine.snapshot().round.lucky.entries[0].gross_multiplier);
    TEST_ASSERT_EQUAL_UINT64(prior_sequence + 2, engine.snapshot().last_sequence);
    Event reveal{};
    EXPECT_ERROR(Error::Ok, storage.read_event(engine.snapshot().last_sequence, reveal));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EventKind::LuckyNumbersDrawn), static_cast<int>(reveal.kind));
    assert_same_draw(engine.snapshot().round.lucky, reveal.lucky);
    const LuckyDraw recorded = engine.snapshot().round.lucky;
    EXPECT_ERROR(Error::IllegalTransition, engine.close_bets());
    BetId id = 99;
    EXPECT_ERROR(Error::IllegalTransition, engine.place_bet(straight(0, 0, 1), id));
    TEST_ASSERT_EQUAL_UINT64(99, id);
    TEST_ASSERT_EQUAL_UINT64(calls_after_draw, rng.calls);
    assert_same_draw(recorded, engine.snapshot().round.lucky);
}

void test_boost_replaces_gross_return_and_other_bets_keep_standard_payouts() {
    Engine engine(wheel, storage, rng);
    initialize_open(engine);
    place(engine);
    BetRequest split{};
    split.seat = 1;
    split.stake = 5;
    const Number numbers[] = {{0}, {1}};
    EXPECT_ERROR(Error::Ok, make_inside_bet(BetKind::Split, numbers, 2, split.selection));
    place(engine, split);
    place(engine, straight(2, 1));
    start(engine);
    EXPECT_ERROR(Error::Ok, record_result(engine));
    EXPECT_ERROR(Error::Ok, engine.settle());
    TEST_ASSERT_EQUAL_INT64(1245, engine.snapshot().seats[0].credits); // 995 + 5*50.
    TEST_ASSERT_EQUAL_INT64(1085, engine.snapshot().seats[1].credits); // 995 + 5*18.
    TEST_ASSERT_EQUAL_INT64(995, engine.snapshot().seats[2].credits);
    TEST_ASSERT_EQUAL_INT64(-325, engine.snapshot().house_result);
    TEST_ASSERT_EQUAL_UINT64(1, engine.snapshot().seats[0].meters.games_won);
    TEST_ASSERT_EQUAL_UINT64(1, engine.snapshot().seats[1].meters.games_won);
    TEST_ASSERT_EQUAL_UINT64(0, engine.snapshot().seats[2].meters.games_won);
    assert_conserved(engine);
}

void test_unselected_straight_uses_reduced_base_return() {
    Engine engine(wheel, storage, rng);
    initialize_open(engine);
    place(engine, straight(0, 1));
    start(engine);
    EXPECT_ERROR(Error::Ok, record_result(engine, 23)); // Pocket 23 is number 1.
    EXPECT_ERROR(Error::Ok, engine.settle());
    TEST_ASSERT_EQUAL_INT64(1145, engine.snapshot().seats[0].credits); // 995 + 5*30.
    TEST_ASSERT_EQUAL_INT64(-145, engine.snapshot().house_result);
    assert_conserved(engine);
}

void test_uint16_multiplier_is_not_truncated_in_settlement_or_events() {
    Engine engine(wheel, storage, rng);
    initialize_open(engine, lucky_config(65535));
    place(engine, straight(0, 0, 100));
    start(engine);
    EXPECT_ERROR(Error::Ok, record_result(engine));
    EXPECT_ERROR(Error::Ok, engine.settle());
    TEST_ASSERT_EQUAL_INT64(6554400, engine.snapshot().seats[0].credits);
    TEST_ASSERT_EQUAL_INT64(-6553400, engine.snapshot().house_result);
    Event settled{};
    EXPECT_ERROR(Error::Ok, storage.read_event(engine.snapshot().last_sequence - 1, settled));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EventKind::BetSettled), static_cast<int>(settled.kind));
    TEST_ASSERT_EQUAL_INT64(6553500, settled.amount);
    assert_conserved(engine);
}

void test_close_commit_crash_refunds_without_rerolling_or_partial_reveal() {
    for (bool after : {false, true}) {
        storage.reset();
        wheel.reset();
        rng.calls = 0;
        Engine engine(wheel, storage, rng);
        initialize_open(engine);
        place(engine);
        EXPECT_ERROR(Error::Ok, engine.begin_closing());
        const Sequence before = engine.snapshot().last_sequence;
        if (after) storage.fail_next_after_commit();
        else storage.fail_next_before_commit();
        EXPECT_ERROR(after ? Error::CommitOutcomeUnknown : Error::StorageIoError, engine.close_bets());
        EXPECT_STATE(RoundState::BetsClosing, engine);
        TEST_ASSERT_EQUAL_UINT8(0, engine.snapshot().round.lucky.count);
        Snapshot saved{};
        EXPECT_ERROR(Error::Ok, storage.load_latest(saved));
        TEST_ASSERT_EQUAL_INT(static_cast<int>(after ? RoundState::BetsClosed : RoundState::BetsClosing),
                              static_cast<int>(saved.round.state));
        TEST_ASSERT_EQUAL_UINT64(before + (after ? 2 : 0), saved.last_sequence);
        TEST_ASSERT_EQUAL_UINT8(after ? 1 : 0, saved.round.lucky.count);
        const auto consumed = rng.calls;
        TEST_ASSERT_TRUE(consumed > 0);
        Engine restarted(wheel, storage); // Recovery does not need an RNG.
        RecoveryReport report{};
        EXPECT_ERROR(Error::Ok, restarted.recover(report));
        TEST_ASSERT_EQUAL_INT(static_cast<int>(RecoveryAction::Refunded), static_cast<int>(report.action));
        TEST_ASSERT_EQUAL_INT64(1000, restarted.snapshot().seats[0].credits);
        TEST_ASSERT_EQUAL_UINT64(0, restarted.snapshot().seats[0].meters.games_played);
        assert_same_draw(saved.round.lucky, restarted.snapshot().round.lucky);
        const Sequence resolved = restarted.snapshot().last_sequence;
        EXPECT_ERROR(Error::Ok, restarted.recover(report));
        TEST_ASSERT_EQUAL_UINT64(resolved, restarted.snapshot().last_sequence);
        TEST_ASSERT_EQUAL_UINT64(consumed, rng.calls);
        assert_conserved(restarted);
    }
}

void test_result_and_payout_crash_boundaries_use_recorded_multiplier_once() {
    for (bool during_payout : {false, true}) {
        for (bool after : {false, true}) {
            storage.reset();
            wheel.reset();
            rng.calls = 0;
            Engine engine(wheel, storage, rng);
            initialize_open(engine);
            place(engine);
            start(engine);
            const LuckyDraw recorded = engine.snapshot().round.lucky;
            if (during_payout) EXPECT_ERROR(Error::Ok, record_result(engine));
            if (after) storage.fail_next_after_commit();
            else storage.fail_next_before_commit();
            const Error error = during_payout ? engine.settle() : record_result(engine);
            EXPECT_ERROR(after ? Error::CommitOutcomeUnknown : Error::StorageIoError, error);
            const auto consumed = rng.calls;
            Engine restarted(wheel, storage);
            RecoveryReport report{};
            EXPECT_ERROR(Error::Ok, restarted.recover(report));
            const bool should_pay = during_payout || after;
            TEST_ASSERT_EQUAL_INT64(should_pay ? 1245 : 1000, restarted.snapshot().seats[0].credits);
            TEST_ASSERT_EQUAL_INT64(should_pay ? -245 : 0, restarted.snapshot().house_result);
            TEST_ASSERT_EQUAL_UINT64(should_pay ? 1 : 0, restarted.snapshot().seats[0].meters.games_played);
            TEST_ASSERT_EQUAL_UINT64(should_pay ? 1 : 0, restarted.snapshot().seats[0].meters.games_won);
            assert_same_draw(recorded, restarted.snapshot().round.lucky);
            const Sequence resolved = restarted.snapshot().last_sequence;
            EXPECT_ERROR(Error::Ok, restarted.recover(report));
            TEST_ASSERT_EQUAL_UINT64(resolved, restarted.snapshot().last_sequence);
            TEST_ASSERT_EQUAL_UINT64(consumed, rng.calls);
            assert_conserved(restarted);
        }
    }
}

void test_recovery_does_not_consume_a_new_random_source() {
    Engine engine(wheel, storage, rng);
    initialize_open(engine);
    place(engine);
    start(engine);
    EXPECT_ERROR(Error::Ok, record_result(engine));
    const LuckyDraw recorded = engine.snapshot().round.lucky;
    CountingRandom replacement;
    replacement.word = 12345;
    Engine restarted(wheel, storage, replacement);
    RecoveryReport report{};
    EXPECT_ERROR(Error::Ok, restarted.recover(report));
    TEST_ASSERT_EQUAL_UINT64(0, replacement.calls);
    TEST_ASSERT_EQUAL_INT64(1245, restarted.snapshot().seats[0].credits);
    assert_same_draw(recorded, restarted.snapshot().round.lucky);
    EXPECT_ERROR(Error::Ok, restarted.finish_round());
    EXPECT_ERROR(Error::Ok, restarted.open_bets());
    TEST_ASSERT_EQUAL_UINT8(0, restarted.snapshot().round.lucky.count);
    TEST_ASSERT_EQUAL_UINT64(0, replacement.calls);
}

void test_snapshot_rejects_missing_invalid_or_early_draws() {
    Engine engine(wheel, storage, rng);
    initialize_open(engine);
    place(engine);
    const Snapshot open = engine.snapshot();
    close(engine);
    const Snapshot closed = engine.snapshot();
    EXPECT_ERROR(Error::Ok, validate_snapshot(closed));
    Snapshot changed = closed;
    changed.round.lucky = {};
    EXPECT_ERROR(Error::StorageCorrupt, validate_snapshot(changed));
    changed = closed;
    changed.round.lucky.entries[0].gross_multiplier = 75; // Not in this table.
    EXPECT_ERROR(Error::StorageCorrupt, validate_snapshot(changed));
    changed = closed;
    changed.round.lucky.entries[0].number.value = 37;
    EXPECT_ERROR(Error::StorageCorrupt, validate_snapshot(changed));
    changed = closed;
    changed.round.bets[0].profit_odds = 35;
    EXPECT_ERROR(Error::StorageCorrupt, validate_snapshot(changed));
    changed = closed;
    changed.config.lucky.enabled = false;
    EXPECT_ERROR(Error::StorageCorrupt, validate_snapshot(changed));
    changed = closed;
    changed.format_version = 1;
    EXPECT_ERROR(Error::StorageCorrupt, validate_snapshot(changed));
    changed = open;
    changed.round.lucky = closed.round.lucky;
    EXPECT_ERROR(Error::StorageCorrupt, validate_snapshot(changed));
}

void test_snapshot_rejects_duplicate_lucky_numbers() {
    Engine engine(wheel, storage, rng);
    initialize_open(engine, lucky_config(50, 2));
    close(engine);
    Snapshot changed = engine.snapshot();
    EXPECT_ERROR(Error::Ok, validate_snapshot(changed));
    changed.round.lucky.entries[1].number = changed.round.lucky.entries[0].number;
    EXPECT_ERROR(Error::StorageCorrupt, validate_snapshot(changed));
    storage.restore_for_test(changed);
    Engine restarted(wheel, storage);
    RecoveryReport report{};
    EXPECT_ERROR(Error::StorageCorrupt, restarted.recover(report));
    EXPECT_ERROR(Error::NotReady, restarted.open_bets());
}

void test_worst_multiplier_headroom_is_reserved_before_any_random_draw() {
    EngineConfig config{};
    config.lucky.enabled = true; // Default table reaches 500x.
    Engine engine(wheel, storage, rng);
    auto credits = funding(0);
    credits[0] = std::numeric_limits<Credits>::max() - 100;
    EXPECT_ERROR(Error::Ok, engine.initialize(config, credits));
    EXPECT_ERROR(Error::Ok, engine.open_bets());
    BetId id = 123;
    EXPECT_ERROR(Error::ArithmeticOverflow, engine.place_bet(straight(0, 0, 1), id));
    TEST_ASSERT_EQUAL_UINT64(123, id);
    TEST_ASSERT_EQUAL_UINT64(0, rng.calls);
    TEST_ASSERT_EQUAL_UINT64(0, engine.snapshot().round.bet_count);
    TEST_ASSERT_EQUAL_INT64(0, engine.snapshot().round.escrow);
    TEST_ASSERT_EQUAL_INT64(credits[0], engine.snapshot().seats[0].credits);
}

void test_worst_multiplier_house_underflow_is_rejected_before_acceptance() {
    constexpr Credits maximum = std::numeric_limits<Credits>::max();
    Snapshot saved{};
    saved.revision = 1;
    saved.last_sequence = 1;
    saved.config.lucky.enabled = true;
    saved.seats[0].credits = maximum - 1135;
    saved.seats[1].credits = 100;
    saved.seats[0].meters.credits_out = 1000;
    saved.house_result = -maximum + 35;
    EXPECT_ERROR(Error::Ok, validate_snapshot(saved));
    storage.restore_for_test(saved);
    Engine engine(wheel, storage, rng);
    RecoveryReport report{};
    EXPECT_ERROR(Error::Ok, engine.recover(report));
    EXPECT_ERROR(Error::Ok, engine.open_bets());
    BetId id = 321;
    // A 30x base payout fits. The possible 500x payout underflows house_result.
    EXPECT_ERROR(Error::ArithmeticOverflow, engine.place_bet(straight(1, 0, 1), id));
    TEST_ASSERT_EQUAL_UINT64(321, id);
    TEST_ASSERT_EQUAL_UINT64(0, rng.calls);
    TEST_ASSERT_EQUAL_INT64(100, engine.snapshot().seats[1].credits);
    TEST_ASSERT_EQUAL_INT64(-maximum + 35, engine.snapshot().house_result);
    EXPECT_ERROR(Error::Ok, check_accounting(engine.snapshot()));
}

void test_reveal_event_has_counter_and_storage_reserve() {
    Engine engine(wheel, storage, rng);
    initialize_open(engine);
    place(engine);
    ResolutionReserve enabled{};
    EXPECT_ERROR(Error::Ok, required_resolution_reserve(engine.snapshot(), enabled));
    Snapshot standard = engine.snapshot();
    standard.config.lucky.enabled = false;
    ResolutionReserve disabled{};
    EXPECT_ERROR(Error::Ok, required_resolution_reserve(standard, disabled));
    TEST_ASSERT_EQUAL_UINT64(disabled.revisions, enabled.revisions);
    TEST_ASSERT_EQUAL_UINT64(disabled.event_sequences + 1, enabled.event_sequences);
    EXPECT_ERROR(Error::Ok, engine.begin_closing());
    EXPECT_ERROR(Error::Ok, required_resolution_reserve(engine.snapshot(), enabled));
    standard = engine.snapshot();
    standard.config.lucky.enabled = false;
    EXPECT_ERROR(Error::Ok, required_resolution_reserve(standard, disabled));
    TEST_ASSERT_EQUAL_UINT64(disabled.event_sequences + 1, enabled.event_sequences);
    EXPECT_ERROR(Error::Ok, engine.close_bets());
    EXPECT_ERROR(Error::Ok, required_resolution_reserve(engine.snapshot(), enabled));
    standard = engine.snapshot();
    standard.config.lucky.enabled = false;
    EXPECT_ERROR(Error::Ok, required_resolution_reserve(standard, disabled));
    TEST_ASSERT_EQUAL_UINT64(disabled.event_sequences, enabled.event_sequences);
}

void test_lucky_round_path_does_not_allocate() {
    Engine engine(wheel, storage, rng);
    EXPECT_ERROR(Error::Ok, engine.initialize(lucky_config(), funding()));
    track_allocations = true;
    allocation_count = 0;
    EXPECT_ERROR(Error::Ok, engine.open_bets());
    for (SeatId seat = 0; seat < kSeatCount; ++seat) place(engine, straight(seat, 0, 1));
    start(engine);
    EXPECT_ERROR(Error::Ok, record_result(engine));
    EXPECT_ERROR(Error::Ok, engine.settle());
    EXPECT_ERROR(Error::Ok, engine.finish_round());
    track_allocations = false;
    TEST_ASSERT_EQUAL_UINT64(0, allocation_count);
    assert_conserved(engine);
}

void test_exact_event_capacity_covers_reveal_result_recovery_and_idle() {
    // Nine initialization events plus twelve events for this one-bet round:
    // open, bet, closing, closed+reveal, spin, result, recovered payout, idle.
    static MemoryStorage limited(21);
    limited.reset();
    FakeWheel limited_wheel(&limited);
    Engine engine(limited_wheel, limited, rng);
    initialize_open(engine);
    place(engine);
    close(engine);
    EXPECT_ERROR(Error::Ok, engine.start_spin(1000));
    limited_wheel.motion(1100);
    limited_wheel.ball(1200, 0);
    EXPECT_ERROR(Error::Ok, engine.tick(1200));
    Engine restarted(limited_wheel, limited);
    RecoveryReport report{};
    EXPECT_ERROR(Error::Ok, restarted.recover(report));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(RecoveryAction::PayoutCompleted), static_cast<int>(report.action));
    TEST_ASSERT_EQUAL_INT64(1245, restarted.snapshot().seats[0].credits);
    EXPECT_ERROR(Error::Ok, restarted.finish_round());
    TEST_ASSERT_EQUAL_UINT64(21, limited.event_count());
    assert_conserved(restarted);
}

void test_rejection_logging_preserves_lucky_reveal_and_resolution_sequence_ids() {
    constexpr Sequence maximum = std::numeric_limits<Sequence>::max();
    Snapshot saved{};
    saved.revision = 1;
    saved.last_sequence = maximum - 13;
    saved.config = lucky_config();
    for (Seat& seat : saved.seats) {
        seat.credits = 1000;
        seat.meters.credits_in = 1000;
    }
    EXPECT_ERROR(Error::Ok, validate_snapshot(saved));
    storage.restore_for_test(saved);
    Engine engine(wheel, storage, rng);
    RecoveryReport report{};
    EXPECT_ERROR(Error::Ok, engine.recover(report));
    EXPECT_ERROR(Error::Ok, engine.open_bets());
    place(engine);
    // One rejection fits exactly before the protected remainder of the round.
    EXPECT_ERROR(Error::IllegalTransition, engine.close_bets());
    start(engine);
    EXPECT_ERROR(Error::Ok, record_result(engine));
    TEST_ASSERT_EQUAL_UINT64(maximum - 4, engine.snapshot().last_sequence);
    const Sequence before = engine.snapshot().last_sequence;
    EXPECT_ERROR(Error::CounterExhausted, engine.close_bets());
    TEST_ASSERT_EQUAL_UINT64(before, engine.snapshot().last_sequence);
    Engine restarted(wheel, storage);
    EXPECT_ERROR(Error::Ok, restarted.recover(report));
    TEST_ASSERT_EQUAL_INT64(1245, restarted.snapshot().seats[0].credits);
    EXPECT_ERROR(Error::Ok, restarted.finish_round());
    TEST_ASSERT_EQUAL_UINT64(maximum, restarted.snapshot().last_sequence);
    assert_conserved(restarted);
}

} // namespace

void setUp() {
    track_allocations = false;
    allocation_count = 0;
    storage.reset();
    wheel.reset();
    rng.calls = 0;
    rng.word = 37;
}

void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_lucky_initialization_requires_rng_but_standard_does_not);
    RUN_TEST(test_draw_happens_only_on_close_and_is_journalled_once);
    RUN_TEST(test_boost_replaces_gross_return_and_other_bets_keep_standard_payouts);
    RUN_TEST(test_unselected_straight_uses_reduced_base_return);
    RUN_TEST(test_uint16_multiplier_is_not_truncated_in_settlement_or_events);
    RUN_TEST(test_close_commit_crash_refunds_without_rerolling_or_partial_reveal);
    RUN_TEST(test_result_and_payout_crash_boundaries_use_recorded_multiplier_once);
    RUN_TEST(test_recovery_does_not_consume_a_new_random_source);
    RUN_TEST(test_snapshot_rejects_missing_invalid_or_early_draws);
    RUN_TEST(test_snapshot_rejects_duplicate_lucky_numbers);
    RUN_TEST(test_worst_multiplier_headroom_is_reserved_before_any_random_draw);
    RUN_TEST(test_worst_multiplier_house_underflow_is_rejected_before_acceptance);
    RUN_TEST(test_reveal_event_has_counter_and_storage_reserve);
    RUN_TEST(test_lucky_round_path_does_not_allocate);
    RUN_TEST(test_exact_event_capacity_covers_reveal_result_recovery_and_idle);
    RUN_TEST(test_rejection_logging_preserves_lucky_reveal_and_resolution_sequence_ids);
    return UNITY_END();
}
