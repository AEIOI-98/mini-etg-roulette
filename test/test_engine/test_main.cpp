#include <unity.h>

#include "mini_etg/engine.hpp"
#include "../support/fake_wheel.hpp"
#include "../support/memory_storage.hpp"
#include "../support/seeded_random.hpp"

#include <array>
#include <cstdlib>
#include <initializer_list>
#include <limits>
#include <new>

using namespace mini_etg;
using mini_etg::test::FakeWheel;
using mini_etg::test::MemoryStorage;

namespace {
bool track_allocations = false;
std::size_t allocation_count = 0;
}

// Observe accidental C++ heap use in the actual round path, including adapters.
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
FakeWheel wheel{&storage};

#define EXPECT_ERROR(expected, expression) \
    TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(expression))
#define EXPECT_STATE(expected, engine) \
    TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>((engine).snapshot().round.state))

std::array<Credits, kSeatCount> funding(Credits amount = 1000) {
    std::array<Credits, kSeatCount> result{};
    result.fill(amount);
    return result;
}

void initialize(Engine& engine, const EngineConfig& config = {}) {
    EXPECT_ERROR(Error::Ok, engine.initialize(config, funding()));
    EXPECT_ERROR(Error::Ok, check_accounting(engine.snapshot()));
}

BetRequest straight(SeatId seat, std::uint8_t number, Credits stake) {
    BetRequest request{};
    request.seat = seat;
    request.stake = stake;
    request.selection.kind = BetKind::Straight;
    request.selection.count = 1;
    request.selection.numbers[0] = Number{number};
    return request;
}

BetId place(Engine& engine, const BetRequest& request) {
    BetId id = 0;
    EXPECT_ERROR(Error::Ok, engine.place_bet(request, id));
    TEST_ASSERT_TRUE(id != 0);
    return id;
}

void open_with_bet(Engine& engine, std::uint8_t number = 32, Credits stake = 10) {
    EXPECT_ERROR(Error::Ok, engine.open_bets());
    place(engine, straight(0, number, stake));
}

void start(Engine& engine, TickMs now = 0) {
    EXPECT_ERROR(Error::Ok, engine.begin_closing());
    EXPECT_ERROR(Error::Ok, engine.close_bets());
    EXPECT_ERROR(Error::Ok, engine.start_spin(now));
}

void record_result(Engine& engine, std::uint8_t pocket = 1) {
    wheel.motion(wheel.started_at + 1);
    wheel.ball(wheel.started_at + 100, pocket);
    EXPECT_ERROR(Error::Ok, engine.tick(wheel.started_at + 100));
    EXPECT_STATE(RoundState::Result, engine);
}

void expect_refund(const Engine& engine, FaultCode fault) {
    EXPECT_STATE(RoundState::Fault, engine);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(fault), static_cast<int>(engine.snapshot().round.fault));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Resolution::Refunded), static_cast<int>(engine.snapshot().round.resolution));
    TEST_ASSERT_EQUAL_INT64(1000, engine.snapshot().seats[0].credits);
    TEST_ASSERT_EQUAL_INT64(0, engine.snapshot().round.escrow);
    TEST_ASSERT_EQUAL_INT64(0, engine.snapshot().house_result);
    TEST_ASSERT_EQUAL_UINT64(0, engine.snapshot().seats[0].meters.games_played);
    TEST_ASSERT_EQUAL_UINT64(0, engine.snapshot().seats[0].meters.games_won);
    EXPECT_ERROR(Error::Ok, check_accounting(engine.snapshot()));
}

void expect_same_money(const Snapshot& before, const Snapshot& after) {
    TEST_ASSERT_EQUAL_INT64(before.house_result, after.house_result);
    TEST_ASSERT_EQUAL_INT64(before.round.escrow, after.round.escrow);
    TEST_ASSERT_EQUAL_UINT64(before.round.id, after.round.id);
    TEST_ASSERT_EQUAL_UINT64(before.round.bet_count, after.round.bet_count);
    for (std::size_t i = 0; i < kSeatCount; ++i) {
        TEST_ASSERT_EQUAL_INT64(before.seats[i].credits, after.seats[i].credits);
        TEST_ASSERT_EQUAL_INT64(before.seats[i].meters.credits_in, after.seats[i].meters.credits_in);
        TEST_ASSERT_EQUAL_INT64(before.seats[i].meters.credits_out, after.seats[i].meters.credits_out);
        TEST_ASSERT_EQUAL_UINT64(before.seats[i].meters.games_played, after.seats[i].meters.games_played);
        TEST_ASSERT_EQUAL_UINT64(before.seats[i].meters.games_won, after.seats[i].meters.games_won);
    }
}

Event last_event(const Engine& engine) {
    Event event{};
    EXPECT_ERROR(Error::Ok, storage.read_event(engine.snapshot().last_sequence, event));
    return event;
}

void reach(Engine& engine, RoundState state) {
    initialize(engine);
    if (state == RoundState::Idle) return;
    open_with_bet(engine);
    if (state == RoundState::BetsOpen) return;
    EXPECT_ERROR(Error::Ok, engine.begin_closing());
    if (state == RoundState::BetsClosing) return;
    EXPECT_ERROR(Error::Ok, engine.close_bets());
    if (state == RoundState::BetsClosed) return;
    EXPECT_ERROR(Error::Ok, engine.start_spin(0));
    if (state == RoundState::Spinning) return;
    if (state == RoundState::Fault) {
        EXPECT_ERROR(Error::Ok, engine.raise_fault(FaultCode::OperatorInjected));
        return;
    }
    record_result(engine);
    if (state == RoundState::Result) return;
    EXPECT_ERROR(Error::Ok, engine.settle());
}

void test_initialization_and_credit_meters() {
    Engine engine(wheel, storage);
    EXPECT_ERROR(Error::NotReady, engine.open_bets());
    RecoveryReport report{};
    EXPECT_ERROR(Error::NoJournal, engine.recover(report));
    initialize(engine);
    EXPECT_STATE(RoundState::Idle, engine);
    TEST_ASSERT_EQUAL_UINT64(0, engine.snapshot().round.id);
    for (const auto& seat : engine.snapshot().seats) {
        TEST_ASSERT_EQUAL_INT64(1000, seat.credits);
        TEST_ASSERT_EQUAL_INT64(1000, seat.meters.credits_in);
        TEST_ASSERT_EQUAL_INT64(0, seat.meters.credits_out);
    }
    EXPECT_ERROR(Error::Ok, engine.credit_in(0, 25));
    EXPECT_ERROR(Error::Ok, engine.credit_out(0, 10));
    TEST_ASSERT_EQUAL_INT64(1015, engine.snapshot().seats[0].credits);
    TEST_ASSERT_EQUAL_INT64(1025, engine.snapshot().seats[0].meters.credits_in);
    TEST_ASSERT_EQUAL_INT64(10, engine.snapshot().seats[0].meters.credits_out);
    EXPECT_ERROR(Error::Ok, check_accounting(engine.snapshot()));
    EXPECT_ERROR(Error::AlreadyInitialized, engine.initialize({}, funding()));
}

void test_invalid_config_and_funding_do_not_initialize() {
    Engine engine(wheel, storage);
    EngineConfig config{};
    config.limits.min_per_bet = 0;
    EXPECT_ERROR(Error::InvalidConfig, engine.initialize(config, funding()));
    TEST_ASSERT_EQUAL_UINT64(0, storage.event_count());
    auto credits = funding();
    credits[3] = -1;
    TEST_ASSERT_NOT_EQUAL(static_cast<int>(Error::Ok), static_cast<int>(engine.initialize({}, credits)));
    TEST_ASSERT_EQUAL_UINT64(0, storage.event_count());
    credits.fill(std::numeric_limits<Credits>::max());
    EXPECT_ERROR(Error::ArithmeticOverflow, engine.initialize({}, credits));
    TEST_ASSERT_EQUAL_UINT64(0, storage.event_count());
}

void test_credit_transfer_errors_preserve_money_and_are_logged() {
    Engine engine(wheel, storage);
    initialize(engine);
    const Snapshot before = engine.snapshot();
    EXPECT_ERROR(Error::InvalidSeat, engine.credit_in(8, 1));
    EXPECT_ERROR(Error::InvalidAmount, engine.credit_in(0, 0));
    EXPECT_ERROR(Error::InvalidAmount, engine.credit_out(0, -1));
    EXPECT_ERROR(Error::InsufficientCredits, engine.credit_out(0, 1001));
    expect_same_money(before, engine.snapshot());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EventKind::CommandRejected), static_cast<int>(last_event(engine).kind));
    TEST_ASSERT_EQUAL_UINT64(before.last_sequence + 4, engine.snapshot().last_sequence);
}

void test_credit_meter_overflow_is_rejected() {
    Engine engine(wheel, storage);
    auto credits = funding(0);
    credits[0] = std::numeric_limits<Credits>::max();
    EXPECT_ERROR(Error::Ok, engine.initialize({}, credits));
    EXPECT_ERROR(Error::ArithmeticOverflow, engine.credit_in(0, 1));
    EXPECT_ERROR(Error::Ok, engine.credit_out(0, 100));
    // The balance has room, but the lifetime credits-in meter does not.
    EXPECT_ERROR(Error::ArithmeticOverflow, engine.credit_in(0, 100));
    TEST_ASSERT_EQUAL_INT64(std::numeric_limits<Credits>::max() - 100, engine.snapshot().seats[0].credits);
}

void test_winning_round_preserves_conservation_and_pays_gross_once() {
    Engine engine(wheel, storage);
    initialize(engine);
    open_with_bet(engine);
    TEST_ASSERT_EQUAL_INT64(990, engine.snapshot().seats[0].credits);
    TEST_ASSERT_EQUAL_INT64(10, engine.snapshot().round.escrow);
    EXPECT_ERROR(Error::Ok, check_accounting(engine.snapshot()));
    start(engine);
    TEST_ASSERT_TRUE(wheel.start_was_journalled);
    record_result(engine, 1); // Physical pocket 1 is printed number 32.
    TEST_ASSERT_EQUAL_UINT8(32, engine.snapshot().round.result_number.value);
    TEST_ASSERT_EQUAL_INT64(990, engine.snapshot().seats[0].credits);
    EXPECT_ERROR(Error::Ok, engine.settle());
    TEST_ASSERT_EQUAL_INT64(1350, engine.snapshot().seats[0].credits);
    TEST_ASSERT_EQUAL_INT64(-350, engine.snapshot().house_result);
    TEST_ASSERT_EQUAL_INT64(0, engine.snapshot().round.escrow);
    TEST_ASSERT_EQUAL_UINT64(1, engine.snapshot().seats[0].meters.games_played);
    TEST_ASSERT_EQUAL_UINT64(1, engine.snapshot().seats[0].meters.games_won);
    TEST_ASSERT_EQUAL_INT64(1000, engine.snapshot().seats[0].meters.credits_in);
    TEST_ASSERT_EQUAL_INT64(0, engine.snapshot().seats[0].meters.credits_out);
    EXPECT_ERROR(Error::IllegalTransition, engine.settle());
    TEST_ASSERT_EQUAL_INT64(1350, engine.snapshot().seats[0].credits);
    EXPECT_ERROR(Error::Ok, check_accounting(engine.snapshot()));
    EXPECT_ERROR(Error::Ok, engine.finish_round());
    EXPECT_STATE(RoundState::Idle, engine);
    EXPECT_ERROR(Error::Ok, engine.credit_in(0, 1));
    EXPECT_ERROR(Error::Ok, check_accounting(engine.snapshot()));
}

void test_losing_round_counts_played_but_not_won() {
    Engine engine(wheel, storage);
    initialize(engine);
    open_with_bet(engine);
    start(engine);
    record_result(engine, 0);
    EXPECT_ERROR(Error::Ok, engine.settle());
    TEST_ASSERT_EQUAL_INT64(990, engine.snapshot().seats[0].credits);
    TEST_ASSERT_EQUAL_INT64(10, engine.snapshot().house_result);
    TEST_ASSERT_EQUAL_UINT64(1, engine.snapshot().seats[0].meters.games_played);
    TEST_ASSERT_EQUAL_UINT64(0, engine.snapshot().seats[0].meters.games_won);
    TEST_ASSERT_EQUAL_UINT64(0, engine.snapshot().seats[1].meters.games_played);
    EXPECT_ERROR(Error::Ok, check_accounting(engine.snapshot()));
}

void test_games_won_uses_net_profit_not_any_winning_bet() {
    Engine engine(wheel, storage);
    initialize(engine);
    EXPECT_ERROR(Error::Ok, engine.open_bets());
    BetSelection red{}, black{};
    EXPECT_ERROR(Error::Ok, make_even_money_bet(BetKind::Red, red));
    EXPECT_ERROR(Error::Ok, make_even_money_bet(BetKind::Black, black));
    place(engine, {0, red, 10});
    place(engine, {0, black, 10});
    start(engine);
    record_result(engine, 1);
    EXPECT_ERROR(Error::Ok, engine.settle());
    TEST_ASSERT_EQUAL_INT64(1000, engine.snapshot().seats[0].credits);
    TEST_ASSERT_EQUAL_UINT64(1, engine.snapshot().seats[0].meters.games_played);
    TEST_ASSERT_EQUAL_UINT64(0, engine.snapshot().seats[0].meters.games_won);
    EXPECT_ERROR(Error::Ok, check_accounting(engine.snapshot()));
}

void test_empty_rounds_finish_or_recover_without_player_meter_changes() {
    for (bool recover_before_result : {false, true}) {
        storage.reset();
        wheel.reset();
        Engine engine(wheel, storage);
        initialize(engine);
        EXPECT_ERROR(Error::Ok, engine.open_bets());
        start(engine);
        if (recover_before_result) {
            RecoveryReport report{};
            EXPECT_ERROR(Error::Ok, engine.recover(report));
            TEST_ASSERT_EQUAL_INT(static_cast<int>(RecoveryAction::Refunded), static_cast<int>(report.action));
            EXPECT_STATE(RoundState::Fault, engine);
            EXPECT_ERROR(Error::Ok, engine.acknowledge_fault());
        } else {
            record_result(engine);
            EXPECT_ERROR(Error::Ok, engine.settle());
            EXPECT_ERROR(Error::Ok, engine.finish_round());
        }
        EXPECT_STATE(RoundState::Idle, engine);
        TEST_ASSERT_EQUAL_INT64(0, engine.snapshot().round.escrow);
        TEST_ASSERT_EQUAL_INT64(0, engine.snapshot().house_result);
        for (const Seat& seat : engine.snapshot().seats) {
            TEST_ASSERT_EQUAL_INT64(1000, seat.credits);
            TEST_ASSERT_EQUAL_UINT64(0, seat.meters.games_played);
            TEST_ASSERT_EQUAL_UINT64(0, seat.meters.games_won);
        }
        EXPECT_ERROR(Error::Ok, check_accounting(engine.snapshot()));
    }
}

void test_all_eight_seats_settle_and_conserve() {
    Engine engine(wheel, storage);
    initialize(engine);
    EXPECT_ERROR(Error::Ok, engine.open_bets());
    for (SeatId seat = 0; seat < kSeatCount; ++seat) {
        place(engine, straight(seat, seat == 0 ? 32 : seat, seat + 1));
    }
    start(engine);
    record_result(engine, 1);
    EXPECT_ERROR(Error::Ok, engine.settle());
    Credits total = engine.snapshot().house_result;
    for (std::size_t i = 0; i < kSeatCount; ++i) {
        const auto& seat = engine.snapshot().seats[i];
        total += seat.credits;
        TEST_ASSERT_EQUAL_INT64(i == 0 ? 1035 : 1000 - static_cast<Credits>(i + 1), seat.credits);
        TEST_ASSERT_EQUAL_UINT64(1, seat.meters.games_played);
        TEST_ASSERT_EQUAL_UINT64(i == 0 ? 1 : 0, seat.meters.games_won);
    }
    TEST_ASSERT_EQUAL_INT64(8000, total);
    TEST_ASSERT_EQUAL_UINT64(1, engine.snapshot().seats[0].meters.games_won);
    EXPECT_ERROR(Error::Ok, check_accounting(engine.snapshot()));
}

void test_min_max_total_and_canonical_duplicate_limits() {
    Engine engine(wheel, storage);
    EngineConfig config{};
    config.limits = {5, 20, 25};
    initialize(engine, config);
    EXPECT_ERROR(Error::Ok, engine.open_bets());
    BetId untouched = 777;
    EXPECT_ERROR(Error::LimitExceeded, engine.place_bet(straight(0, 1, 4), untouched));
    EXPECT_ERROR(Error::LimitExceeded, engine.place_bet(straight(0, 1, 21), untouched));
    TEST_ASSERT_EQUAL_UINT64(777, untouched);
    BetSelection split{};
    split.kind = BetKind::Split;
    split.count = 2;
    split.numbers[0] = Number{1};
    split.numbers[1] = Number{2};
    place(engine, {0, split, 15});
    split.numbers[0] = Number{2};
    split.numbers[1] = Number{1};
    EXPECT_ERROR(Error::LimitExceeded, engine.place_bet({0, split, 10}, untouched));
    place(engine, {0, split, 5});
    place(engine, straight(0, 9, 5));
    EXPECT_ERROR(Error::LimitExceeded, engine.place_bet(straight(0, 10, 5), untouched));
    TEST_ASSERT_EQUAL_INT64(975, engine.snapshot().seats[0].credits);
    TEST_ASSERT_EQUAL_INT64(25, engine.snapshot().round.escrow);
    TEST_ASSERT_EQUAL_UINT64(777, untouched);
    EXPECT_ERROR(Error::Ok, check_accounting(engine.snapshot()));
}

void test_invalid_geometry_and_insufficient_credit_preserve_bet_id() {
    Engine engine(wheel, storage);
    initialize(engine);
    EXPECT_ERROR(Error::Ok, engine.credit_out(0, 995));
    EXPECT_ERROR(Error::Ok, engine.open_bets());
    BetId id = 456;
    EXPECT_ERROR(Error::InsufficientCredits, engine.place_bet(straight(0, 1, 10), id));
    EXPECT_ERROR(Error::InvalidSeat, engine.place_bet(straight(8, 1, 1), id));
    BetRequest bad = straight(0, 3, 1);
    bad.selection.kind = BetKind::Split;
    bad.selection.count = 2;
    bad.selection.numbers[1] = Number{4};
    EXPECT_ERROR(Error::InvalidGeometry, engine.place_bet(bad, id));
    TEST_ASSERT_EQUAL_UINT64(456, id);
    TEST_ASSERT_EQUAL_UINT64(0, engine.snapshot().round.bet_count);
    TEST_ASSERT_EQUAL_INT64(5, engine.snapshot().seats[0].credits);
}

void test_capacity_is_per_seat_and_accepts_all_128_slots() {
    Engine engine(wheel, storage);
    initialize(engine);
    EXPECT_ERROR(Error::Ok, engine.open_bets());
    for (SeatId seat = 0; seat < kSeatCount; ++seat) {
        for (std::uint8_t number = 0; number < kMaxBetsPerSeat; ++number) {
            place(engine, straight(seat, number, 1));
        }
        BetId id = 999;
        EXPECT_ERROR(Error::BetCapacityReached, engine.place_bet(straight(seat, 20, 1), id));
        TEST_ASSERT_EQUAL_UINT64(999, id);
    }
    TEST_ASSERT_EQUAL_UINT64(128, engine.snapshot().round.bet_count);
    EXPECT_ERROR(Error::Ok, engine.raise_fault(FaultCode::OperatorInjected));
    for (const auto& seat : engine.snapshot().seats) TEST_ASSERT_EQUAL_INT64(1000, seat.credits);
    EXPECT_ERROR(Error::Ok, check_accounting(engine.snapshot()));
}

void test_payout_headroom_is_checked_before_accepting_a_bet() {
    Engine engine(wheel, storage);
    auto credits = funding(0);
    credits[0] = std::numeric_limits<Credits>::max() - 10;
    EXPECT_ERROR(Error::Ok, engine.initialize({}, credits));
    EXPECT_ERROR(Error::Ok, engine.open_bets());
    BetId id = 123;
    EXPECT_ERROR(Error::ArithmeticOverflow, engine.place_bet(straight(0, 1, 1), id));
    TEST_ASSERT_EQUAL_UINT64(123, id);
    TEST_ASSERT_EQUAL_INT64(std::numeric_limits<Credits>::max() - 10, engine.snapshot().seats[0].credits);
    TEST_ASSERT_EQUAL_INT64(0, engine.snapshot().round.escrow);
}

void test_no_spin_fault_refunds_once() {
    Engine engine(wheel, storage);
    initialize(engine);
    open_with_bet(engine);
    start(engine);
    EXPECT_ERROR(Error::WheelError, engine.tick(2000));
    expect_refund(engine, FaultCode::NoSpin);
    EXPECT_ERROR(Error::IllegalTransition, engine.tick(3000));
    EXPECT_ERROR(Error::Ok, engine.acknowledge_fault());
    EXPECT_ERROR(Error::IllegalTransition, engine.acknowledge_fault());
    TEST_ASSERT_EQUAL_INT64(1000, engine.snapshot().seats[0].credits);
}

void test_ball_timeout_refunds() {
    Engine engine(wheel, storage);
    initialize(engine);
    open_with_bet(engine);
    start(engine);
    wheel.motion(1);
    EXPECT_ERROR(Error::Ok, engine.tick(1));
    EXPECT_ERROR(Error::WheelError, engine.tick(30000));
    expect_refund(engine, FaultCode::BallTimeout);
}

void test_timely_reports_survive_delayed_polling() {
    Engine engine(wheel, storage);
    initialize(engine);
    open_with_bet(engine);
    start(engine);
    wheel.motion(1999);
    EXPECT_ERROR(Error::Ok, engine.tick(2000));
    EXPECT_STATE(RoundState::Spinning, engine);
    wheel.ball(29999, 1);
    EXPECT_ERROR(Error::Ok, engine.tick(31000));
    EXPECT_STATE(RoundState::Result, engine);
    TEST_ASSERT_EQUAL_UINT8(32, engine.snapshot().round.result_number.value);
}

void test_two_timely_buffered_reports_survive_late_poll() {
    Engine engine(wheel, storage);
    initialize(engine);
    open_with_bet(engine);
    start(engine);
    wheel.motion(1999);
    wheel.ball(29999, 1);
    EXPECT_ERROR(Error::Ok, engine.tick(31000));
    EXPECT_STATE(RoundState::Result, engine);
}

void test_motion_exactly_at_deadline_is_late_even_with_ball() {
    Engine engine(wheel, storage);
    initialize(engine);
    open_with_bet(engine);
    start(engine);
    wheel.motion(2000);
    wheel.ball(29999, 1);
    EXPECT_ERROR(Error::WheelError, engine.tick(31000));
    expect_refund(engine, FaultCode::NoSpin);
}

void test_ball_exactly_at_deadline_is_late() {
    Engine engine(wheel, storage);
    initialize(engine);
    open_with_bet(engine);
    start(engine);
    wheel.motion(1);
    wheel.ball(30000, 1);
    EXPECT_ERROR(Error::WheelError, engine.tick(31000));
    expect_refund(engine, FaultCode::BallTimeout);
}

void test_capture_and_poll_times_wrap_around_uint32() {
    Engine engine(wheel, storage);
    initialize(engine);
    open_with_bet(engine);
    const TickMs beginning = std::numeric_limits<TickMs>::max() - 1000;
    start(engine, beginning);
    wheel.motion(beginning + 1999);
    wheel.ball(beginning + 29999, 1);
    EXPECT_ERROR(Error::Ok, engine.tick(beginning + 31000));
    EXPECT_STATE(RoundState::Result, engine);
}

void test_ambiguous_half_range_clock_values_refund() {
    constexpr TickMs half_range = TickMs{1} << 31;
    for (unsigned scenario = 0; scenario < 3; ++scenario) {
        storage.reset();
        wheel.reset();
        Engine engine(wheel, storage);
        initialize(engine);
        open_with_bet(engine);
        const TickMs beginning = std::numeric_limits<TickMs>::max() - 50;
        start(engine, beginning);
        TickMs now = beginning + half_range;
        if (scenario == 1) now = beginning - 1;
        if (scenario == 2) {
            now = beginning + 10;
            wheel.motion(beginning + half_range);
        }
        EXPECT_ERROR(Error::WheelError, engine.tick(now));
        expect_refund(engine, FaultCode::InvalidSensor);
        TEST_ASSERT_FALSE(engine.snapshot().round.has_result);
    }
}

void test_future_timestamp_faults() {
    Engine engine(wheel, storage);
    initialize(engine);
    open_with_bet(engine);
    start(engine);
    wheel.motion(11);
    EXPECT_ERROR(Error::WheelError, engine.tick(10));
    expect_refund(engine, FaultCode::InvalidSensor);
}

void test_backdated_timestamp_across_polls_faults() {
    Engine engine(wheel, storage);
    initialize(engine);
    open_with_bet(engine);
    start(engine);
    wheel.motion(10);
    EXPECT_ERROR(Error::Ok, engine.tick(10));
    wheel.ball(5, 1);
    EXPECT_ERROR(Error::WheelError, engine.tick(20));
    expect_refund(engine, FaultCode::InvalidSensor);
}

void test_duplicate_motion_faults() {
    Engine engine(wheel, storage);
    initialize(engine);
    open_with_bet(engine);
    start(engine);
    wheel.motion(1);
    wheel.motion(2);
    EXPECT_ERROR(Error::WheelError, engine.tick(2));
    expect_refund(engine, FaultCode::InvalidSensor);
}

void test_result_without_motion_faults() {
    Engine engine(wheel, storage);
    initialize(engine);
    open_with_bet(engine);
    start(engine);
    wheel.ball(100, 1);
    EXPECT_ERROR(Error::WheelError, engine.tick(100));
    expect_refund(engine, FaultCode::InvalidSensor);
}

void test_invalid_pocket_faults() {
    Engine engine(wheel, storage);
    initialize(engine);
    open_with_bet(engine);
    start(engine);
    wheel.motion(1);
    wheel.ball(100, 37);
    EXPECT_ERROR(Error::WheelError, engine.tick(100));
    expect_refund(engine, FaultCode::InvalidSensor);
}

void test_stale_round_report_refunds_without_accepting_result() {
    Engine engine(wheel, storage);
    initialize(engine);
    open_with_bet(engine);
    start(engine);
    wheel.push({engine.snapshot().round.id + 1, WheelSignal::MotionDetected, 1, {}, FaultCode::None});
    EXPECT_ERROR(Error::WheelError, engine.tick(1));
    EXPECT_STATE(RoundState::Fault, engine);
    TEST_ASSERT_FALSE(engine.snapshot().round.has_result);
    TEST_ASSERT_EQUAL_INT64(1000, engine.snapshot().seats[0].credits);
    EXPECT_ERROR(Error::Ok, check_accounting(engine.snapshot()));
}

void test_bad_batch_count_and_fault_code_are_rejected() {
    for (bool bad_count : {false, true}) {
        storage.reset();
        wheel.reset();
        Engine engine(wheel, storage);
        initialize(engine);
        open_with_bet(engine);
        start(engine);
        if (bad_count) {
            WheelBatch bad{};
            bad.count = 3;
            wheel.inject_batch(bad);
        } else {
            wheel.fault(1, FaultCode::None);
        }
        EXPECT_ERROR(Error::WheelError, engine.tick(1));
        expect_refund(engine, FaultCode::InvalidSensor);
    }
}

void test_sensor_fault_and_wheel_start_failure_refund() {
    {
        Engine engine(wheel, storage);
        initialize(engine);
        open_with_bet(engine);
        start(engine);
        wheel.fault(1, FaultCode::InvalidSensor);
        EXPECT_ERROR(Error::WheelError, engine.tick(1));
        expect_refund(engine, FaultCode::InvalidSensor);
    }
    storage.reset();
    wheel.reset();
    Engine engine(wheel, storage);
    initialize(engine);
    open_with_bet(engine);
    EXPECT_ERROR(Error::Ok, engine.begin_closing());
    EXPECT_ERROR(Error::Ok, engine.close_bets());
    wheel.start_error = Error::WheelError;
    EXPECT_ERROR(Error::WheelError, engine.start_spin(0));
    TEST_ASSERT_TRUE(wheel.start_was_journalled);
    expect_refund(engine, FaultCode::WheelStartFailed);
}

void test_invalid_explicit_faults_are_logged_without_refunding() {
    for (bool spinning : {false, true}) {
        storage.reset();
        wheel.reset();
        Engine engine(wheel, storage);
        initialize(engine);
        if (spinning) {
            open_with_bet(engine);
            start(engine);
        }
        for (FaultCode invalid : {FaultCode::None, static_cast<FaultCode>(255)}) {
            const Snapshot before = engine.snapshot();
            EXPECT_ERROR(Error::WheelError, engine.raise_fault(invalid));
            expect_same_money(before, engine.snapshot());
            EXPECT_STATE(before.round.state, engine);
            TEST_ASSERT_EQUAL_UINT64(before.last_sequence + 1, engine.snapshot().last_sequence);
            const Event event = last_event(engine);
            TEST_ASSERT_EQUAL_INT(static_cast<int>(EventKind::CommandRejected), static_cast<int>(event.kind));
            TEST_ASSERT_EQUAL_INT(static_cast<int>(Command::RaiseFault), static_cast<int>(event.command));
            TEST_ASSERT_EQUAL_INT(static_cast<int>(Error::WheelError), static_cast<int>(event.error));
        }
        if (spinning) {
            record_result(engine);
            EXPECT_ERROR(Error::Ok, engine.settle());
            TEST_ASSERT_EQUAL_INT64(1350, engine.snapshot().seats[0].credits);
        }
    }
}

void test_fault_after_result_cannot_replace_a_loss_with_refund() {
    Engine engine(wheel, storage);
    initialize(engine);
    open_with_bet(engine);
    start(engine);
    record_result(engine, 0);
    EXPECT_ERROR(Error::IllegalTransition, engine.raise_fault(FaultCode::OperatorInjected));
    EXPECT_STATE(RoundState::Result, engine);
    EXPECT_ERROR(Error::Ok, engine.settle());
    TEST_ASSERT_EQUAL_INT64(990, engine.snapshot().seats[0].credits);
}

enum class Attempt { Open, BeginClosing, Close, Start, Tick, Settle, Finish, Fault, Acknowledge, CreditIn, CreditOut, Place, Undo, Clear, Repeat };

bool allowed(Attempt attempt, RoundState state) {
    switch (attempt) {
    case Attempt::Open: return state == RoundState::Idle;
    case Attempt::BeginClosing: return state == RoundState::BetsOpen;
    case Attempt::Close: return state == RoundState::BetsClosing;
    case Attempt::Start: return state == RoundState::BetsClosed;
    case Attempt::Tick: return state == RoundState::Spinning;
    case Attempt::Settle: return state == RoundState::Result;
    case Attempt::Finish: return state == RoundState::Payout;
    case Attempt::Fault: return state == RoundState::Idle || state == RoundState::BetsOpen || state == RoundState::BetsClosing || state == RoundState::BetsClosed || state == RoundState::Spinning;
    case Attempt::Acknowledge: return state == RoundState::Fault;
    case Attempt::CreditIn: case Attempt::CreditOut: return state == RoundState::Idle;
    case Attempt::Undo: case Attempt::Clear: case Attempt::Repeat:
    case Attempt::Place: return state == RoundState::BetsOpen;
    }
    return false;
}

Error attempt_command(Engine& engine, Attempt attempt) {
    BetId ignored = 0;
    switch (attempt) {
    case Attempt::Open: return engine.open_bets();
    case Attempt::BeginClosing: return engine.begin_closing();
    case Attempt::Close: return engine.close_bets();
    case Attempt::Start: return engine.start_spin(0);
    case Attempt::Tick: return engine.tick(0);
    case Attempt::Settle: return engine.settle();
    case Attempt::Finish: return engine.finish_round();
    case Attempt::Fault: return engine.raise_fault(FaultCode::OperatorInjected);
    case Attempt::Acknowledge: return engine.acknowledge_fault();
    case Attempt::CreditIn: return engine.credit_in(0, 1);
    case Attempt::CreditOut: return engine.credit_out(0, 1);
    case Attempt::Undo: return engine.undo_last_bet(0);
    case Attempt::Clear: return engine.clear_bets(0);
    case Attempt::Repeat: return engine.repeat_last_bets(0);
    case Attempt::Place: return engine.place_bet(straight(0, 1, 1), ignored);
    }
    return Error::InvalidConfig;
}

void test_every_illegal_command_state_pair_is_rejected_and_logged() {
    for (int state_value = 0; state_value <= static_cast<int>(RoundState::Fault); ++state_value) {
        const auto state = static_cast<RoundState>(state_value);
        for (int attempt_value = 0; attempt_value <= static_cast<int>(Attempt::Repeat); ++attempt_value) {
            const auto attempt = static_cast<Attempt>(attempt_value);
            if (allowed(attempt, state)) continue;
            storage.reset();
            wheel.reset();
            Engine engine(wheel, storage);
            reach(engine, state);
            const Snapshot before = engine.snapshot();
            EXPECT_ERROR(Error::IllegalTransition, attempt_command(engine, attempt));
            EXPECT_STATE(state, engine);
            expect_same_money(before, engine.snapshot());
            TEST_ASSERT_EQUAL_UINT64(before.last_sequence + 1, engine.snapshot().last_sequence);
            TEST_ASSERT_EQUAL_UINT64(before.revision + 1, engine.snapshot().revision);
            const auto event = last_event(engine);
            TEST_ASSERT_EQUAL_INT(static_cast<int>(EventKind::CommandRejected), static_cast<int>(event.kind));
            TEST_ASSERT_EQUAL_INT(static_cast<int>(Error::IllegalTransition), static_cast<int>(event.error));
        }
    }
}

void test_restart_from_every_round_state_is_idempotent() {
    for (int state_value = 0; state_value <= static_cast<int>(RoundState::Fault); ++state_value) {
        const auto state = static_cast<RoundState>(state_value);
        storage.reset();
        wheel.reset();
        {
            Engine engine(wheel, storage);
            reach(engine, state);
        }
        Engine restarted(wheel, storage);
        RecoveryReport report{};
        EXPECT_ERROR(Error::Ok, restarted.recover(report));
        if (state == RoundState::Result || state == RoundState::Payout) {
            EXPECT_STATE(RoundState::Payout, restarted);
            TEST_ASSERT_EQUAL_INT64(1350, restarted.snapshot().seats[0].credits);
            TEST_ASSERT_EQUAL_UINT64(1, restarted.snapshot().seats[0].meters.games_played);
            TEST_ASSERT_EQUAL_UINT64(1, restarted.snapshot().seats[0].meters.games_won);
            const auto expected_action = state == RoundState::Result ? RecoveryAction::PayoutCompleted : RecoveryAction::AlreadyPaid;
            TEST_ASSERT_EQUAL_INT(static_cast<int>(expected_action), static_cast<int>(report.action));
        } else if (state == RoundState::Idle) {
            EXPECT_STATE(RoundState::Idle, restarted);
            TEST_ASSERT_EQUAL_INT(static_cast<int>(RecoveryAction::None), static_cast<int>(report.action));
        } else {
            EXPECT_STATE(RoundState::Fault, restarted);
            TEST_ASSERT_EQUAL_INT64(1000, restarted.snapshot().seats[0].credits);
            TEST_ASSERT_EQUAL_UINT64(0, restarted.snapshot().seats[0].meters.games_played);
        }
        EXPECT_ERROR(Error::Ok, check_accounting(restarted.snapshot()));
        const Snapshot once = restarted.snapshot();
        const auto events = storage.event_count();
        for (int repeat = 0; repeat < 3; ++repeat) {
            EXPECT_ERROR(Error::Ok, restarted.recover(report));
            expect_same_money(once, restarted.snapshot());
            TEST_ASSERT_EQUAL_UINT64(events, storage.event_count());
            TEST_ASSERT_EQUAL_UINT64(once.revision, restarted.snapshot().revision);
        }
    }
}

enum class CrashPoint { Initialize, Open, Bet, BeginClosing, Close, StartSpin, Result, Payout, Refund, CreditIn, CreditOut, Rejection, Acknowledge, Finish };

void prepare_crash_point(Engine& engine, CrashPoint point) {
    if (point == CrashPoint::Initialize) return;
    initialize(engine);
    if (point == CrashPoint::Open || point == CrashPoint::CreditIn ||
        point == CrashPoint::CreditOut || point == CrashPoint::Rejection) return;
    EXPECT_ERROR(Error::Ok, engine.open_bets());
    if (point == CrashPoint::Bet) return;
    place(engine, straight(0, 32, 10));
    if (point == CrashPoint::BeginClosing || point == CrashPoint::Refund) return;
    if (point == CrashPoint::Acknowledge) {
        EXPECT_ERROR(Error::Ok, engine.raise_fault(FaultCode::OperatorInjected));
        return;
    }
    EXPECT_ERROR(Error::Ok, engine.begin_closing());
    if (point == CrashPoint::Close) return;
    EXPECT_ERROR(Error::Ok, engine.close_bets());
    if (point == CrashPoint::StartSpin) return;
    EXPECT_ERROR(Error::Ok, engine.start_spin(0));
    if (point == CrashPoint::Result) return;
    record_result(engine);
    if (point == CrashPoint::Payout) return;
    EXPECT_ERROR(Error::Ok, engine.settle());
}

void test_crash_before_and_after_each_money_or_state_commit() {
    for (int point_value = 0; point_value <= static_cast<int>(CrashPoint::Finish); ++point_value) {
        const auto point = static_cast<CrashPoint>(point_value);
        for (bool after : {false, true}) {
            storage.reset();
            wheel.reset();
            Credits expected_credit = 1000;
            bool expected_paid = false;
            {
                Engine engine(wheel, storage);
                prepare_crash_point(engine, point);
                const auto starts_before = wheel.start_calls;
                if (after) storage.fail_next_after_commit();
                else storage.fail_next_before_commit();
                Error error = Error::Ok;
                BetId id = 999;
                switch (point) {
                case CrashPoint::Initialize: error = engine.initialize({}, funding()); break;
                case CrashPoint::Open: error = engine.open_bets(); break;
                case CrashPoint::Bet: error = engine.place_bet(straight(0, 32, 10), id); break;
                case CrashPoint::BeginClosing: error = engine.begin_closing(); break;
                case CrashPoint::Close: error = engine.close_bets(); break;
                case CrashPoint::StartSpin: error = engine.start_spin(0); break;
                case CrashPoint::Result:
                    wheel.motion(1);
                    wheel.ball(100, 1);
                    error = engine.tick(100);
                    expected_paid = after;
                    break;
                case CrashPoint::Payout: error = engine.settle(); expected_paid = true; break;
                case CrashPoint::Refund: error = engine.raise_fault(FaultCode::OperatorInjected); break;
                case CrashPoint::CreditIn: error = engine.credit_in(0, 25); expected_credit += after ? 25 : 0; break;
                case CrashPoint::CreditOut: error = engine.credit_out(0, 25); expected_credit -= after ? 25 : 0; break;
                case CrashPoint::Rejection: error = engine.close_bets(); break;
                case CrashPoint::Acknowledge: error = engine.acknowledge_fault(); break;
                case CrashPoint::Finish: error = engine.finish_round(); expected_paid = true; break;
                }
                EXPECT_ERROR(after ? Error::CommitOutcomeUnknown : Error::StorageIoError, error);
                TEST_ASSERT_EQUAL_INT(static_cast<int>(RuntimeStatus::RecoveryRequired), static_cast<int>(engine.status()));
                EXPECT_ERROR(Error::NotReady, engine.credit_in(0, 1));
                if (point == CrashPoint::Bet) TEST_ASSERT_EQUAL_UINT64(999, id);
                if (point == CrashPoint::StartSpin) TEST_ASSERT_EQUAL_UINT64(starts_before, wheel.start_calls);
            }
            Engine restarted(wheel, storage);
            RecoveryReport report{};
            if (point == CrashPoint::Initialize && !after) {
                EXPECT_ERROR(Error::NoJournal, restarted.recover(report));
                initialize(restarted);
            }
            EXPECT_ERROR(Error::Ok, restarted.recover(report));
            TEST_ASSERT_EQUAL_INT64(expected_paid ? 1350 : expected_credit, restarted.snapshot().seats[0].credits);
            TEST_ASSERT_EQUAL_UINT64(expected_paid ? 1 : 0, restarted.snapshot().seats[0].meters.games_played);
            TEST_ASSERT_EQUAL_INT64(0, restarted.snapshot().round.escrow);
            EXPECT_ERROR(Error::Ok, check_accounting(restarted.snapshot()));
            const auto count = storage.event_count();
            EXPECT_ERROR(Error::Ok, restarted.recover(report));
            TEST_ASSERT_EQUAL_UINT64(count, storage.event_count());
        }
    }
}

void test_crash_during_recovery_never_refunds_or_pays_twice() {
    for (bool payout : {false, true}) {
        for (bool after : {false, true}) {
            storage.reset();
            wheel.reset();
            {
                Engine engine(wheel, storage);
                reach(engine, payout ? RoundState::Result : RoundState::Spinning);
            }
            if (after) storage.fail_next_after_commit();
            else storage.fail_next_before_commit();
            Engine interrupted(wheel, storage);
            RecoveryReport report{};
            EXPECT_ERROR(after ? Error::CommitOutcomeUnknown : Error::StorageIoError, interrupted.recover(report));
            Engine restarted(wheel, storage);
            EXPECT_ERROR(Error::Ok, restarted.recover(report));
            TEST_ASSERT_EQUAL_INT64(payout ? 1350 : 1000, restarted.snapshot().seats[0].credits);
            TEST_ASSERT_EQUAL_UINT64(payout ? 1 : 0, restarted.snapshot().seats[0].meters.games_played);
            const auto count = storage.event_count();
            EXPECT_ERROR(Error::Ok, restarted.recover(report));
            TEST_ASSERT_EQUAL_UINT64(count, storage.event_count());
        }
    }
}

void test_corrupt_storage_is_never_treated_as_empty() {
    Engine engine(wheel, storage);
    initialize(engine);
    open_with_bet(engine);
    storage.corrupt();
    Engine restarted(wheel, storage);
    RecoveryReport report{};
    EXPECT_ERROR(Error::StorageCorrupt, restarted.recover(report));
    EXPECT_ERROR(Error::StorageCorrupt, restarted.initialize({}, funding()));
    EXPECT_ERROR(Error::NotReady, restarted.open_bets());
}

void test_rejection_logging_preserves_counter_reserve_for_refund() {
    {
        Engine initial(wheel, storage);
        initialize(initial);
        Snapshot boundary = initial.snapshot();
        // Five forthcoming commits reach Spinning with exactly six event IDs
        // reserved (one active bet). The synthetic historical prefix is skipped.
        boundary.last_sequence = std::numeric_limits<Sequence>::max() - 11;
        storage.restore_for_test(boundary);
    }
    Engine engine(wheel, storage);
    RecoveryReport report{};
    EXPECT_ERROR(Error::Ok, engine.recover(report));
    open_with_bet(engine);
    start(engine);
    const auto events = storage.event_count();
    EXPECT_ERROR(Error::CounterExhausted, engine.open_bets());
    TEST_ASSERT_EQUAL_UINT64(events, storage.event_count());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(RuntimeStatus::RecoveryRequired), static_cast<int>(engine.status()));
    EXPECT_ERROR(Error::Ok, engine.recover(report));
    expect_refund(engine, FaultCode::PowerLoss);
    EXPECT_ERROR(Error::Ok, engine.acknowledge_fault());
    EXPECT_STATE(RoundState::Idle, engine);
}

void test_full_event_storage_retains_room_to_refund_and_return_idle() {
    static MemoryStorage limited(32);
    limited.reset();
    FakeWheel limited_wheel(&limited);
    Engine engine(limited_wheel, limited);
    initialize(engine);
    open_with_bet(engine);
    start(engine);
    bool refused = false;
    for (int attempt = 0; attempt < 40; ++attempt) {
        const auto count = limited.event_count();
        const auto error = engine.open_bets();
        if (error == Error::StorageFull) {
            TEST_ASSERT_EQUAL_UINT64(count, limited.event_count());
            refused = true;
            break;
        }
        EXPECT_ERROR(Error::IllegalTransition, error);
    }
    TEST_ASSERT_TRUE(refused);
    RecoveryReport report{};
    EXPECT_ERROR(Error::Ok, engine.recover(report));
    expect_refund(engine, FaultCode::PowerLoss);
    EXPECT_ERROR(Error::Ok, engine.acknowledge_fault());
    EXPECT_STATE(RoundState::Idle, engine);
}

void test_journal_events_are_contiguous_and_record_each_payout() {
    Engine engine(wheel, storage);
    initialize(engine);
    EXPECT_ERROR(Error::Ok, engine.open_bets());
    const auto winner = place(engine, straight(0, 32, 10));
    const auto loser = place(engine, straight(1, 1, 5));
    start(engine);
    record_result(engine);
    EXPECT_ERROR(Error::Ok, engine.settle());
    EXPECT_ERROR(Error::Ok, engine.finish_round());
    std::size_t accepted = 0, settled = 0, transitions = 0;
    Revision previous_revision = 0;
    for (Sequence sequence = 1; sequence <= engine.snapshot().last_sequence; ++sequence) {
        Event event{};
        EXPECT_ERROR(Error::Ok, storage.read_event(sequence, event));
        TEST_ASSERT_EQUAL_UINT64(sequence, event.sequence);
        TEST_ASSERT_TRUE(event.revision >= previous_revision);
        previous_revision = event.revision;
        if (event.kind == EventKind::BetAccepted) {
            ++accepted;
            TEST_ASSERT_EQUAL_UINT8(35, event.profit_odds);
            TEST_ASSERT_EQUAL_UINT8(1, event.selection.count);
        }
        if (event.kind == EventKind::BetSettled) {
            ++settled;
            TEST_ASSERT_TRUE(event.bet_id == winner || event.bet_id == loser);
            TEST_ASSERT_EQUAL_INT64(event.bet_id == winner ? 360 : 0, event.amount);
        }
        if (event.kind == EventKind::StateChanged) ++transitions;
    }
    TEST_ASSERT_EQUAL_UINT64(2, accepted);
    TEST_ASSERT_EQUAL_UINT64(2, settled);
    TEST_ASSERT_EQUAL_UINT64(7, transitions);
    TEST_ASSERT_EQUAL_UINT64(engine.snapshot().last_sequence, storage.event_count());
}

void test_round_and_bet_identifiers_are_not_reused() {
    Engine engine(wheel, storage);
    initialize(engine);
    RoundId previous_round = 0;
    BetId previous_bet = 0;
    for (int round = 0; round < 3; ++round) {
        EXPECT_ERROR(Error::Ok, engine.open_bets());
        TEST_ASSERT_TRUE(engine.snapshot().round.id > previous_round);
        previous_round = engine.snapshot().round.id;
        const auto id = place(engine, straight(0, 1, 1));
        TEST_ASSERT_TRUE(id > previous_bet);
        previous_bet = id;
        EXPECT_ERROR(Error::Ok, engine.raise_fault(FaultCode::OperatorInjected));
        EXPECT_ERROR(Error::Ok, engine.acknowledge_fault());
    }
    EXPECT_ERROR(Error::Ok, engine.raise_fault(FaultCode::OperatorInjected));
    EXPECT_ERROR(Error::Ok, engine.acknowledge_fault());
    EXPECT_ERROR(Error::Ok, engine.open_bets());
    TEST_ASSERT_TRUE(engine.snapshot().round.id > previous_round);
}

void test_seeded_multi_round_accounting_survives_mixed_recovery_paths() {
    Engine engine(wheel, storage);
    initialize(engine);
    auto expected_credits = funding();
    auto expected_in = funding();
    std::array<Credits, kSeatCount> expected_out{};
    std::array<std::uint64_t, kSeatCount> expected_played{};
    std::array<std::uint64_t, kSeatCount> expected_won{};
    Credits expected_house = 0;
    mini_etg::test::SeededRandom random{0x43b91e07};
    const auto draw = [&random](std::uint32_t bound) {
        std::uint32_t value = 0;
        EXPECT_ERROR(Error::Ok, uniform_below(random, bound, value));
        return value;
    };
    // An independent wheel-order oracle keeps payout expectations separate from
    // the engine's sensor-index conversion and its payout helper.
    constexpr std::array<std::uint8_t, kPocketCount> pockets{
        0, 32, 15, 19, 4, 21, 2, 25, 17, 34, 6, 27, 13, 36, 11, 30, 8, 23, 10,
        5, 24, 16, 33, 1, 20, 14, 31, 9, 22, 18, 29, 7, 28, 12, 35, 3, 26
    };

    for (std::uint32_t round = 0; round < 50; ++round) {
        const SeatId transferring = static_cast<SeatId>(round % kSeatCount);
        EXPECT_ERROR(Error::Ok, engine.credit_in(transferring, 3));
        EXPECT_ERROR(Error::Ok, engine.credit_out(transferring, 2));
        expected_credits[transferring] += 1;
        expected_in[transferring] += 3;
        expected_out[transferring] += 2;
        EXPECT_ERROR(Error::Ok, engine.open_bets());
        TEST_ASSERT_EQUAL_UINT64(round + 1, engine.snapshot().round.id);
        const std::uint8_t result_index = static_cast<std::uint8_t>(draw(kPocketCount));
        const std::uint8_t result_number = pockets[result_index];
        std::array<Credits, kSeatCount> stakes{};
        std::array<Credits, kSeatCount> returns{};
        for (SeatId seat = 0; seat < kSeatCount; ++seat) {
            if (draw(4) == 0) continue;
            const Credits stake = 1 + draw(5);
            const bool wins = draw(3) == 0;
            const auto number = static_cast<std::uint8_t>(wins ? result_number : (result_number + 1) % kPocketCount);
            place(engine, straight(seat, number, stake));
            stakes[seat] = stake;
            returns[seat] = wins ? 36 * stake : 0;
            expected_credits[seat] -= stake;
            EXPECT_ERROR(Error::Ok, check_accounting(engine.snapshot()));
        }
        const bool operator_void = round % 7 == 0;
        const bool power_loss = !operator_void && round % 11 == 0;
        RecoveryReport report{};
        if (operator_void) {
            EXPECT_ERROR(Error::Ok, engine.raise_fault(FaultCode::OperatorInjected));
        } else {
            start(engine, round * 100000u);
            if (power_loss) {
                EXPECT_ERROR(Error::Ok, engine.recover(report));
                TEST_ASSERT_EQUAL_INT(static_cast<int>(RecoveryAction::Refunded), static_cast<int>(report.action));
            } else {
                record_result(engine, result_index);
                TEST_ASSERT_EQUAL_UINT8(result_number, engine.snapshot().round.result_number.value);
                if (round % 5 == 0) {
                    EXPECT_ERROR(Error::Ok, engine.recover(report));
                    TEST_ASSERT_EQUAL_INT(static_cast<int>(RecoveryAction::PayoutCompleted), static_cast<int>(report.action));
                } else {
                    EXPECT_ERROR(Error::Ok, engine.settle());
                }
            }
        }
        for (SeatId seat = 0; seat < kSeatCount; ++seat) {
            if (operator_void || power_loss) {
                expected_credits[seat] += stakes[seat];
            } else {
                expected_credits[seat] += returns[seat];
                expected_house += stakes[seat] - returns[seat];
                if (stakes[seat] > 0) ++expected_played[seat];
                if (returns[seat] > stakes[seat]) ++expected_won[seat];
            }
        }
        // Recovery of an already completed payout/refund must preserve every
        // ledger value and append no duplicate settlements.
        const Sequence completed_sequence = engine.snapshot().last_sequence;
        EXPECT_ERROR(Error::Ok, engine.recover(report));
        TEST_ASSERT_EQUAL_UINT64(completed_sequence, engine.snapshot().last_sequence);
        if (operator_void || power_loss) {
            EXPECT_ERROR(Error::Ok, engine.acknowledge_fault());
        } else {
            EXPECT_ERROR(Error::Ok, engine.finish_round());
        }
        Credits available = 0;
        for (SeatId seat = 0; seat < kSeatCount; ++seat) {
            const Seat& actual = engine.snapshot().seats[seat];
            TEST_ASSERT_EQUAL_INT64(expected_credits[seat], actual.credits);
            TEST_ASSERT_EQUAL_INT64(expected_in[seat], actual.meters.credits_in);
            TEST_ASSERT_EQUAL_INT64(expected_out[seat], actual.meters.credits_out);
            TEST_ASSERT_EQUAL_UINT64(expected_played[seat], actual.meters.games_played);
            TEST_ASSERT_EQUAL_UINT64(expected_won[seat], actual.meters.games_won);
            available += actual.credits;
        }
        TEST_ASSERT_EQUAL_INT64(expected_house, engine.snapshot().house_result);
        TEST_ASSERT_EQUAL_INT64(8000 + round + 1, available + engine.snapshot().house_result);
        EXPECT_ERROR(Error::Ok, check_accounting(engine.snapshot()));
    }
}

void test_edits_affect_only_active_bets_for_one_seat() {
    Engine engine(wheel, storage);
    initialize(engine);
    EXPECT_ERROR(Error::Ok, engine.open_bets());
    EXPECT_ERROR(Error::NoBets, engine.undo_last_bet(0));
    EXPECT_ERROR(Error::NoBets, engine.clear_bets(0));
    EXPECT_ERROR(Error::NoBets, engine.repeat_last_bets(0));
    EXPECT_ERROR(Error::InvalidSeat, engine.undo_last_bet(kSeatCount));
    EXPECT_ERROR(Error::InvalidSeat, engine.clear_bets(kSeatCount));
    EXPECT_ERROR(Error::InvalidSeat, engine.repeat_last_bets(kSeatCount));

    const BetId first = place(engine, straight(0, 32, 3));
    const BetId second = place(engine, straight(0, 32, 7));
    const BetId other = place(engine, straight(1, 32, 5));
    EXPECT_ERROR(Error::Ok, engine.undo_last_bet(0));
    const Event undo = last_event(engine);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EventKind::BetCancelled), static_cast<int>(undo.kind));
    TEST_ASSERT_EQUAL_UINT64(second, undo.bet_id);
    TEST_ASSERT_EQUAL_INT64(7, undo.amount);
    TEST_ASSERT_EQUAL_UINT64(first, engine.snapshot().round.bets[0].id);
    TEST_ASSERT_EQUAL_UINT64(other, engine.snapshot().round.bets[1].id);
    TEST_ASSERT_EQUAL_INT64(8, engine.snapshot().round.escrow);
    TEST_ASSERT_EQUAL_INT64(997, engine.snapshot().seats[0].credits);
    // Reusing freed capacity must never reuse a cancelled placement's ID.
    for (std::size_t i = 1; i < kMaxBetsPerSeat; ++i)
        TEST_ASSERT_TRUE(place(engine, straight(0, 32, 1)) > other);
    const Snapshot before = engine.snapshot();
    EXPECT_ERROR(Error::Ok, engine.clear_bets(0));
    TEST_ASSERT_EQUAL_UINT64(before.revision + 1, engine.snapshot().revision);
    TEST_ASSERT_EQUAL_UINT64(before.last_sequence + kMaxBetsPerSeat, engine.snapshot().last_sequence);
    TEST_ASSERT_EQUAL_UINT64(1, engine.snapshot().round.bet_count);
    TEST_ASSERT_EQUAL_UINT64(other, engine.snapshot().round.bets[0].id);
    TEST_ASSERT_EQUAL_INT64(1000, engine.snapshot().seats[0].credits);
    start(engine);
    record_result(engine);
    EXPECT_ERROR(Error::Ok, engine.settle());
    TEST_ASSERT_EQUAL_UINT64(0, engine.snapshot().seats[0].meters.games_played);
    TEST_ASSERT_EQUAL_UINT64(0, engine.snapshot().seats[0].meters.games_won);
    TEST_ASSERT_EQUAL_UINT8(0, engine.snapshot().last_bets[0].count);
    TEST_ASSERT_EQUAL_UINT8(1, engine.snapshot().last_bets[1].count);
}

void test_repeat_persists_across_recovery_empty_and_void_rounds() {
    Engine engine(wheel, storage);
    initialize(engine);
    open_with_bet(engine, 1, 3);
    place(engine, straight(0, 2, 7));
    start(engine);
    record_result(engine);
    // Completing payout on boot also saves this seat's two placements.
    Engine restarted(wheel, storage);
    RecoveryReport recovery{};
    EXPECT_ERROR(Error::Ok, restarted.recover(recovery));
    EXPECT_ERROR(Error::Ok, restarted.finish_round());
    EXPECT_ERROR(Error::Ok, restarted.open_bets());
    place(restarted, straight(0, 7, 2));
    EXPECT_ERROR(Error::Ok, restarted.recover(recovery));
    EXPECT_ERROR(Error::Ok, restarted.acknowledge_fault());
    EXPECT_ERROR(Error::Ok, restarted.open_bets());
    place(restarted, straight(1, 32, 5));
    start(restarted);
    record_result(restarted);
    EXPECT_ERROR(Error::Ok, restarted.settle());
    EXPECT_ERROR(Error::Ok, restarted.finish_round());

    EXPECT_ERROR(Error::Ok, restarted.open_bets());
    const auto first = place(restarted, straight(0, 0, 1));
    const Snapshot before = restarted.snapshot();
    EXPECT_ERROR(Error::Ok, restarted.repeat_last_bets(0));
    TEST_ASSERT_EQUAL_UINT64(before.revision + 1, restarted.snapshot().revision);
    TEST_ASSERT_EQUAL_UINT64(before.last_sequence + 2, restarted.snapshot().last_sequence);
    TEST_ASSERT_EQUAL_INT64(11, restarted.snapshot().round.escrow);
    TEST_ASSERT_EQUAL_INT64(979, restarted.snapshot().seats[0].credits);
    for (unsigned i = 1; i <= 2; ++i) {
        const AcceptedBet& bet = restarted.snapshot().round.bets[i];
        TEST_ASSERT_EQUAL_UINT64(first + i, bet.id);
        TEST_ASSERT_EQUAL_UINT8(i, bet.request.selection.numbers[0].value);
        Event event{};
        EXPECT_ERROR(Error::Ok, storage.read_event(before.last_sequence + i, event));
        TEST_ASSERT_EQUAL_INT(static_cast<int>(EventKind::BetAccepted), static_cast<int>(event.kind));
        TEST_ASSERT_EQUAL_INT(static_cast<int>(Command::RepeatLastBets), static_cast<int>(event.command));
        TEST_ASSERT_EQUAL_UINT64(restarted.snapshot().revision, event.revision);
        TEST_ASSERT_EQUAL_UINT64(bet.id, event.bet_id);
    }
    EXPECT_ERROR(Error::Ok, restarted.undo_last_bet(0));
    TEST_ASSERT_EQUAL_INT64(4, restarted.snapshot().round.escrow);
    EXPECT_ERROR(Error::Ok, restarted.repeat_last_bets(0));
    TEST_ASSERT_EQUAL_INT64(14, restarted.snapshot().round.escrow);
    EXPECT_ERROR(Error::Ok, restarted.repeat_last_bets(1));
    TEST_ASSERT_EQUAL_INT64(19, restarted.snapshot().round.escrow);
}

void test_repeat_late_error_rejects_whole_pattern() {
    constexpr std::array<Error, 4> failures{Error::LimitExceeded, Error::BetCapacityReached,
                                            Error::InsufficientCredits, Error::CounterExhausted};
    for (Error expected : failures) {
        storage.reset();
        wheel.reset();
        Engine engine(wheel, storage);
        initialize(engine);
        open_with_bet(engine, 1, 10);
        place(engine, straight(0, 2, 20));
        start(engine);
        record_result(engine);
        EXPECT_ERROR(Error::Ok, engine.settle());
        EXPECT_ERROR(Error::Ok, engine.finish_round());
        if (expected == Error::InsufficientCredits) EXPECT_ERROR(Error::Ok, engine.credit_out(0, 955));
        if (expected == Error::CounterExhausted) {
            Snapshot saved = engine.snapshot();
            saved.next_bet_id = std::numeric_limits<BetId>::max() - 1;
            storage.restore_for_test(saved);
            RecoveryReport report{};
            EXPECT_ERROR(Error::Ok, engine.recover(report));
        }
        EXPECT_ERROR(Error::Ok, engine.open_bets());
        if (expected == Error::LimitExceeded) place(engine, straight(0, 2, 81));
        if (expected == Error::BetCapacityReached) {
            for (std::size_t i = 0; i < kMaxBetsPerSeat - 1; ++i) place(engine, straight(0, 3, 1));
        }
        const Snapshot before = engine.snapshot();
        EXPECT_ERROR(expected, engine.repeat_last_bets(0));
        expect_same_money(before, engine.snapshot());
        TEST_ASSERT_EQUAL_UINT64(before.next_bet_id, engine.snapshot().next_bet_id);
        TEST_ASSERT_EQUAL_UINT64(before.last_sequence + 1, engine.snapshot().last_sequence);
        const Event event = last_event(engine);
        TEST_ASSERT_EQUAL_INT(static_cast<int>(EventKind::CommandRejected), static_cast<int>(event.kind));
        TEST_ASSERT_EQUAL_INT(static_cast<int>(Command::RepeatLastBets), static_cast<int>(event.command));
    }
}

void test_removing_offsetting_stake_rechecks_all_outcomes_for_all_seats() {
    constexpr Credits maximum = std::numeric_limits<Credits>::max();
    Snapshot saved{};
    saved.revision = saved.last_sequence = 1;
    saved.house_result = -maximum + 35;
    saved.seats[0].credits = 35;
    saved.seats[1].credits = maximum - 70;
    storage.restore_for_test(saved);
    Engine engine(wheel, storage);
    RecoveryReport recovery{};
    EXPECT_ERROR(Error::Ok, engine.recover(recovery));
    EXPECT_ERROR(Error::Ok, engine.open_bets());
    BetSelection black{};
    EXPECT_ERROR(Error::Ok, make_even_money_bet(BetKind::Black, black));
    place(engine, {0, black, 35});
    place(engine, straight(1, 1, 2));
    const Snapshot before = engine.snapshot();
    EXPECT_ERROR(Error::ArithmeticOverflow, engine.undo_last_bet(0));
    EXPECT_ERROR(Error::ArithmeticOverflow, engine.clear_bets(0));
    expect_same_money(before, engine.snapshot());
    TEST_ASSERT_EQUAL_UINT64(before.next_bet_id, engine.snapshot().next_bet_id);
    TEST_ASSERT_EQUAL_UINT64(before.last_sequence + 2, engine.snapshot().last_sequence);
}

void test_repeat_validates_headroom_for_complete_pattern() {
    constexpr Credits maximum = std::numeric_limits<Credits>::max();
    Snapshot saved{};
    saved.revision = saved.last_sequence = 1;
    saved.house_result = -maximum + 35;
    saved.seats[0].credits = maximum - 35;
    saved.seats[0].meters.games_played = 1;
    auto& pattern = saved.last_bets[0];
    pattern.count = 2;
    pattern.bets[0] = {straight(0, 1, 2).selection, 2};
    EXPECT_ERROR(Error::Ok, make_even_money_bet(BetKind::Black, pattern.bets[1].selection));
    pattern.bets[1].stake = 35;
    storage.restore_for_test(saved);
    Engine engine(wheel, storage);
    RecoveryReport recovery{};
    EXPECT_ERROR(Error::Ok, engine.recover(recovery));
    EXPECT_ERROR(Error::Ok, engine.open_bets());
    BetId id = 0;
    // The first straight alone is unsafe. Including the black stake makes it safe.
    EXPECT_ERROR(Error::ArithmeticOverflow, engine.place_bet(straight(0, 1, 2), id));
    EXPECT_ERROR(Error::Ok, engine.repeat_last_bets(0));
    TEST_ASSERT_EQUAL_INT64(37, engine.snapshot().round.escrow);
    EXPECT_ERROR(Error::ArithmeticOverflow, engine.undo_last_bet(0));
}

void test_interrupted_edits_are_atomic_and_refund_only_active_stakes() {
    for (Attempt action : {Attempt::Undo, Attempt::Clear, Attempt::Repeat}) {
        for (bool committed : {false, true}) {
            storage.reset();
            wheel.reset();
            Engine engine(wheel, storage);
            initialize(engine);
            open_with_bet(engine, 1, 3);
            place(engine, straight(0, 2, 7));
            start(engine);
            record_result(engine);
            EXPECT_ERROR(Error::Ok, engine.settle());
            EXPECT_ERROR(Error::Ok, engine.finish_round());
            EXPECT_ERROR(Error::Ok, engine.open_bets());
            place(engine, straight(0, 3, 4));
            place(engine, straight(0, 4, 6));
            const Snapshot before = engine.snapshot();
            if (committed) storage.fail_next_after_commit();
            else storage.fail_next_before_commit();
            EXPECT_ERROR(committed ? Error::CommitOutcomeUnknown : Error::StorageIoError,
                         attempt_command(engine, action));
            Snapshot durable{};
            EXPECT_ERROR(Error::Ok, storage.load_latest(durable));
            const std::size_t event_count = action == Attempt::Undo ? 1 : 2;
            TEST_ASSERT_EQUAL_UINT64(before.last_sequence + (committed ? event_count : 0),
                                     durable.last_sequence);
            const std::size_t edited_count = action == Attempt::Undo ? 1 : action == Attempt::Clear ? 0 : 4;
            TEST_ASSERT_EQUAL_UINT64(committed ? edited_count : 2, durable.round.bet_count);
            Engine restarted(wheel, storage);
            RecoveryReport report{};
            EXPECT_ERROR(Error::Ok, restarted.recover(report));
            TEST_ASSERT_EQUAL_INT64(990, restarted.snapshot().seats[0].credits);
            TEST_ASSERT_EQUAL_UINT8(2, restarted.snapshot().last_bets[0].count);
            // One refund per active stake, one transition, one recovery event.
            TEST_ASSERT_EQUAL_UINT64(durable.last_sequence + durable.round.bet_count + 2,
                                     restarted.snapshot().last_sequence);
            const Sequence resolved = restarted.snapshot().last_sequence;
            EXPECT_ERROR(Error::Ok, restarted.recover(report));
            TEST_ASSERT_EQUAL_UINT64(resolved, restarted.snapshot().last_sequence);
        }
    }
}

void test_new_controls_keep_terminal_reserve_at_exact_journal_capacity() {
    // 2 initialization events, 1 open, 1 accepted, 9 reserved for resolution.
    static MemoryStorage limited(13);
    limited.reset();
    FakeWheel local_wheel(&limited);
    Engine engine(local_wheel, limited);
    std::array<Credits, kSeatCount> credits{100};
    EXPECT_ERROR(Error::Ok, engine.initialize({}, credits));
    open_with_bet(engine);
    EXPECT_ERROR(Error::Ok, engine.undo_last_bet(0)); // 1 cancellation, reserve drops by 1.
    TEST_ASSERT_EQUAL_UINT64(5, limited.event_count());
    BetId id = 999;
    EXPECT_ERROR(Error::StorageFull, engine.place_bet(straight(0, 1, 1), id));
    TEST_ASSERT_EQUAL_UINT64(999, id);
    RecoveryReport report{};
    EXPECT_ERROR(Error::Ok, engine.recover(report));
    EXPECT_ERROR(Error::Ok, engine.acknowledge_fault());
    TEST_ASSERT_EQUAL_INT64(100, engine.snapshot().seats[0].credits);
    TEST_ASSERT_EQUAL_INT64(0, engine.snapshot().round.escrow);
}

void test_round_path_does_not_allocate_on_the_heap() {
    Engine engine(wheel, storage);
    initialize(engine);
    allocation_count = 0;
    track_allocations = true;
    EXPECT_ERROR(Error::Ok, engine.open_bets());
    for (SeatId seat = 0; seat < kSeatCount; ++seat) place(engine, straight(seat, seat, 1));
    start(engine);
    record_result(engine);
    EXPECT_ERROR(Error::Ok, engine.settle());
    EXPECT_ERROR(Error::Ok, engine.finish_round());
    EXPECT_ERROR(Error::Ok, engine.open_bets());
    EXPECT_ERROR(Error::Ok, engine.repeat_last_bets(0));
    EXPECT_ERROR(Error::Ok, engine.undo_last_bet(0));
    EXPECT_ERROR(Error::Ok, engine.repeat_last_bets(0));
    EXPECT_ERROR(Error::Ok, engine.clear_bets(0));
    track_allocations = false;
    TEST_ASSERT_EQUAL_UINT64(0, allocation_count);
}

} // namespace

void setUp() {
    track_allocations = false;
    storage.reset();
    wheel.reset();
}

void tearDown() { track_allocations = false; }

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_initialization_and_credit_meters);
    RUN_TEST(test_invalid_config_and_funding_do_not_initialize);
    RUN_TEST(test_credit_transfer_errors_preserve_money_and_are_logged);
    RUN_TEST(test_credit_meter_overflow_is_rejected);
    RUN_TEST(test_winning_round_preserves_conservation_and_pays_gross_once);
    RUN_TEST(test_losing_round_counts_played_but_not_won);
    RUN_TEST(test_games_won_uses_net_profit_not_any_winning_bet);
    RUN_TEST(test_empty_rounds_finish_or_recover_without_player_meter_changes);
    RUN_TEST(test_all_eight_seats_settle_and_conserve);
    RUN_TEST(test_min_max_total_and_canonical_duplicate_limits);
    RUN_TEST(test_invalid_geometry_and_insufficient_credit_preserve_bet_id);
    RUN_TEST(test_capacity_is_per_seat_and_accepts_all_128_slots);
    RUN_TEST(test_payout_headroom_is_checked_before_accepting_a_bet);
    RUN_TEST(test_no_spin_fault_refunds_once);
    RUN_TEST(test_ball_timeout_refunds);
    RUN_TEST(test_timely_reports_survive_delayed_polling);
    RUN_TEST(test_two_timely_buffered_reports_survive_late_poll);
    RUN_TEST(test_motion_exactly_at_deadline_is_late_even_with_ball);
    RUN_TEST(test_ball_exactly_at_deadline_is_late);
    RUN_TEST(test_capture_and_poll_times_wrap_around_uint32);
    RUN_TEST(test_ambiguous_half_range_clock_values_refund);
    RUN_TEST(test_future_timestamp_faults);
    RUN_TEST(test_backdated_timestamp_across_polls_faults);
    RUN_TEST(test_duplicate_motion_faults);
    RUN_TEST(test_result_without_motion_faults);
    RUN_TEST(test_invalid_pocket_faults);
    RUN_TEST(test_stale_round_report_refunds_without_accepting_result);
    RUN_TEST(test_bad_batch_count_and_fault_code_are_rejected);
    RUN_TEST(test_sensor_fault_and_wheel_start_failure_refund);
    RUN_TEST(test_invalid_explicit_faults_are_logged_without_refunding);
    RUN_TEST(test_fault_after_result_cannot_replace_a_loss_with_refund);
    RUN_TEST(test_every_illegal_command_state_pair_is_rejected_and_logged);
    RUN_TEST(test_restart_from_every_round_state_is_idempotent);
    RUN_TEST(test_crash_before_and_after_each_money_or_state_commit);
    RUN_TEST(test_crash_during_recovery_never_refunds_or_pays_twice);
    RUN_TEST(test_corrupt_storage_is_never_treated_as_empty);
    RUN_TEST(test_rejection_logging_preserves_counter_reserve_for_refund);
    RUN_TEST(test_full_event_storage_retains_room_to_refund_and_return_idle);
    RUN_TEST(test_journal_events_are_contiguous_and_record_each_payout);
    RUN_TEST(test_round_and_bet_identifiers_are_not_reused);
    RUN_TEST(test_seeded_multi_round_accounting_survives_mixed_recovery_paths);
    RUN_TEST(test_edits_affect_only_active_bets_for_one_seat);
    RUN_TEST(test_repeat_persists_across_recovery_empty_and_void_rounds);
    RUN_TEST(test_repeat_late_error_rejects_whole_pattern);
    RUN_TEST(test_removing_offsetting_stake_rechecks_all_outcomes_for_all_seats);
    RUN_TEST(test_repeat_validates_headroom_for_complete_pattern);
    RUN_TEST(test_interrupted_edits_are_atomic_and_refund_only_active_stakes);
    RUN_TEST(test_new_controls_keep_terminal_reserve_at_exact_journal_capacity);
    RUN_TEST(test_round_path_does_not_allocate_on_the_heap);
    return UNITY_END();
}
