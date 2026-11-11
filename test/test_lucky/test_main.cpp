#include <unity.h>

#include "mini_etg/lucky.hpp"
#include "../support/seeded_random.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

using namespace mini_etg;

void setUp() {}
void tearDown() {}

namespace {

void expect(Error expected, Error actual) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(actual));
}

void near(double expected, double actual, double tolerance = 1e-12) {
    TEST_ASSERT_TRUE_MESSAGE(std::fabs(expected - actual) <= tolerance,
                             "Analytical result differs from independent expected value");
}

LuckyConfig enabled() {
    LuckyConfig config{};
    config.enabled = true;
    return config;
}

LuckyDraw one(Number number = Number{0}, std::uint16_t multiplier = 50) {
    LuckyDraw draw{};
    draw.count = 1;
    draw.entries[0] = {number, multiplier};
    return draw;
}

void same_draw(const LuckyDraw& expected, const LuckyDraw& actual) {
    TEST_ASSERT_EQUAL_UINT8(expected.count, actual.count);
    for (std::size_t i = 0; i < expected.entries.size(); ++i) {
        TEST_ASSERT_EQUAL_UINT8(expected.entries[i].number.value, actual.entries[i].number.value);
        TEST_ASSERT_EQUAL_UINT16(expected.entries[i].gross_multiplier, actual.entries[i].gross_multiplier);
    }
}

class ScriptedRandom final : public IRandom {
public:
    std::array<std::uint32_t, 32> words{};
    std::size_t calls{0};

    std::uint32_t next_u32() noexcept override {
        const std::size_t index = calls < words.size() ? calls : words.size() - 1;
        ++calls;
        return words[index];
    }
};

// Produce an accepted 32-bit input mapping to a specific bounded output.
std::uint32_t word_for(std::uint32_t bound, std::uint32_t value) {
    const std::uint64_t threshold = (std::uint64_t{1} << 32) % bound;
    std::uint64_t word = value;
    if (word < threshold) word += bound;
    TEST_ASSERT_TRUE(word >= threshold && word <= std::numeric_limits<std::uint32_t>::max());
    return static_cast<std::uint32_t>(word);
}

AcceptedBet straight(Number number, const LuckyConfig& config, Credits stake = 7) {
    AcceptedBet bet{};
    bet.id = 1;
    bet.request.stake = stake;
    expect(Error::Ok, make_inside_bet(BetKind::Straight, &number, 1, bet.request.selection));
    expect(Error::Ok, base_profit_odds(BetKind::Straight, config, bet.profit_odds));
    return bet;
}

void test_default_configuration_and_all_base_odds() {
    LuckyConfig config{};
    expect(Error::Ok, validate_lucky_config(config));
    constexpr std::array<std::uint8_t, 13> ordinary{{35, 17, 11, 8, 5, 2, 2, 1, 1, 1, 1, 1, 1}};
    for (std::size_t i = 0; i < ordinary.size(); ++i) {
        std::uint8_t odds = 255;
        expect(Error::Ok, base_profit_odds(static_cast<BetKind>(i), config, odds));
        TEST_ASSERT_EQUAL_UINT8(ordinary[i], odds);
    }
    config.enabled = true;
    for (std::size_t i = 0; i < ordinary.size(); ++i) {
        std::uint8_t odds = 255;
        expect(Error::Ok, base_profit_odds(static_cast<BetKind>(i), config, odds));
        TEST_ASSERT_EQUAL_UINT8(i == 0 ? 29 : ordinary[i], odds);
    }
    std::uint8_t unchanged = 123;
    expect(Error::InvalidBetKind, base_profit_odds(static_cast<BetKind>(255), config, unchanged));
    TEST_ASSERT_EQUAL_UINT8(123, unchanged);
}

void test_invalid_configuration_and_uint32_weight_boundary() {
    const auto original = enabled();
    auto config = original;
    config.base_straight_profit_odds = 36;
    expect(Error::InvalidConfig, validate_lucky_config(config));
    config = original;
    config.min_count = 0;
    expect(Error::InvalidConfig, validate_lucky_config(config));
    config = original;
    config.min_count = 5;
    config.max_count = 4;
    expect(Error::InvalidConfig, validate_lucky_config(config));
    config = original;
    config.max_count = 6;
    expect(Error::InvalidConfig, validate_lucky_config(config));
    config = original;
    config.table_count = 0;
    expect(Error::InvalidConfig, validate_lucky_config(config));
    config.table_count = kMaxMultiplierEntries + 1;
    expect(Error::InvalidConfig, validate_lucky_config(config));
    config.table_count = std::numeric_limits<std::size_t>::max();
    expect(Error::InvalidConfig, validate_lucky_config(config));
    config = original;
    config.table[0].weight = 0;
    expect(Error::InvalidConfig, validate_lucky_config(config));
    config = original;
    config.table[0].gross_multiplier = 30;
    expect(Error::InvalidConfig, validate_lucky_config(config));
    config.table[0].gross_multiplier = 0;
    expect(Error::InvalidConfig, validate_lucky_config(config));
    config = original;
    config.table[1].gross_multiplier = config.table[0].gross_multiplier;
    expect(Error::InvalidConfig, validate_lucky_config(config));
    config = original;
    config.table_count = 2;
    config.table[0].weight = std::numeric_limits<std::uint32_t>::max() - 1;
    config.table[1].weight = 1;
    expect(Error::Ok, validate_lucky_config(config));
    config.table[1].weight = 2;
    expect(Error::InvalidConfig, validate_lucky_config(config));
    config.enabled = false;
    expect(Error::InvalidConfig, validate_lucky_config(config));

    // Fields beyond table_count do not participate in the weighted distribution.
    config = original;
    config.table_count = 1;
    config.table[7] = {0, std::numeric_limits<std::uint32_t>::max()};
    expect(Error::Ok, validate_lucky_config(config));
}

void test_disabled_draw_consumes_no_randomness_and_standard_payouts() {
    ScriptedRandom random;
    LuckyConfig config{};
    LuckyDraw draw = one(Number{36}, 500);
    expect(Error::Ok, draw_lucky_numbers(random, config, draw));
    same_draw(LuckyDraw{}, draw);
    TEST_ASSERT_EQUAL_UINT64(0, random.calls);
    expect(Error::Ok, validate_lucky_draw(config, draw));
    const auto bet = straight(Number{0}, config);
    Credits returned = -1;
    expect(Error::Ok, lucky_gross_return(bet, Number{0}, config, draw, returned));
    TEST_ASSERT_EQUAL_INT64(7 * 36, returned);
    expect(Error::Ok, maximum_gross_return(bet, Number{0}, config, returned));
    TEST_ASSERT_EQUAL_INT64(7 * 36, returned);
}

void test_invalid_config_preserves_outputs_and_rng_state() {
    ScriptedRandom random;
    LuckyConfig config = enabled();
    const auto bet = straight(Number{0}, config);
    config.table[0].weight = 0;
    const LuckyDraw before = one(Number{36}, 500);
    LuckyDraw draw = before;
    expect(Error::InvalidConfig, draw_lucky_numbers(random, config, draw));
    same_draw(before, draw);
    TEST_ASSERT_EQUAL_UINT64(0, random.calls);
    std::uint8_t odds = 123;
    expect(Error::InvalidConfig, base_profit_odds(BetKind::Straight, config, odds));
    TEST_ASSERT_EQUAL_UINT8(123, odds);
    Credits returned = 987;
    expect(Error::InvalidConfig, base_gross_return(bet, Number{0}, config, returned));
    expect(Error::InvalidConfig, maximum_gross_return(bet, Number{0}, config, returned));
    expect(Error::InvalidConfig, lucky_gross_return(bet, Number{0}, config, draw, returned));
    TEST_ASSERT_EQUAL_INT64(987, returned);
    LuckyMath math{1, 2, 3, 4};
    expect(Error::InvalidConfig, analyze_lucky(config, math));
    near(1, math.expected_count);
    near(2, math.expected_multiplier);
    near(3, math.straight_rtp);
    near(4, math.straight_return_variance);
}

void test_every_weight_ticket_and_number_boundary() {
    auto config = enabled();
    config.min_count = config.max_count = 1;
    std::array<unsigned, 4> counts{};
    for (std::uint32_t ticket = 0; ticket < 100; ++ticket) {
        for (std::uint32_t number = 0; number < kPocketCount; ++number) {
            ScriptedRandom random;
            random.words.fill(0xffffffffU);
            random.words[0] = word_for(1, 0);
            random.words[1] = word_for(37, number);
            random.words[2] = word_for(100, ticket);
            LuckyDraw draw{};
            expect(Error::Ok, draw_lucky_numbers(random, config, draw));
            expect(Error::Ok, validate_lucky_draw(config, draw));
            TEST_ASSERT_EQUAL_UINT8(1, draw.count);
            TEST_ASSERT_EQUAL_UINT8(number, draw.entries[0].number.value);
            const std::size_t bucket = ticket < 48 ? 0 : ticket < 78 ? 1 : ticket < 98 ? 2 : 3;
            TEST_ASSERT_EQUAL_UINT16(config.table[bucket].gross_multiplier, draw.entries[0].gross_multiplier);
            TEST_ASSERT_EQUAL_UINT64(3, random.calls);
            if (number == 0) ++counts[bucket];
        }
    }
    for (std::size_t i = 0; i < counts.size(); ++i) TEST_ASSERT_EQUAL_UINT32(config.table[i].weight, counts[i]);
}

void test_draw_uses_rejection_sampling_for_each_random_decision() {
    auto config = enabled();
    ScriptedRandom random;
    random.words.fill(0xffffffffU);
    // Count bound5 rejects0, then5 selects count1; pocket bound37 rejects6,
    // then37 selects zero; weight bound100 rejects95, then100 selects50x.
    random.words[0] = 0;
    random.words[1] = 5;
    random.words[2] = 6;
    random.words[3] = 37;
    random.words[4] = 95;
    random.words[5] = 100;
    LuckyDraw draw{};
    expect(Error::Ok, draw_lucky_numbers(random, config, draw));
    same_draw(one(Number{0}, 50), draw);
    TEST_ASSERT_EQUAL_UINT64(6, random.calls);
}

void test_partial_shuffle_produces_each_ordered_distinct_pair() {
    auto config = enabled();
    config.min_count = config.max_count = 2;
    std::array<std::array<bool, kPocketCount>, kPocketCount> seen{};
    for (std::uint32_t first = 0; first < 37; ++first) {
        for (std::uint32_t second_offset = 0; second_offset < 36; ++second_offset) {
            ScriptedRandom random;
            random.words.fill(0xffffffffU);
            random.words[0] = word_for(1, 0);
            random.words[1] = word_for(37, first);
            random.words[2] = word_for(100, 0);
            random.words[3] = word_for(36, second_offset);
            random.words[4] = word_for(100, 99);
            LuckyDraw draw{};
            expect(Error::Ok, draw_lucky_numbers(random, config, draw));
            expect(Error::Ok, validate_lucky_draw(config, draw));
            const auto a = draw.entries[0].number.value;
            const auto b = draw.entries[1].number.value;
            TEST_ASSERT_EQUAL_UINT8(first, a);
            TEST_ASSERT_TRUE(a != b);
            TEST_ASSERT_FALSE(seen[a][b]);
            seen[a][b] = true;
            TEST_ASSERT_EQUAL_UINT16(50, draw.entries[0].gross_multiplier);
            TEST_ASSERT_EQUAL_UINT16(500, draw.entries[1].gross_multiplier);
        }
    }
    for (std::size_t a = 0; a < 37; ++a) {
        for (std::size_t b = 0; b < 37; ++b) TEST_ASSERT_EQUAL_INT(a != b, seen[a][b]);
    }
}

void test_count_range_full_capacity_and_seeded_reproducibility() {
    for (std::uint8_t minimum = 1; minimum <= 5; ++minimum) {
        for (std::uint8_t maximum = minimum; maximum <= 5; ++maximum) {
            auto config = enabled();
            config.min_count = minimum;
            config.max_count = maximum;
            for (std::uint32_t count_offset = 0; count_offset <= maximum - minimum; ++count_offset) {
                ScriptedRandom random;
                random.words.fill(0xffffffffU);
                random.words[0] = word_for(maximum - minimum + 1, count_offset);
                LuckyDraw draw{};
                expect(Error::Ok, draw_lucky_numbers(random, config, draw));
                TEST_ASSERT_EQUAL_UINT8(minimum + count_offset, draw.count);
                expect(Error::Ok, validate_lucky_draw(config, draw));
            }
        }
    }
    mini_etg::test::SeededRandom a{27062026};
    mini_etg::test::SeededRandom b{27062026};
    const auto config = enabled();
    for (unsigned i = 0; i < 2000; ++i) {
        LuckyDraw first{}, second{};
        expect(Error::Ok, draw_lucky_numbers(a, config, first));
        expect(Error::Ok, draw_lucky_numbers(b, config, second));
        same_draw(first, second);
        expect(Error::Ok, validate_lucky_draw(config, first));
    }
}

void test_invalid_draws_are_rejected() {
    const auto config = enabled();
    expect(Error::Ok, validate_lucky_draw(config, one()));
    expect(Error::InvalidConfig, validate_lucky_draw(config, LuckyDraw{}));
    auto draw = one();
    draw.count = 255;
    expect(Error::InvalidConfig, validate_lucky_draw(config, draw));
    draw = one(Number{37});
    expect(Error::InvalidConfig, validate_lucky_draw(config, draw));
    draw = one(Number{0}, 49);
    expect(Error::InvalidConfig, validate_lucky_draw(config, draw));
    draw = one();
    draw.count = 2;
    draw.entries[1] = {Number{0}, 100};
    expect(Error::InvalidConfig, validate_lucky_draw(config, draw));
    draw = one();
    draw.entries[4].number.value = 1;
    expect(Error::InvalidConfig, validate_lucky_draw(config, draw));
    draw = one();
    draw.entries[4].gross_multiplier = 50;
    expect(Error::InvalidConfig, validate_lucky_draw(config, draw));
    auto disabled = config;
    disabled.enabled = false;
    expect(Error::InvalidConfig, validate_lucky_draw(disabled, one()));
    draw = {};
    draw.entries[0].gross_multiplier = 50;
    expect(Error::InvalidConfig, validate_lucky_draw(disabled, draw));

    Credits returned = 123;
    const auto bet = straight(Number{0}, config);
    expect(Error::InvalidConfig, lucky_gross_return(bet, Number{0}, config, draw, returned));
    TEST_ASSERT_EQUAL_INT64(123, returned);
}

void test_straight_payout_replaces_base_with_gross_multiplier() {
    const auto config = enabled();
    for (std::uint8_t chosen = 0; chosen < kPocketCount; ++chosen) {
        const auto bet = straight(Number{chosen}, config);
        for (std::uint8_t result = 0; result < kPocketCount; ++result) {
            Credits returned = -1;
            expect(Error::Ok, base_gross_return(bet, Number{result}, config, returned));
            TEST_ASSERT_EQUAL_INT64(chosen == result ? 7 * 30 : 0, returned);
            expect(Error::Ok, maximum_gross_return(bet, Number{result}, config, returned));
            TEST_ASSERT_EQUAL_INT64(chosen == result ? 7 * 500 : 0, returned);
            for (std::size_t table = 0; table < config.table_count; ++table) {
                const auto multiplier = config.table[table].gross_multiplier;
                const LuckyDraw draw = one(Number{result}, multiplier);
                expect(Error::Ok, lucky_gross_return(bet, Number{result}, config, draw, returned));
                TEST_ASSERT_EQUAL_INT64(chosen == result ? Credits{7} * multiplier : 0, returned);
            }
            const LuckyDraw other = one(Number{static_cast<std::uint8_t>((result + 1) % 37)});
            expect(Error::Ok, lucky_gross_return(bet, Number{result}, config, other, returned));
            TEST_ASSERT_EQUAL_INT64(chosen == result ? 7 * 30 : 0, returned);
        }
    }
}

void test_non_straight_bets_remain_standard_for_every_result() {
    const auto config = enabled();
    std::array<BetSelection, 12> selections{};
    const Number split[]{{0}, {1}};
    const Number street[]{{0}, {1}, {2}};
    const Number corner[]{{0}, {1}, {2}, {3}};
    const Number six_line[]{{1}, {2}, {3}, {4}, {5}, {6}};
    expect(Error::Ok, make_inside_bet(BetKind::Split, split, 2, selections[0]));
    expect(Error::Ok, make_inside_bet(BetKind::Street, street, 3, selections[1]));
    expect(Error::Ok, make_inside_bet(BetKind::Corner, corner, 4, selections[2]));
    expect(Error::Ok, make_inside_bet(BetKind::SixLine, six_line, 6, selections[3]));
    expect(Error::Ok, make_dozen(2, selections[4]));
    expect(Error::Ok, make_column(3, selections[5]));
    for (unsigned i = 0; i < 6; ++i) {
        expect(Error::Ok, make_even_money_bet(static_cast<BetKind>(static_cast<unsigned>(BetKind::Red) + i), selections[6 + i]));
    }
    for (const auto& selection : selections) {
        AcceptedBet bet{};
        bet.request = {0, selection, 7};
        expect(Error::Ok, standard_profit_odds(selection.kind, bet.profit_odds));
        for (std::uint8_t result = 0; result < kPocketCount; ++result) {
            Credits ordinary = -1, actual = -1;
            expect(Error::Ok, gross_return(bet, Number{result}, ordinary));
            expect(Error::Ok, lucky_gross_return(bet, Number{result}, config, one(Number{result}, 500), actual));
            TEST_ASSERT_EQUAL_INT64(ordinary, actual);
            expect(Error::Ok, base_gross_return(bet, Number{result}, config, actual));
            TEST_ASSERT_EQUAL_INT64(ordinary, actual);
            expect(Error::Ok, maximum_gross_return(bet, Number{result}, config, actual));
            TEST_ASSERT_EQUAL_INT64(ordinary, actual);
        }
    }
}

void test_custom_base_and_full_uint16_multiplier_do_not_truncate() {
    auto config = enabled();
    config.base_straight_profit_odds = 0;
    config.table_count = 1;
    config.table[0] = {65535, std::numeric_limits<std::uint32_t>::max()};
    const auto bet = straight(Number{36}, config);
    Credits returned = -1;
    expect(Error::Ok, base_gross_return(bet, Number{36}, config, returned));
    TEST_ASSERT_EQUAL_INT64(7, returned);
    expect(Error::Ok, maximum_gross_return(bet, Number{36}, config, returned));
    TEST_ASSERT_EQUAL_INT64(7 * 65535, returned);
    expect(Error::Ok, lucky_gross_return(bet, Number{36}, config, one(Number{36}, 65535), returned));
    TEST_ASSERT_EQUAL_INT64(7 * 65535, returned);
    ScriptedRandom random;
    random.words.fill(0xffffffffU);
    random.words[0] = word_for(5, 0);
    random.words[1] = word_for(37, 36);
    random.words[2] = word_for(std::numeric_limits<std::uint32_t>::max(), 0);
    LuckyDraw draw{};
    expect(Error::Ok, draw_lucky_numbers(random, config, draw));
    same_draw(one(Number{36}, 65535), draw);
}

void test_payout_validation_overflow_and_unchanged_output() {
    const auto config = enabled();
    const auto draw = one(Number{0}, 500);
    auto bet = straight(Number{0}, config, std::numeric_limits<Credits>::max() / 500);
    Credits returned = -1;
    expect(Error::Ok, lucky_gross_return(bet, Number{0}, config, draw, returned));
    TEST_ASSERT_EQUAL_INT64((std::numeric_limits<Credits>::max() / 500) * 500, returned);
    ++bet.request.stake;
    returned = 123;
    expect(Error::ArithmeticOverflow, lucky_gross_return(bet, Number{0}, config, draw, returned));
    expect(Error::ArithmeticOverflow, maximum_gross_return(bet, Number{0}, config, returned));
    TEST_ASSERT_EQUAL_INT64(123, returned);
    expect(Error::Ok, lucky_gross_return(bet, Number{1}, config, draw, returned));
    TEST_ASSERT_EQUAL_INT64(0, returned);
    bet.request.stake = std::numeric_limits<Credits>::max() / 30 + 1;
    returned = 123;
    expect(Error::ArithmeticOverflow, base_gross_return(bet, Number{0}, config, returned));
    bet.request.stake = 0;
    expect(Error::InvalidAmount, lucky_gross_return(bet, Number{0}, config, draw, returned));
    bet.request.stake = -1;
    expect(Error::InvalidAmount, maximum_gross_return(bet, Number{0}, config, returned));
    bet.request.stake = 1;
    bet.profit_odds = 35;
    expect(Error::InvalidConfig, lucky_gross_return(bet, Number{0}, config, draw, returned));
    bet.profit_odds = 29;
    expect(Error::InvalidNumber, lucky_gross_return(bet, Number{37}, config, draw, returned));
    bet.request.selection.count = 2;
    expect(Error::InvalidGeometry, lucky_gross_return(bet, Number{0}, config, draw, returned));
    TEST_ASSERT_EQUAL_INT64(123, returned);
}

void test_analytical_default_rtp_and_variance() {
    LuckyMath math{};
    expect(Error::Ok, analyze_lucky(enabled(), math));
    near(3, math.expected_count);
    near(104, math.expected_multiplier);
    near(36.0 / 37.0, math.straight_rtp);
    near(80904.0 / 1369.0, math.straight_return_variance);
    TEST_ASSERT_TRUE(std::fabs(math.straight_rtp - 0.9730) < 0.001);
    expect(Error::Ok, analyze_lucky(LuckyConfig{}, math));
    near(0, math.expected_count);
    near(0, math.expected_multiplier);
    near(36.0 / 37.0, math.straight_rtp);
    near(46656.0 / 1369.0, math.straight_return_variance);
}

void test_analytical_custom_table_matches_enumerated_outcomes() {
    auto config = enabled();
    config.base_straight_profit_odds = 9;
    config.min_count = 2;
    config.max_count = 4;
    config.table_count = 2;
    config.table[0] = {20, 1};
    config.table[1] = {80, 3};
    // Enumerate K, whether the fixed number is selected, and four weight tickets.
    // The actual wheel result is zero with 1/37 probability; otherwise return0.
    double first_moment = 0, second_moment = 0;
    for (unsigned count = 2; count <= 4; ++count) {
        for (unsigned location = 0; location < 37; ++location) {
            for (unsigned ticket = 0; ticket < 4; ++ticket) {
                const double returned = location < count ? (ticket == 0 ? 20 : 80) : 10;
                first_moment += returned / (3.0 * 37.0 * 4.0 * 37.0);
                second_moment += returned * returned / (3.0 * 37.0 * 4.0 * 37.0);
            }
        }
    }
    LuckyMath math{};
    expect(Error::Ok, analyze_lucky(config, math));
    near(3, math.expected_count);
    near(65, math.expected_multiplier);
    near(first_moment, math.straight_rtp);
    near(second_moment - first_moment * first_moment, math.straight_return_variance);
}

} // namespace

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_default_configuration_and_all_base_odds);
    RUN_TEST(test_invalid_configuration_and_uint32_weight_boundary);
    RUN_TEST(test_disabled_draw_consumes_no_randomness_and_standard_payouts);
    RUN_TEST(test_invalid_config_preserves_outputs_and_rng_state);
    RUN_TEST(test_every_weight_ticket_and_number_boundary);
    RUN_TEST(test_draw_uses_rejection_sampling_for_each_random_decision);
    RUN_TEST(test_partial_shuffle_produces_each_ordered_distinct_pair);
    RUN_TEST(test_count_range_full_capacity_and_seeded_reproducibility);
    RUN_TEST(test_invalid_draws_are_rejected);
    RUN_TEST(test_straight_payout_replaces_base_with_gross_multiplier);
    RUN_TEST(test_non_straight_bets_remain_standard_for_every_result);
    RUN_TEST(test_custom_base_and_full_uint16_multiplier_do_not_truncate);
    RUN_TEST(test_payout_validation_overflow_and_unchanged_output);
    RUN_TEST(test_analytical_default_rtp_and_variance);
    RUN_TEST(test_analytical_custom_table_matches_enumerated_outcomes);
    return UNITY_END();
}
