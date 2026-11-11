#include <unity.h>

#include "mini_etg/bet.hpp"
#include "mini_etg/random.hpp"
#include "mini_etg/wheel.hpp"
#include "../support/seeded_random.hpp"

#include <array>
#include <cstdint>
#include <initializer_list>
#include <limits>

using namespace mini_etg;

void setUp() {}
void tearDown() {}

namespace {

void assert_error(Error expected, Error actual) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(actual));
}

void assert_selection(const BetSelection& expected, const BetSelection& actual) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(expected.kind), static_cast<int>(actual.kind));
    TEST_ASSERT_EQUAL_UINT8(expected.count, actual.count);
    for (std::size_t i = 0; i < expected.numbers.size(); ++i) {
        TEST_ASSERT_EQUAL_UINT8(expected.numbers[i].value, actual.numbers[i].value);
    }
}

using Arithmetic = Error (*)(Credits, Credits, Credits&) noexcept;

void assert_math(Arithmetic operation, Credits a, Credits b,
                 Error expected, Credits value = 0) {
    Credits out = 123;
    assert_error(expected, operation(a, b, out));
    TEST_ASSERT_EQUAL_INT64(expected == Error::Ok ? value : 123, out);
}

void test_checked_arithmetic_boundaries() {
    const Credits minimum = std::numeric_limits<Credits>::min();
    const Credits maximum = std::numeric_limits<Credits>::max();
    assert_math(checked_add, maximum, 1, Error::ArithmeticOverflow);
    assert_math(checked_add, minimum, -1, Error::ArithmeticOverflow);
    assert_math(checked_add, maximum, maximum, Error::ArithmeticOverflow);
    assert_math(checked_add, minimum, minimum, Error::ArithmeticOverflow);
    assert_math(checked_add, minimum, maximum, Error::Ok, -1);
    assert_math(checked_add, maximum - 1, 1, Error::Ok, maximum);
    assert_math(checked_add, minimum + 1, -1, Error::Ok, minimum);
    assert_math(checked_add, minimum, 0, Error::Ok, minimum);

    assert_math(checked_subtract, minimum, 1, Error::ArithmeticOverflow);
    assert_math(checked_subtract, maximum, -1, Error::ArithmeticOverflow);
    assert_math(checked_subtract, 0, minimum, Error::ArithmeticOverflow);
    assert_math(checked_subtract, maximum, minimum, Error::ArithmeticOverflow);
    assert_math(checked_subtract, minimum, maximum, Error::ArithmeticOverflow);
    assert_math(checked_subtract, minimum, minimum, Error::Ok, 0);
    assert_math(checked_subtract, maximum, maximum, Error::Ok, 0);
    assert_math(checked_subtract, -1, minimum, Error::Ok, maximum);
    assert_math(checked_subtract, minimum, -1, Error::Ok, minimum + 1);
    assert_math(checked_subtract, 0, maximum, Error::Ok, -maximum);

    assert_math(checked_multiply, minimum, -1, Error::ArithmeticOverflow);
    assert_math(checked_multiply, -1, minimum, Error::ArithmeticOverflow);
    assert_math(checked_multiply, maximum, 2, Error::ArithmeticOverflow);
    assert_math(checked_multiply, 2, maximum, Error::ArithmeticOverflow);
    assert_math(checked_multiply, minimum, 2, Error::ArithmeticOverflow);
    assert_math(checked_multiply, 2, minimum, Error::ArithmeticOverflow);
    assert_math(checked_multiply, minimum, minimum, Error::ArithmeticOverflow);
    assert_math(checked_multiply, -3037000500LL, -3037000500LL, Error::ArithmeticOverflow);
    assert_math(checked_multiply, 3037000500LL, 3037000500LL, Error::ArithmeticOverflow);
    assert_math(checked_multiply, -3037000499LL, -3037000499LL,
                Error::Ok, 9223372030926249001LL);
    assert_math(checked_multiply, maximum, -1, Error::Ok, -maximum);
    assert_math(checked_multiply, minimum, 1, Error::Ok, minimum);
    assert_math(checked_multiply, minimum, 0, Error::Ok, 0);
    assert_math(checked_multiply, 0, minimum, Error::Ok, 0);
    assert_math(checked_multiply, minimum / 2, 2, Error::Ok, minimum);
    assert_math(checked_multiply, maximum / 2, 2, Error::Ok, maximum - 1);

    // Ordinary values exercise every sign combination without overflowing int.
    for (int a = -64; a <= 64; ++a) {
        for (int b = -64; b <= 64; ++b) {
            assert_math(checked_add, a, b, Error::Ok, a + b);
            assert_math(checked_subtract, a, b, Error::Ok, a - b);
            assert_math(checked_multiply, a, b, Error::Ok, a * b);
        }
    }
}

class ScriptedRandom final : public IRandom {
public:
    std::array<std::uint32_t, 8> words{};
    std::size_t calls{0};

    std::uint32_t next_u32() noexcept override {
        // The last value repeats if a broken implementation draws too many times.
        const std::size_t position = calls < words.size() ? calls : words.size() - 1;
        ++calls;
        return words[position];
    }
};

void test_rejection_sampling_boundaries() {
    ScriptedRandom random;
    random.words = {{0, 6, 7, 36, 37, 0xffffffffU, 0xffffffffU, 0xffffffffU}};
    std::uint32_t out = 123;
    assert_error(Error::InvalidBound, uniform_below(random, 0, out));
    TEST_ASSERT_EQUAL_UINT32(123, out);
    TEST_ASSERT_EQUAL_UINT32(0, random.calls);

    // 2^32 = 37 * 116080197 + 7, so words 0 through 6 must be rejected.
    assert_error(Error::Ok, uniform_below(random, 37, out));
    TEST_ASSERT_EQUAL_UINT32(7, out);
    TEST_ASSERT_EQUAL_UINT32(3, random.calls);
    assert_error(Error::Ok, uniform_below(random, 37, out));
    TEST_ASSERT_EQUAL_UINT32(36, out);
    assert_error(Error::Ok, uniform_below(random, 37, out));
    TEST_ASSERT_EQUAL_UINT32(0, out);
    assert_error(Error::Ok, uniform_below(random, 37, out));
    TEST_ASSERT_EQUAL_UINT32(6, out);

    random.calls = 0;
    random.words = {{0, 1, 0xffffffffU, 0xffffffffU, 0xffffffffU, 0xffffffffU,
                     0xffffffffU, 0xffffffffU}};
    assert_error(Error::Ok, uniform_below(random, 0xffffffffU, out));
    TEST_ASSERT_EQUAL_UINT32(1, out);
    TEST_ASSERT_EQUAL_UINT32(2, random.calls);
    assert_error(Error::Ok, uniform_below(random, 0xffffffffU, out));
    TEST_ASSERT_EQUAL_UINT32(0, out);

    random.calls = 0;
    random.words = {{0, 0xffffffffU, 0xffffffffU, 0xffffffffU, 0xffffffffU,
                     0xffffffffU, 0xffffffffU, 0xffffffffU}};
    assert_error(Error::Ok, uniform_below(random, 1, out));
    TEST_ASSERT_EQUAL_UINT32(0, out);
    TEST_ASSERT_EQUAL_UINT32(1, random.calls);
    assert_error(Error::Ok, uniform_below(random, 16, out));
    TEST_ASSERT_EQUAL_UINT32(15, out);

    random.calls = 0;
    random.words = {{0, 0x7ffffffeU, 0x7fffffffU, 0xffffffffU, 0xffffffffU,
                     0xffffffffU, 0xffffffffU, 0xffffffffU}};
    assert_error(Error::Ok, uniform_below(random, 0x80000001U, out));
    TEST_ASSERT_EQUAL_UINT32(0x7fffffffU, out);
    TEST_ASSERT_EQUAL_UINT32(3, random.calls);
    assert_error(Error::Ok, uniform_below(random, 0x80000001U, out));
    TEST_ASSERT_EQUAL_UINT32(0x7ffffffeU, out);
}

void test_rejection_sampling_many_bounds() {
    constexpr std::uint64_t word_count = std::uint64_t{1} << 32;
    for (std::uint32_t bound = 1; bound <= 128; ++bound) {
        const auto threshold = static_cast<std::uint32_t>(word_count % bound);
        ScriptedRandom random;
        random.words.fill(0xffffffffU);
        random.words[0] = threshold == 0 ? 0 : threshold - 1;
        random.words[1] = threshold;
        std::uint32_t out = 999;
        assert_error(Error::Ok, uniform_below(random, bound, out));
        TEST_ASSERT_EQUAL_UINT32(threshold % bound, out);
        TEST_ASSERT_EQUAL_UINT32(threshold == 0 ? 1 : 2, random.calls);
        TEST_ASSERT_EQUAL_UINT64(0, (word_count - threshold) % bound);
    }
}

void test_seeded_random_is_reproducible() {
    mini_etg::test::SeededRandom reference(5489);
    constexpr std::array<std::uint32_t, 5> expected = {{
        3499211612U, 581869302U, 3890346734U, 3586334585U, 545404204U
    }};
    for (const auto word : expected) {
        TEST_ASSERT_EQUAL_UINT32(word, reference.next_u32());
    }

    mini_etg::test::SeededRandom first(12345);
    mini_etg::test::SeededRandom second(12345);
    constexpr std::array<std::uint32_t, 6> bounds = {{1, 2, 10, 37, 0x80000001U, 0xffffffffU}};
    for (std::size_t i = 0; i < 2000; ++i) {
        const auto bound = bounds[i % bounds.size()];
        std::uint32_t a = 0;
        std::uint32_t b = 0;
        assert_error(Error::Ok, uniform_below(first, bound, a));
        assert_error(Error::Ok, uniform_below(second, bound, b));
        TEST_ASSERT_EQUAL_UINT32(a, b);
        TEST_ASSERT_TRUE(a < bound);
    }
}

constexpr std::array<std::uint8_t, 18> reference_red = {{
    1, 3, 5, 7, 9, 12, 14, 16, 18, 19, 21, 23, 25, 27, 30, 32, 34, 36
}};

bool is_reference_red(std::uint8_t number) {
    for (const auto red : reference_red) {
        if (number == red) {
            return true;
        }
    }
    return false;
}

void test_wheel_order_unique_numbers_and_colours() {
    constexpr std::array<std::uint8_t, 37> expected_order = {{
        0, 32, 15, 19, 4, 21, 2, 25, 17, 34, 6, 27, 13, 36, 11, 30, 8, 23,
        10, 5, 24, 16, 33, 1, 20, 14, 31, 9, 22, 18, 29, 7, 28, 12, 35, 3, 26
    }};
    std::array<bool, 37> seen{};
    int red = 0;
    int black = 0;
    int green = 0;
    for (std::uint8_t i = 0; i < expected_order.size(); ++i) {
        Number number{255};
        assert_error(Error::Ok, number_at(PocketIndex{i}, number));
        TEST_ASSERT_EQUAL_UINT8(expected_order[i], number.value);
        TEST_ASSERT_EQUAL_UINT8(expected_order[i], kEuropeanWheelOrder[i].value);
        TEST_ASSERT_FALSE(seen[number.value]);
        seen[number.value] = true;
        Colour colour{};
        assert_error(Error::Ok, colour_of(number, colour));
        const Colour expected_colour = number.value == 0 ? Colour::Green
            : is_reference_red(number.value) ? Colour::Red : Colour::Black;
        TEST_ASSERT_EQUAL_INT(static_cast<int>(expected_colour), static_cast<int>(colour));
        red += colour == Colour::Red;
        black += colour == Colour::Black;
        green += colour == Colour::Green;
    }
    for (const bool present : seen) {
        TEST_ASSERT_TRUE(present);
    }
    TEST_ASSERT_EQUAL_INT(18, red);
    TEST_ASSERT_EQUAL_INT(18, black);
    TEST_ASSERT_EQUAL_INT(1, green);

    Number unchanged{12};
    Colour colour = Colour::Red;
    assert_error(Error::InvalidNumber, number_at(PocketIndex{37}, unchanged));
    assert_error(Error::InvalidNumber, number_at(PocketIndex{255}, unchanged));
    TEST_ASSERT_EQUAL_UINT8(12, unchanged.value);
    assert_error(Error::InvalidNumber, colour_of(Number{37}, colour));
    assert_error(Error::InvalidNumber, colour_of(Number{255}, colour));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Colour::Red), static_cast<int>(colour));
}

void verify_all_outcomes(const BetSelection& selection, std::uint8_t profit_odds) {
    AcceptedBet bet{};
    bet.id = 1;
    bet.request.selection = selection;
    bet.request.stake = 7;
    bet.profit_odds = profit_odds;
    int winning_outcomes = 0;
    for (std::uint8_t result = 0; result < 37; ++result) {
        bool expected = false;
        for (std::size_t i = 0; i < selection.count; ++i) {
            expected = expected || selection.numbers[i].value == result;
        }
        bool winning = !expected;
        assert_error(Error::Ok, covers(selection, Number{result}, winning));
        TEST_ASSERT_EQUAL_INT(expected, winning);
        Credits returned = -1;
        assert_error(Error::Ok, gross_return(bet, Number{result}, returned));
        TEST_ASSERT_EQUAL_INT64(expected ? Credits{7} * (profit_odds + 1) : 0, returned);
        winning_outcomes += winning;
    }
    TEST_ASSERT_EQUAL_INT(selection.count, winning_outcomes);
    for (std::size_t i = selection.count; i < selection.numbers.size(); ++i) {
        TEST_ASSERT_EQUAL_UINT8(0, selection.numbers[i].value);
    }
}

void test_every_straight_and_split() {
    for (std::uint8_t number = 0; number < 37; ++number) {
        BetSelection bet{};
        Number input{number};
        assert_error(Error::Ok, make_inside_bet(BetKind::Straight, &input, 1, bet));
        TEST_ASSERT_EQUAL_UINT8(number, bet.numbers[0].value);
        verify_all_outcomes(bet, 35);
    }

    int split_count = 0;
    for (std::uint8_t a = 0; a < 37; ++a) {
        for (std::uint8_t b = 0; b < 37; ++b) {
            bool valid = false;
            if (a == 0 || b == 0) {
                valid = (a == 0 && b >= 1 && b <= 3) || (b == 0 && a >= 1 && a <= 3);
            } else {
                int rows = (a - 1) / 3 - (b - 1) / 3;
                int columns = (a - 1) % 3 - (b - 1) % 3;
                if (rows < 0) rows = -rows;
                if (columns < 0) columns = -columns;
                valid = rows + columns == 1;
            }
            const Number numbers[] = {{a}, {b}};
            BetSelection bet{};
            assert_error(valid ? Error::Ok : Error::InvalidGeometry,
                         make_inside_bet(BetKind::Split, numbers, 2, bet));
            if (valid && a < b) {
                ++split_count;
                TEST_ASSERT_EQUAL_UINT8(a, bet.numbers[0].value);
                TEST_ASSERT_EQUAL_UINT8(b, bet.numbers[1].value);
                verify_all_outcomes(bet, 17);
            }
        }
    }
    TEST_ASSERT_EQUAL_INT(60, split_count);
}

constexpr std::uint8_t reference_streets[][3] = {
    {0, 1, 2}, {0, 2, 3}, {1, 2, 3}, {4, 5, 6}, {7, 8, 9}, {10, 11, 12},
    {13, 14, 15}, {16, 17, 18}, {19, 20, 21}, {22, 23, 24}, {25, 26, 27},
    {28, 29, 30}, {31, 32, 33}, {34, 35, 36}
};

constexpr std::uint8_t reference_corners[][4] = {
    {0, 1, 2, 3}, {1, 2, 4, 5}, {2, 3, 5, 6}, {4, 5, 7, 8}, {5, 6, 8, 9},
    {7, 8, 10, 11}, {8, 9, 11, 12}, {10, 11, 13, 14}, {11, 12, 14, 15},
    {13, 14, 16, 17}, {14, 15, 17, 18}, {16, 17, 19, 20}, {17, 18, 20, 21},
    {19, 20, 22, 23}, {20, 21, 23, 24}, {22, 23, 25, 26}, {23, 24, 26, 27},
    {25, 26, 28, 29}, {26, 27, 29, 30}, {28, 29, 31, 32}, {29, 30, 32, 33},
    {31, 32, 34, 35}, {32, 33, 35, 36}
};

void test_every_three_number_set_is_a_street_or_rejected() {
    int street_count = 0;
    for (std::uint8_t a = 0; a < 37; ++a) {
        for (std::uint8_t b = a + 1; b < 37; ++b) {
            for (std::uint8_t c = b + 1; c < 37; ++c) {
                bool valid = false;
                for (const auto& street : reference_streets) {
                    valid = valid || (a == street[0] && b == street[1] && c == street[2]);
                }
                const Number numbers[] = {{c}, {b}, {a}};
                BetSelection bet{};
                assert_error(valid ? Error::Ok : Error::InvalidGeometry,
                             make_inside_bet(BetKind::Street, numbers, 3, bet));
                if (valid) {
                    ++street_count;
                    TEST_ASSERT_EQUAL_UINT8(a, bet.numbers[0].value);
                    TEST_ASSERT_EQUAL_UINT8(b, bet.numbers[1].value);
                    TEST_ASSERT_EQUAL_UINT8(c, bet.numbers[2].value);
                    verify_all_outcomes(bet, 11);
                }
            }
        }
    }
    TEST_ASSERT_EQUAL_INT(14, street_count);
}

void test_every_four_number_set_is_a_corner_or_rejected() {
    int corner_count = 0;
    for (std::uint8_t a = 0; a < 37; ++a) {
        for (std::uint8_t b = a + 1; b < 37; ++b) {
            for (std::uint8_t c = b + 1; c < 37; ++c) {
                for (std::uint8_t d = c + 1; d < 37; ++d) {
                    bool valid = false;
                    for (const auto& corner : reference_corners) {
                        valid = valid || (a == corner[0] && b == corner[1]
                                          && c == corner[2] && d == corner[3]);
                    }
                    const Number numbers[] = {{c}, {a}, {d}, {b}};
                    BetSelection bet{};
                    assert_error(valid ? Error::Ok : Error::InvalidGeometry,
                                 make_inside_bet(BetKind::Corner, numbers, 4, bet));
                    if (valid) {
                        ++corner_count;
                        TEST_ASSERT_EQUAL_UINT8(a, bet.numbers[0].value);
                        TEST_ASSERT_EQUAL_UINT8(b, bet.numbers[1].value);
                        TEST_ASSERT_EQUAL_UINT8(c, bet.numbers[2].value);
                        TEST_ASSERT_EQUAL_UINT8(d, bet.numbers[3].value);
                        verify_all_outcomes(bet, 8);
                    }
                }
            }
        }
    }
    TEST_ASSERT_EQUAL_INT(23, corner_count);
}

void test_six_lines_and_invalid_boundaries() {
    int six_count = 0;
    for (std::uint8_t start = 0; start <= 31; ++start) {
        Number numbers[6]{};
        for (std::uint8_t i = 0; i < 6; ++i) {
            numbers[5 - i] = Number{static_cast<std::uint8_t>(start + i)};
        }
        const bool valid = start >= 1 && start % 3 == 1;
        BetSelection bet{};
        assert_error(valid ? Error::Ok : Error::InvalidGeometry,
                     make_inside_bet(BetKind::SixLine, numbers, 6, bet));
        if (valid) {
            ++six_count;
            for (std::size_t i = 0; i < 6; ++i) {
                TEST_ASSERT_EQUAL_UINT8(start + i, bet.numbers[i].value);
            }
            verify_all_outcomes(bet, 5);
        }
    }
    TEST_ASSERT_EQUAL_INT(11, six_count);
    const Number non_adjacent[] = {{1}, {2}, {3}, {7}, {8}, {9}};
    BetSelection bet{};
    assert_error(Error::InvalidGeometry, make_inside_bet(BetKind::SixLine, non_adjacent, 6, bet));
    const Number duplicate[] = {{1}, {2}, {3}, {4}, {5}, {5}};
    assert_error(Error::InvalidGeometry, make_inside_bet(BetKind::SixLine, duplicate, 6, bet));
}

void test_dozens_columns_and_even_money_sets() {
    for (std::uint8_t group = 1; group <= 3; ++group) {
        BetSelection dozen{};
        BetSelection column{};
        assert_error(Error::Ok, make_dozen(group, dozen));
        assert_error(Error::Ok, make_column(group, column));
        for (std::size_t i = 0; i < 12; ++i) {
            TEST_ASSERT_EQUAL_UINT8((group - 1) * 12 + i + 1, dozen.numbers[i].value);
            TEST_ASSERT_EQUAL_UINT8(group + 3 * i, column.numbers[i].value);
        }
        verify_all_outcomes(dozen, 2);
        verify_all_outcomes(column, 2);
    }

    constexpr std::array<BetKind, 6> kinds = {{
        BetKind::Red, BetKind::Black, BetKind::Odd, BetKind::Even, BetKind::Low, BetKind::High
    }};
    for (const auto kind : kinds) {
        BetSelection bet{};
        assert_error(Error::Ok, make_even_money_bet(kind, bet));
        TEST_ASSERT_EQUAL_UINT8(18, bet.count);
        for (std::uint8_t number = 0; number < 37; ++number) {
            bool expected = false;
            if (number != 0) {
                switch (kind) {
                case BetKind::Red: expected = is_reference_red(number); break;
                case BetKind::Black: expected = !is_reference_red(number); break;
                case BetKind::Odd: expected = number % 2 == 1; break;
                case BetKind::Even: expected = number % 2 == 0; break;
                case BetKind::Low: expected = number <= 18; break;
                case BetKind::High: expected = number >= 19; break;
                default: TEST_FAIL_MESSAGE("unexpected test kind"); break;
                }
            }
            bool actual = !expected;
            assert_error(Error::Ok, covers(bet, Number{number}, actual));
            TEST_ASSERT_EQUAL_INT(expected, actual);
        }
        verify_all_outcomes(bet, 1);
    }
}

void test_invalid_selections_preserve_output() {
    BetSelection sentinel{};
    assert_error(Error::Ok, make_even_money_bet(BetKind::Red, sentinel));
    BetSelection out = sentinel;
    BetSelection bad{};
    assert_error(Error::InvalidGeometry, canonicalize(bad, out));
    assert_selection(sentinel, out);

    bad.count = 255;
    assert_error(Error::InvalidGeometry, canonicalize(bad, out));
    assert_selection(sentinel, out);
    bad.count = 1;
    bad.numbers[0] = Number{37};
    assert_error(Error::InvalidNumber, canonicalize(bad, out));
    assert_selection(sentinel, out);
    bad.kind = static_cast<BetKind>(255);
    assert_error(Error::InvalidBetKind, canonicalize(bad, out));
    assert_selection(sentinel, out);

    const Number pair[] = {{1}, {2}};
    assert_error(Error::InvalidGeometry, make_inside_bet(BetKind::Split, nullptr, 2, out));
    assert_selection(sentinel, out);
    assert_error(Error::InvalidGeometry, make_inside_bet(BetKind::Split, pair, 0, out));
    assert_error(Error::InvalidGeometry, make_inside_bet(BetKind::Split, pair, 19, out));
    assert_error(Error::InvalidBetKind, make_inside_bet(BetKind::Dozen, pair, 2, out));
    assert_error(Error::InvalidBetKind, make_inside_bet(static_cast<BetKind>(255), pair, 2, out));
    assert_selection(sentinel, out);

    for (const auto group : {0, 4, 255}) {
        assert_error(Error::InvalidGeometry, make_dozen(static_cast<std::uint8_t>(group), out));
        assert_error(Error::InvalidGeometry, make_column(static_cast<std::uint8_t>(group), out));
        assert_selection(sentinel, out);
    }
    assert_error(Error::InvalidBetKind, make_even_money_bet(BetKind::Straight, out));
    assert_selection(sentinel, out);

    // Valid cardinality alone does not make a dozen, column, or even-money bet.
    assert_error(Error::Ok, make_dozen(1, bad));
    bad.numbers[11] = Number{13};
    assert_error(Error::InvalidGeometry, canonicalize(bad, out));
    assert_error(Error::Ok, make_column(1, bad));
    bad.numbers[0] = Number{0};
    assert_error(Error::InvalidGeometry, canonicalize(bad, out));
    assert_error(Error::Ok, make_even_money_bet(BetKind::Even, bad));
    bad.numbers[0] = Number{0};
    assert_error(Error::InvalidGeometry, canonicalize(bad, out));
    assert_error(Error::Ok, make_even_money_bet(BetKind::Red, bad));
    bad.numbers[0] = Number{2};
    assert_error(Error::InvalidGeometry, canonicalize(bad, out));
    assert_selection(sentinel, out);
}

void test_canonicalization_aliasing_and_unused_slots() {
    BetSelection selection{};
    selection.kind = BetKind::Split;
    selection.count = 2;
    selection.numbers.fill(Number{255});
    selection.numbers[0] = Number{2};
    selection.numbers[1] = Number{1};
    assert_error(Error::Ok, canonicalize(selection, selection));
    TEST_ASSERT_EQUAL_UINT8(1, selection.numbers[0].value);
    TEST_ASSERT_EQUAL_UINT8(2, selection.numbers[1].value);
    for (std::size_t i = 2; i < selection.numbers.size(); ++i) {
        TEST_ASSERT_EQUAL_UINT8(0, selection.numbers[i].value);
    }

    selection.numbers[1] = Number{1};
    const BetSelection before = selection;
    assert_error(Error::InvalidGeometry, canonicalize(selection, selection));
    assert_selection(before, selection);
}

void test_payout_validation_and_overflow() {
    constexpr std::array<std::uint8_t, 13> odds = {{35, 17, 11, 8, 5, 2, 2, 1, 1, 1, 1, 1, 1}};
    for (std::size_t i = 0; i < odds.size(); ++i) {
        std::uint8_t out = 255;
        assert_error(Error::Ok, standard_profit_odds(static_cast<BetKind>(i), out));
        TEST_ASSERT_EQUAL_UINT8(odds[i], out);
    }
    std::uint8_t unchanged_odds = 99;
    assert_error(Error::InvalidBetKind, standard_profit_odds(static_cast<BetKind>(255), unchanged_odds));
    TEST_ASSERT_EQUAL_UINT8(99, unchanged_odds);

    AcceptedBet bet{};
    Number zero{0};
    assert_error(Error::Ok, make_inside_bet(BetKind::Straight, &zero, 1, bet.request.selection));
    bet.profit_odds = 35;
    const Credits maximum = std::numeric_limits<Credits>::max();
    bet.request.stake = maximum / 36;
    Credits returned = -1;
    assert_error(Error::Ok, gross_return(bet, zero, returned));
    TEST_ASSERT_EQUAL_INT64((maximum / 36) * 36, returned);
    bet.request.stake += 1;
    returned = 123;
    assert_error(Error::ArithmeticOverflow, gross_return(bet, zero, returned));
    TEST_ASSERT_EQUAL_INT64(123, returned);
    assert_error(Error::Ok, gross_return(bet, Number{1}, returned));
    TEST_ASSERT_EQUAL_INT64(0, returned);

    returned = 123;
    bet.request.stake = 0;
    assert_error(Error::InvalidAmount, gross_return(bet, zero, returned));
    bet.request.stake = -1;
    assert_error(Error::InvalidAmount, gross_return(bet, zero, returned));
    bet.request.stake = 1;
    bet.profit_odds = 255;
    assert_error(Error::InvalidConfig, gross_return(bet, zero, returned));
    bet.profit_odds = 35;
    assert_error(Error::InvalidNumber, gross_return(bet, Number{37}, returned));
    bet.request.selection.count = 2;
    assert_error(Error::InvalidGeometry, gross_return(bet, zero, returned));
    TEST_ASSERT_EQUAL_INT64(123, returned);

    bool covered = true;
    assert_error(Error::InvalidGeometry, covers(bet.request.selection, zero, covered));
    assert_error(Error::InvalidNumber, covers(bet.request.selection, Number{255}, covered));
    TEST_ASSERT_TRUE(covered);
}

} // namespace

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_checked_arithmetic_boundaries);
    RUN_TEST(test_rejection_sampling_boundaries);
    RUN_TEST(test_rejection_sampling_many_bounds);
    RUN_TEST(test_seeded_random_is_reproducible);
    RUN_TEST(test_wheel_order_unique_numbers_and_colours);
    RUN_TEST(test_every_straight_and_split);
    RUN_TEST(test_every_three_number_set_is_a_street_or_rejected);
    RUN_TEST(test_every_four_number_set_is_a_corner_or_rejected);
    RUN_TEST(test_six_lines_and_invalid_boundaries);
    RUN_TEST(test_dozens_columns_and_even_money_sets);
    RUN_TEST(test_invalid_selections_preserve_output);
    RUN_TEST(test_canonicalization_aliasing_and_unused_slots);
    RUN_TEST(test_payout_validation_and_overflow);
    return UNITY_END();
}
