#include <unity.h>

#include "mini_etg/engine.hpp"
#include "mini_etg/sim/bots.hpp"
#include "mini_etg/sim/random.hpp"
#include "mini_etg/sim/wheel.hpp"
#include "../support/memory_storage.hpp"

#include <array>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>

using namespace mini_etg;
using namespace mini_etg::sim;
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

MemoryStorage storage;

#define EXPECT_ERROR(expected, expression) \
    TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(expression))

class CountingRandom final : public IRandom {
public:
    std::uint32_t next_u32() noexcept override { ++calls; return word; }
    std::uint32_t word{37}; // Accepted by bound37 and maps to pocket0.
    std::size_t calls{0};
};

std::array<Credits, kSeatCount> funding() {
    std::array<Credits, kSeatCount> credits{};
    credits.fill(1000);
    return credits;
}

Snapshot open_snapshot(RoundId round = 1) {
    Snapshot snapshot{};
    snapshot.round.id = round;
    snapshot.round.state = RoundState::BetsOpen;
    snapshot.round.resolution = Resolution::Pending;
    for (Seat& seat : snapshot.seats) seat.credits = 1000;
    return snapshot;
}

void expect_empty(const WheelBatch& batch) { TEST_ASSERT_EQUAL_UINT64(0, batch.count); }

void expect_report(const WheelReport& report, RoundId round, WheelSignal signal, TickMs observed) {
    TEST_ASSERT_EQUAL_UINT64(round, report.round_id);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(signal), static_cast<int>(report.signal));
    TEST_ASSERT_EQUAL_UINT32(observed, report.observed_at_ms);
}

void expect_same_request(const BetRequest& expected, const BetRequest& actual) {
    TEST_ASSERT_EQUAL_UINT8(expected.seat, actual.seat);
    TEST_ASSERT_EQUAL_INT64(expected.stake, actual.stake);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(expected.selection.kind), static_cast<int>(actual.selection.kind));
    TEST_ASSERT_EQUAL_UINT8(expected.selection.count, actual.selection.count);
    for (std::size_t i = 0; i < expected.selection.numbers.size(); ++i) {
        TEST_ASSERT_EQUAL_UINT8(expected.selection.numbers[i].value, actual.selection.numbers[i].value);
    }
}

void expect_canonical(const BetSelection& selection) {
    BetSelection checked{};
    EXPECT_ERROR(Error::Ok, canonicalize(selection, checked));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(checked.kind), static_cast<int>(selection.kind));
    TEST_ASSERT_EQUAL_UINT8(checked.count, selection.count);
    for (std::size_t i = 0; i < checked.numbers.size(); ++i) {
        TEST_ASSERT_EQUAL_UINT8(checked.numbers[i].value, selection.numbers[i].value);
    }
}

void close_bets(Engine& engine) {
    EXPECT_ERROR(Error::Ok, engine.begin_closing());
    EXPECT_ERROR(Error::Ok, engine.close_bets());
}

void place_one(Engine& engine) {
    BetRequest request{};
    request.stake = 5;
    request.selection.kind = BetKind::Straight;
    request.selection.count = 1;
    request.selection.numbers[0] = Number{0};
    BetId accepted = 0;
    EXPECT_ERROR(Error::Ok, engine.place_bet(request, accepted));
}

void assert_conserved(const Engine& engine) {
    EXPECT_ERROR(Error::Ok, check_accounting(engine.snapshot()));
    Credits total = engine.snapshot().round.escrow + engine.snapshot().house_result;
    for (const Seat& seat : engine.snapshot().seats) total += seat.credits;
    TEST_ASSERT_EQUAL_INT64(8000, total);
}

void test_seeded_source_is_repeatable_and_matches_known_mt_sequence() {
    SeededRandom known(5489);
    constexpr std::array<std::uint32_t, 5> expected{{3499211612u, 581869302u, 3890346734u, 3586334585u, 545404204u}};
    for (const auto word : expected) TEST_ASSERT_EQUAL_UINT32(word, known.next_u32());
    SeededRandom first(20261111), second(20261111), different(20261112);
    bool differs = false;
    for (std::size_t i = 0; i < 1000; ++i) {
        const auto word = first.next_u32();
        TEST_ASSERT_EQUAL_UINT32(word, second.next_u32());
        differs = differs || word != different.next_u32();
    }
    TEST_ASSERT_TRUE(differs);
}

void test_wheel_partial_polling_uses_capture_times_and_reports_once() {
    CountingRandom random;
    SimulatedWheel wheel(random);
    EXPECT_ERROR(Error::Ok, wheel.start(7, 1000));
    TEST_ASSERT_EQUAL_UINT64(1, random.calls);
    expect_empty(wheel.poll(1099));
    const WheelBatch motion = wheel.poll(1100);
    TEST_ASSERT_EQUAL_UINT64(1, motion.count);
    expect_report(motion.reports[0], 7, WheelSignal::MotionDetected, 1100);
    expect_empty(wheel.poll(1100));
    expect_empty(wheel.poll(1999));
    const WheelBatch result = wheel.poll(2000);
    TEST_ASSERT_EQUAL_UINT64(1, result.count);
    expect_report(result.reports[0], 7, WheelSignal::BallSettled, 2000);
    TEST_ASSERT_EQUAL_UINT8(0, result.reports[0].pocket.value);
    expect_empty(wheel.poll(2001));
    expect_empty(wheel.poll(50000));
    TEST_ASSERT_EQUAL_UINT64(1, random.calls);
}

void test_delayed_poll_emits_both_timely_observations_in_order() {
    CountingRandom random;
    SimulatedWheel wheel(random);
    EXPECT_ERROR(Error::Ok, wheel.start(123, 0));
    const WheelBatch batch = wheel.poll(50000);
    TEST_ASSERT_EQUAL_UINT64(2, batch.count);
    expect_report(batch.reports[0], 123, WheelSignal::MotionDetected, 100);
    expect_report(batch.reports[1], 123, WheelSignal::BallSettled, 1000);
    expect_empty(wheel.poll(50000));
}

void test_wheel_wraparound_and_custom_timing() {
    CountingRandom random;
    SimulatedWheel wheel(random, WheelTiming{10, 30});
    constexpr TickMs started = std::numeric_limits<TickMs>::max() - 15;
    EXPECT_ERROR(Error::Ok, wheel.start(1, started));
    expect_empty(wheel.poll(started + TickMs{9}));
    const WheelBatch motion = wheel.poll(started + TickMs{10});
    TEST_ASSERT_EQUAL_UINT64(1, motion.count);
    expect_report(motion.reports[0], 1, WheelSignal::MotionDetected, started + TickMs{10});
    const WheelBatch result = wheel.poll(started + TickMs{30});
    TEST_ASSERT_EQUAL_UINT64(1, result.count);
    expect_report(result.reports[0], 1, WheelSignal::BallSettled, started + TickMs{30});
    expect_empty(wheel.poll(started + TickMs{31}));
}

void test_stop_and_restart_discard_prior_observations() {
    CountingRandom random;
    SimulatedWheel wheel(random);
    EXPECT_ERROR(Error::Ok, wheel.start(1, 0));
    wheel.stop();
    expect_empty(wheel.poll(30000));
    EXPECT_ERROR(Error::Ok, wheel.start(2, 30000));
    EXPECT_ERROR(Error::Ok, wheel.start(3, 40000));
    expect_empty(wheel.poll(40099));
    const WheelBatch batch = wheel.poll(41000);
    TEST_ASSERT_EQUAL_UINT64(2, batch.count);
    expect_report(batch.reports[0], 3, WheelSignal::MotionDetected, 40100);
    expect_report(batch.reports[1], 3, WheelSignal::BallSettled, 41000);
}

void test_invalid_wheel_inputs_fail_without_random_draws() {
    CountingRandom random;
    for (WheelTiming timing : {WheelTiming{0, 1000}, WheelTiming{100, 100},
                               WheelTiming{1000, 100}, WheelTiming{1, TickMs{1} << 31}}) {
        SimulatedWheel wheel(random, timing);
        EXPECT_ERROR(Error::InvalidConfig, wheel.start(1, 0));
        expect_empty(wheel.poll(30000));
    }
    SimulatedWheel wheel(random);
    EXPECT_ERROR(Error::InvalidConfig, wheel.start(0, 0));
    TEST_ASSERT_EQUAL_UINT64(0, random.calls);
    EXPECT_ERROR(Error::Ok, wheel.arm_fault(SimFault::NoSpin));
    EXPECT_ERROR(Error::InvalidConfig, wheel.arm_fault(static_cast<SimFault>(255)));
    EXPECT_ERROR(Error::Ok, wheel.start(1, 0));
    expect_empty(wheel.poll(30000)); // Invalid arm did not replace the prior fault.
}

void test_armed_fault_survives_stop_and_applies_only_to_next_start() {
    CountingRandom random;
    SimulatedWheel wheel(random);
    EXPECT_ERROR(Error::Ok, wheel.arm_fault(SimFault::BallTimeout));
    wheel.stop();
    EXPECT_ERROR(Error::Ok, wheel.start(1, 0));
    const WheelBatch first = wheel.poll(30000);
    TEST_ASSERT_EQUAL_UINT64(1, first.count);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WheelSignal::MotionDetected), static_cast<int>(first.reports[0].signal));
    expect_empty(wheel.poll(60000));
    EXPECT_ERROR(Error::Ok, wheel.start(2, 60000));
    const WheelBatch second = wheel.poll(61000);
    TEST_ASSERT_EQUAL_UINT64(2, second.count);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(WheelSignal::BallSettled), static_cast<int>(second.reports[1].signal));
    EXPECT_ERROR(Error::Ok, wheel.arm_fault(SimFault::NoSpin));
    EXPECT_ERROR(Error::Ok, wheel.arm_fault(SimFault::None));
    EXPECT_ERROR(Error::Ok, wheel.start(3, 70000));
    TEST_ASSERT_EQUAL_UINT64(2, wheel.poll(71000).count);
}

void test_every_injected_fault_refunds_through_the_actual_engine() {
    constexpr std::array<SimFault, 4> faults{{SimFault::NoSpin, SimFault::BallTimeout,
        SimFault::InvalidSensor, SimFault::StartFailure}};
    constexpr std::array<FaultCode, 4> expected{{FaultCode::NoSpin, FaultCode::BallTimeout,
        FaultCode::InvalidSensor, FaultCode::WheelStartFailed}};
    for (std::size_t i = 0; i < faults.size(); ++i) {
        storage.reset();
        CountingRandom random;
        SimulatedWheel wheel(random);
        Engine engine(wheel, storage);
        EXPECT_ERROR(Error::Ok, engine.initialize({}, funding()));
        EXPECT_ERROR(Error::Ok, engine.open_bets());
        place_one(engine);
        close_bets(engine);
        EXPECT_ERROR(Error::Ok, wheel.arm_fault(faults[i]));
        const Error started = engine.start_spin(1000);
        if (faults[i] == SimFault::StartFailure) {
            EXPECT_ERROR(Error::WheelError, started);
        } else {
            EXPECT_ERROR(Error::Ok, started);
            EXPECT_ERROR(Error::WheelError, engine.tick(31000));
        }
        TEST_ASSERT_EQUAL_INT(static_cast<int>(RoundState::Fault), static_cast<int>(engine.snapshot().round.state));
        TEST_ASSERT_EQUAL_INT(static_cast<int>(expected[i]), static_cast<int>(engine.snapshot().round.fault));
        TEST_ASSERT_EQUAL_INT64(1000, engine.snapshot().seats[0].credits);
        TEST_ASSERT_EQUAL_UINT64(0, engine.snapshot().seats[0].meters.games_played);
        TEST_ASSERT_EQUAL_INT64(0, engine.snapshot().house_result);
        TEST_ASSERT_EQUAL_UINT64(1, random.calls);
        assert_conserved(engine);
        EXPECT_ERROR(Error::Ok, engine.acknowledge_fault());
        EXPECT_ERROR(Error::Ok, engine.open_bets());
        place_one(engine);
        close_bets(engine);
        EXPECT_ERROR(Error::Ok, engine.start_spin(40000));
        EXPECT_ERROR(Error::Ok, engine.tick(41000));
        EXPECT_ERROR(Error::Ok, engine.settle());
        TEST_ASSERT_EQUAL_INT64(1175, engine.snapshot().seats[0].credits);
        TEST_ASSERT_EQUAL_UINT64(1, engine.snapshot().seats[0].meters.games_played);
        assert_conserved(engine);
    }
}

void test_fault_attempts_preserve_later_seeded_wheel_results() {
    for (SimFault fault : {SimFault::NoSpin, SimFault::BallTimeout, SimFault::InvalidSensor, SimFault::StartFailure}) {
        SeededRandom baseline_random(445566), faulty_random(445566);
        SimulatedWheel baseline(baseline_random), faulty(faulty_random);
        EXPECT_ERROR(Error::Ok, baseline.start(1, 0));
        TEST_ASSERT_EQUAL_UINT64(2, baseline.poll(1000).count);
        EXPECT_ERROR(Error::Ok, faulty.arm_fault(fault));
        EXPECT_ERROR(fault == SimFault::StartFailure ? Error::WheelError : Error::Ok, faulty.start(1, 0));
        faulty.poll(30000);
        for (RoundId round = 2; round < 50; ++round) {
            EXPECT_ERROR(Error::Ok, baseline.start(round, 0));
            EXPECT_ERROR(Error::Ok, faulty.start(round, 0));
            const auto ordinary = baseline.poll(1000);
            const auto recovered = faulty.poll(1000);
            TEST_ASSERT_EQUAL_UINT64(2, recovered.count);
            TEST_ASSERT_EQUAL_UINT8(ordinary.reports[1].pocket.value, recovered.reports[1].pocket.value);
        }
    }
}

void test_all_bot_strategies_produce_legal_distinct_patterns() {
    SeededRandom random(123456);
    std::array<bool, 3> dozen_groups{}, column_groups{}, mixed_kinds{};
    std::array<bool, 2> even_odd{};
    for (SeatId seat = 0; seat < kSeatCount; ++seat) {
        TEST_ASSERT_NOT_NULL(bot_name(seat));
        TEST_ASSERT_TRUE(std::strlen(bot_name(seat)) > 0);
        for (SeatId earlier = 0; earlier < seat; ++earlier) {
            TEST_ASSERT_TRUE(std::strcmp(bot_name(earlier), bot_name(seat)) != 0);
        }
    }
    for (RoundId round = 1; round <= 120; ++round) {
        const Snapshot snapshot = open_snapshot(round);
        for (SeatId seat = 0; seat < kSeatCount; ++seat) {
            BotDecision decision{};
            EXPECT_ERROR(Error::Ok, choose_bot_bet(snapshot, seat, random, decision));
            TEST_ASSERT_TRUE(decision.place);
            const auto& request = decision.request;
            TEST_ASSERT_EQUAL_UINT8(seat, request.seat);
            TEST_ASSERT_EQUAL_INT64(seat >= 2 && seat <= 5 ? 10 : 5, request.stake);
            expect_canonical(request.selection);
            const auto kind = request.selection.kind;
            const auto first = request.selection.numbers[0].value;
            switch (seat) {
            case 0:
                TEST_ASSERT_EQUAL_INT(static_cast<int>(BetKind::Straight), static_cast<int>(kind));
                TEST_ASSERT_EQUAL_UINT8(0, first);
                break;
            case 1:
                TEST_ASSERT_EQUAL_INT(static_cast<int>(BetKind::Straight), static_cast<int>(kind));
                TEST_ASSERT_TRUE(first < 37);
                break;
            case 2:
                TEST_ASSERT_EQUAL_INT(static_cast<int>(BetKind::Red), static_cast<int>(kind));
                break;
            case 3:
                TEST_ASSERT_TRUE(kind == BetKind::Odd || kind == BetKind::Even);
                even_odd[kind == BetKind::Odd ? 0 : 1] = true;
                break;
            case 4:
                TEST_ASSERT_EQUAL_INT(static_cast<int>(BetKind::Dozen), static_cast<int>(kind));
                dozen_groups[(first - 1) / 12] = true;
                break;
            case 5:
                TEST_ASSERT_EQUAL_INT(static_cast<int>(BetKind::Column), static_cast<int>(kind));
                column_groups[first - 1] = true;
                break;
            case 6:
                TEST_ASSERT_EQUAL_INT(static_cast<int>(BetKind::Split), static_cast<int>(kind));
                TEST_ASSERT_TRUE(first >= 1);
                TEST_ASSERT_EQUAL_UINT8(first + 1, request.selection.numbers[1].value);
                TEST_ASSERT_EQUAL_UINT8((first - 1) / 3, (request.selection.numbers[1].value - 1) / 3);
                break;
            case 7:
                TEST_ASSERT_TRUE(kind == BetKind::Street || kind == BetKind::Corner || kind == BetKind::SixLine);
                mixed_kinds[kind == BetKind::Street ? 0 : kind == BetKind::Corner ? 1 : 2] = true;
                break;
            }
        }
    }
    for (const bool seen : dozen_groups) TEST_ASSERT_TRUE(seen);
    for (const bool seen : column_groups) TEST_ASSERT_TRUE(seen);
    for (const bool seen : mixed_kinds) TEST_ASSERT_TRUE(seen);
    for (const bool seen : even_odd) TEST_ASSERT_TRUE(seen);
}

void test_bots_obey_affordability_limits_and_one_placement_guard() {
    CountingRandom random;
    Snapshot snapshot = open_snapshot();
    BotDecision decision{};
    snapshot.seats[1].credits = 3;
    EXPECT_ERROR(Error::Ok, choose_bot_bet(snapshot, 1, random, decision));
    TEST_ASSERT_TRUE(decision.place);
    TEST_ASSERT_EQUAL_INT64(3, decision.request.stake);
    snapshot.config.limits.max_per_bet = 2;
    EXPECT_ERROR(Error::Ok, choose_bot_bet(snapshot, 1, random, decision));
    TEST_ASSERT_EQUAL_INT64(2, decision.request.stake);
    snapshot.config.limits.max_total_per_seat = 1;
    EXPECT_ERROR(Error::Ok, choose_bot_bet(snapshot, 1, random, decision));
    TEST_ASSERT_EQUAL_INT64(1, decision.request.stake);
    const std::size_t before_skips = random.calls;
    snapshot.seats[1].credits = 0;
    EXPECT_ERROR(Error::Ok, choose_bot_bet(snapshot, 1, random, decision));
    TEST_ASSERT_FALSE(decision.place);
    snapshot = open_snapshot();
    snapshot.config.limits.min_per_bet = 6;
    EXPECT_ERROR(Error::Ok, choose_bot_bet(snapshot, 1, random, decision));
    TEST_ASSERT_FALSE(decision.place); // Never increase a 5-credit desired stake.
    snapshot = open_snapshot();
    snapshot.round.bet_count = 1;
    snapshot.round.bets[0].request.seat = 1;
    EXPECT_ERROR(Error::Ok, choose_bot_bet(snapshot, 1, random, decision));
    TEST_ASSERT_FALSE(decision.place);
    snapshot.round.bet_count = kMaxBets;
    EXPECT_ERROR(Error::Ok, choose_bot_bet(snapshot, 1, random, decision));
    TEST_ASSERT_FALSE(decision.place);
    TEST_ASSERT_EQUAL_UINT64(before_skips, random.calls);
}

void test_invalid_bot_inputs_leave_output_unchanged() {
    CountingRandom random;
    Snapshot snapshot = open_snapshot();
    BotDecision decision{};
    decision.place = true;
    decision.request.seat = 7;
    decision.request.stake = 123;
    decision.request.selection.kind = BetKind::Straight;
    decision.request.selection.count = 1;
    decision.request.selection.numbers[0] = Number{36};
    const BotDecision before = decision;
    EXPECT_ERROR(Error::InvalidSeat, choose_bot_bet(snapshot, 8, random, decision));
    snapshot.round.state = RoundState::BetsClosing;
    EXPECT_ERROR(Error::IllegalTransition, choose_bot_bet(snapshot, 1, random, decision));
    snapshot = open_snapshot();
    snapshot.config.limits.min_per_bet = 0;
    EXPECT_ERROR(Error::InvalidConfig, choose_bot_bet(snapshot, 1, random, decision));
    snapshot = open_snapshot();
    snapshot.round.bet_count = kMaxBets + 1;
    EXPECT_ERROR(Error::StorageCorrupt, choose_bot_bet(snapshot, 1, random, decision));
    snapshot = open_snapshot();
    snapshot.seats[1].credits = -1;
    EXPECT_ERROR(Error::AccountingMismatch, choose_bot_bet(snapshot, 1, random, decision));
    TEST_ASSERT_EQUAL_INT(before.place, decision.place);
    expect_same_request(before.request, decision.request);
    TEST_ASSERT_EQUAL_UINT64(0, random.calls);
}

void test_shared_components_drive_many_engine_rounds_and_preserve_money() {
    SeededRandom wheel_random(789), bot_random(456), lucky_random(123);
    SimulatedWheel wheel(wheel_random);
    Engine engine(wheel, storage, lucky_random);
    EngineConfig config{};
    config.lucky.enabled = true;
    EXPECT_ERROR(Error::Ok, engine.initialize(config, funding()));
    for (std::uint32_t round = 0; round < 100; ++round) {
        EXPECT_ERROR(Error::Ok, engine.open_bets());
        for (SeatId seat = 0; seat < kSeatCount; ++seat) {
            BotDecision decision{};
            EXPECT_ERROR(Error::Ok, choose_bot_bet(engine.snapshot(), seat, bot_random, decision));
            if (!decision.place) continue;
            BetId id = 0;
            EXPECT_ERROR(Error::Ok, engine.place_bet(decision.request, id));
            BotDecision duplicate{};
            EXPECT_ERROR(Error::Ok, choose_bot_bet(engine.snapshot(), seat, bot_random, duplicate));
            TEST_ASSERT_FALSE(duplicate.place);
        }
        close_bets(engine);
        const TickMs started = round * 100000u;
        EXPECT_ERROR(Error::Ok, engine.start_spin(started));
        // The engine must use capture time even when this poll occurs late.
        EXPECT_ERROR(Error::Ok, engine.tick(started + 50000u));
        EXPECT_ERROR(Error::Ok, engine.settle());
        EXPECT_ERROR(Error::Ok, engine.finish_round());
        assert_conserved(engine);
        TEST_ASSERT_EQUAL_UINT64(round + 1, engine.snapshot().round.id);
    }
}

void test_shared_round_path_does_not_allocate() {
    SeededRandom wheel_random(789), bot_random(456), lucky_random(123);
    SimulatedWheel wheel(wheel_random);
    Engine engine(wheel, storage, lucky_random);
    EngineConfig config{};
    config.lucky.enabled = true;
    EXPECT_ERROR(Error::Ok, engine.initialize(config, funding()));
    track_allocations = true;
    allocation_count = 0;
    EXPECT_ERROR(Error::Ok, engine.open_bets());
    for (SeatId seat = 0; seat < kSeatCount; ++seat) {
        BotDecision decision{};
        EXPECT_ERROR(Error::Ok, choose_bot_bet(engine.snapshot(), seat, bot_random, decision));
        TEST_ASSERT_TRUE(decision.place);
        BetId id = 0;
        EXPECT_ERROR(Error::Ok, engine.place_bet(decision.request, id));
    }
    close_bets(engine);
    EXPECT_ERROR(Error::Ok, engine.start_spin(0));
    EXPECT_ERROR(Error::Ok, engine.tick(1000));
    EXPECT_ERROR(Error::Ok, engine.settle());
    EXPECT_ERROR(Error::Ok, engine.finish_round());
    track_allocations = false;
    TEST_ASSERT_EQUAL_UINT64(0, allocation_count);
    assert_conserved(engine);
}

} // namespace

void setUp() {
    track_allocations = false;
    allocation_count = 0;
    storage.reset();
}

void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_seeded_source_is_repeatable_and_matches_known_mt_sequence);
    RUN_TEST(test_wheel_partial_polling_uses_capture_times_and_reports_once);
    RUN_TEST(test_delayed_poll_emits_both_timely_observations_in_order);
    RUN_TEST(test_wheel_wraparound_and_custom_timing);
    RUN_TEST(test_stop_and_restart_discard_prior_observations);
    RUN_TEST(test_invalid_wheel_inputs_fail_without_random_draws);
    RUN_TEST(test_armed_fault_survives_stop_and_applies_only_to_next_start);
    RUN_TEST(test_every_injected_fault_refunds_through_the_actual_engine);
    RUN_TEST(test_fault_attempts_preserve_later_seeded_wheel_results);
    RUN_TEST(test_all_bot_strategies_produce_legal_distinct_patterns);
    RUN_TEST(test_bots_obey_affordability_limits_and_one_placement_guard);
    RUN_TEST(test_invalid_bot_inputs_leave_output_unchanged);
    RUN_TEST(test_shared_components_drive_many_engine_rounds_and_preserve_money);
    RUN_TEST(test_shared_round_path_does_not_allocate);
    return UNITY_END();
}
