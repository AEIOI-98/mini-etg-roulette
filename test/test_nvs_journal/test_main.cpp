#include <unity.h>
#include "nvs_journal.hpp"
#include "mini_etg/engine.hpp"
#include "fake_wheel.hpp"

#include <map>
#include <string>
#include <vector>
#include <algorithm>

using namespace mini_etg;
using namespace mini_etg::firmware;
using mini_etg::test::FakeWheel;

namespace {
class FakeNvs final : public BlobStore {
public:
    enum class Cut { None, Before, After };
    std::map<std::string, std::vector<std::uint8_t>> keys;
    std::string cut_key;
    Cut cut{Cut::None};

    Error get(const char* key, std::uint8_t* out, std::size_t capacity,
              std::size_t& length) noexcept override {
        auto it = keys.find(key);
        if (it == keys.end()) return Error::NoJournal;
        if (it->second.size() > capacity) return Error::StorageCorrupt;
        std::copy(it->second.begin(), it->second.end(), out);
        length = it->second.size();
        return Error::Ok;
    }

    Error put(const char* key, const std::uint8_t* data, std::size_t length) noexcept override {
        const bool interrupt = cut != Cut::None && cut_key == key;
        if (interrupt && cut == Cut::Before) return Error::StorageIoError;
        keys[key].assign(data, data + length);
        return interrupt ? Error::CommitOutcomeUnknown : Error::Ok;
    }
};

void expect(Error expected, Error actual) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(actual));
}

void fund(Engine& engine) {
    std::array<Credits, kSeatCount> credits{};
    credits[0] = 100;
    expect(Error::Ok, engine.initialize({}, credits));
}

void bet(Engine& engine) {
    expect(Error::Ok, engine.open_bets());
    BetRequest request{};
    request.stake = 10;
    Number zero{};
    expect(Error::Ok, make_inside_bet(BetKind::Straight, &zero, 1, request.selection));
    BetId id{};
    expect(Error::Ok, engine.place_bet(request, id));
}

void result(Engine& engine, FakeWheel& wheel) {
    expect(Error::Ok, engine.begin_closing());
    expect(Error::Ok, engine.close_bets());
    expect(Error::Ok, engine.start_spin(10));
    wheel.motion(11);
    wheel.ball(12, 0);
    expect(Error::Ok, engine.tick(12));
}

// Position head before a 16-event rollover and cut on either side of each NVS
// replacement. A restart must choose exactly one complete ledger and history.
void test_cut_before_or_after_archive_and_head() {
    for (const char* key : {"block000", "head"}) {
        for (auto cut : {FakeNvs::Cut::Before, FakeNvs::Cut::After}) {
            FakeNvs flash;
            NvsJournal journal(flash);
            FakeWheel wheel;
            Engine engine(wheel, journal);
            fund(engine); // Two events.
            for (unsigned i = 0; i < 13; ++i) expect(Error::Ok, engine.credit_in(0, 1));
            const auto prior_revision = engine.snapshot().revision;
            TEST_ASSERT_EQUAL_UINT64(15, engine.snapshot().last_sequence);
            flash.cut_key = key;
            flash.cut = cut;
            const bool advanced = flash.cut_key == "head" && cut == FakeNvs::Cut::After;
            expect(advanced ? Error::CommitOutcomeUnknown : Error::StorageIoError,
                   engine.credit_in(0, 10));
            flash.cut = FakeNvs::Cut::None;
            NvsJournal restored(flash);
            Engine reboot(wheel, restored);
            RecoveryReport report{};
            expect(Error::Ok, reboot.recover(report));
            TEST_ASSERT_EQUAL_UINT64(prior_revision + (advanced ? 1 : 0), reboot.snapshot().revision);
            TEST_ASSERT_EQUAL_INT64(advanced ? 123 : 113, reboot.snapshot().seats[0].credits);
            Event e{};
            expect(advanced ? Error::Ok : Error::EventNotFound, restored.read_event(16, e));
            expect(Error::Ok, reboot.credit_in(0, 7));
            const auto last = reboot.snapshot().last_sequence;
            expect(Error::Ok, restored.read_event(last, e));
            TEST_ASSERT_EQUAL_INT64(7, e.amount);
            TEST_ASSERT_EQUAL_UINT64(advanced ? 17 : 16, last);
            for (Sequence seq = 1; seq <= last; ++seq) {
                expect(Error::Ok, restored.read_event(seq, e));
                TEST_ASSERT_EQUAL_UINT64(seq, e.sequence);
            }
        }
    }
}

void test_reboot_before_and_after_result_resolves_exactly_once() {
    for (bool detected : {false, true}) {
        FakeNvs flash;
        FakeWheel wheel;
        NvsJournal journal(flash);
        Engine engine(wheel, journal);
        fund(engine);
        bet(engine);
        if (detected) result(engine, wheel);
        NvsJournal restored(flash);
        Engine reboot(wheel, restored);
        RecoveryReport report{};
        expect(Error::Ok, reboot.recover(report));
        TEST_ASSERT_EQUAL_INT(static_cast<int>(detected ? RecoveryAction::PayoutCompleted : RecoveryAction::Refunded),
                              static_cast<int>(report.action));
        TEST_ASSERT_EQUAL_INT64(detected ? 450 : 100, reboot.snapshot().seats[0].credits);
        TEST_ASSERT_EQUAL_INT64(0, reboot.snapshot().round.escrow);
        if (detected) TEST_ASSERT_EQUAL_INT64(10, reboot.snapshot().last_bets[0].bets[0].stake);
        const auto keys = flash.keys;
        NvsJournal restored_again(flash);
        Engine again(wheel, restored_again);
        expect(Error::Ok, again.recover(report));
        TEST_ASSERT_TRUE(keys == flash.keys);
        TEST_ASSERT_EQUAL_INT64(detected ? 450 : 100, again.snapshot().seats[0].credits);
    }
}

void test_corrupt_or_missing_published_history_never_resets_funding() {
    FakeNvs clean;
    FakeWheel wheel;
    NvsJournal journal(clean);
    Engine engine(wheel, journal);
    fund(engine);
    for (unsigned i = 0; i < 16; ++i) expect(Error::Ok, engine.credit_in(0, 1));
    for (unsigned mode = 0; mode < 5; ++mode) {
        FakeNvs broken = clean;
        if (mode == 0) broken.keys.erase("head");
        if (mode == 1) broken.keys["head"].resize(2);
        if (mode == 2) broken.keys["head"].at(8) ^= 1;
        if (mode == 3) broken.keys.erase("block000");
        if (mode == 4) broken.keys["block000"].at(7) ^= 1;
        NvsJournal reboot(broken);
        Snapshot out{};
        out.revision = 77;
        expect(Error::StorageCorrupt, reboot.load_latest(out));
        TEST_ASSERT_EQUAL_UINT64(77, out.revision);
        Engine again(wheel, reboot);
        expect(Error::StorageCorrupt, again.initialize({}, {}));
    }
}

void test_reserved_slots_refund_a_full_journal() {
    FakeNvs flash;
    FakeWheel wheel;
    NvsJournal journal(flash, 16);
    Engine engine(wheel, journal);
    fund(engine);
    bet(engine);
    // Fill the ordinary-command budget with logged rejections.
    Error error = Error::Ok;
    for (unsigned i = 0; i <= 16 && error != Error::StorageFull; ++i) {
        error = engine.open_bets();
        TEST_ASSERT_TRUE(error == Error::IllegalTransition || error == Error::StorageFull);
    }
    expect(Error::StorageFull, error);
    NvsJournal restored(flash, 16);
    Engine reboot(wheel, restored);
    RecoveryReport report{};
    expect(Error::Ok, reboot.recover(report));
    TEST_ASSERT_EQUAL_INT64(100, reboot.snapshot().seats[0].credits);
    expect(Error::Ok, reboot.acknowledge_fault());
}

void test_many_blocks_with_maximum_settlement_batch() {
    FakeNvs flash;
    FakeWheel wheel;
    NvsJournal storage(flash);
    Engine engine(wheel, storage);
    std::array<Credits, kSeatCount> funding{};
    funding.fill(200);
    expect(Error::Ok, engine.initialize({}, funding));
    expect(Error::Ok, engine.open_bets());
    BetRequest request{};
    request.stake = 1;
    Number zero{};
    expect(Error::Ok, make_inside_bet(BetKind::Straight, &zero, 1, request.selection));
    for (SeatId seat = 0; seat < kSeatCount; ++seat) {
        request.seat = seat;
        for (unsigned i = 0; i < kMaxBetsPerSeat; ++i) {
            BetId id{};
            expect(Error::Ok, engine.place_bet(request, id));
        }
    }
    result(engine, wheel);
    for (const bool interrupt : {true, false}) {
        FakeNvs copy = flash;
        if (interrupt) {
            // Lose power after writing a middle block of the 128-bet payout.
            copy.cut_key = "block011";
            copy.cut = FakeNvs::Cut::After;
        }
        NvsJournal journal(copy);
        Engine reboot(wheel, journal);
        RecoveryReport report{};
        if (interrupt) {
            expect(Error::StorageIoError, reboot.recover(report));
            copy.cut = FakeNvs::Cut::None;
        }
        expect(Error::Ok, reboot.recover(report));
        TEST_ASSERT_EQUAL_UINT64(kMaxBets, reboot.snapshot().round.bet_count);
        const auto keys = copy.keys;
        NvsJournal fresh(copy);
        Engine again(wheel, fresh);
        expect(Error::Ok, again.recover(report));
        TEST_ASSERT_TRUE(keys == copy.keys);
        for (auto& pattern : again.snapshot().last_bets)
            TEST_ASSERT_EQUAL_UINT8(kMaxBetsPerSeat, pattern.count);
        for (Sequence seq = 1; seq <= again.snapshot().last_sequence; ++seq) {
            Event e{};
            expect(Error::Ok, fresh.read_event(seq, e));
            TEST_ASSERT_EQUAL_UINT64(seq, e.sequence);
        }
    }
}
} // namespace

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_cut_before_or_after_archive_and_head);
    RUN_TEST(test_reboot_before_and_after_result_resolves_exactly_once);
    RUN_TEST(test_corrupt_or_missing_published_history_never_resets_funding);
    RUN_TEST(test_reserved_slots_refund_a_full_journal);
    RUN_TEST(test_many_blocks_with_maximum_settlement_batch);
    return UNITY_END();
}
