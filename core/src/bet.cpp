#include "mini_etg/bet.hpp"
#include "mini_etg/wheel.hpp"

namespace mini_etg {
namespace {

std::uint8_t expected_count(BetKind kind) noexcept {
    switch (kind) {
    case BetKind::Straight: return 1;
    case BetKind::Split: return 2;
    case BetKind::Street: return 3;
    case BetKind::Corner: return 4;
    case BetKind::SixLine: return 6;
    case BetKind::Dozen:
    case BetKind::Column: return 12;
    case BetKind::Red:
    case BetKind::Black:
    case BetKind::Odd:
    case BetKind::Even:
    case BetKind::Low:
    case BetKind::High: return 18;
    }
    return 0;
}

bool even_money_match(BetKind kind, Number number) noexcept {
    // Zero is not even/low in roulette, despite its mathematical properties.
    if (number.value == 0) {
        return false;
    }
    Colour colour{};
    switch (kind) {
    case BetKind::Red:
    case BetKind::Black:
        if (colour_of(number, colour) != Error::Ok) {
            return false;
        }
        return colour == (kind == BetKind::Red ? Colour::Red : Colour::Black);
    case BetKind::Odd: return number.value % 2 == 1;
    case BetKind::Even: return number.value % 2 == 0;
    case BetKind::Low: return number.value <= 18;
    case BetKind::High: return number.value >= 19;
    default: return false;
    }
}

bool has_geometry(const BetSelection& bet) noexcept {
    const auto first = bet.numbers[0].value;
    switch (bet.kind) {
    case BetKind::Straight:
        return true;
    case BetKind::Split: {
        const auto second = bet.numbers[1].value;
        if (first == 0) {
            return second >= 1 && second <= 3;
        }
        const bool same_row = (first - 1) / 3 == (second - 1) / 3;
        return (same_row && second == first + 1) || second == first + 3;
    }
    case BetKind::Street:
        if (first == 0) {
            return (bet.numbers[1].value == 1 && bet.numbers[2].value == 2)
                || (bet.numbers[1].value == 2 && bet.numbers[2].value == 3);
        }
        return first % 3 == 1 && bet.numbers[1].value == first + 1
            && bet.numbers[2].value == first + 2;
    case BetKind::Corner:
        if (first == 0) {
            return bet.numbers[1].value == 1 && bet.numbers[2].value == 2
                && bet.numbers[3].value == 3;
        }
        return first % 3 != 0 && bet.numbers[1].value == first + 1
            && bet.numbers[2].value == first + 3 && bet.numbers[3].value == first + 4;
    case BetKind::SixLine:
        if (first == 0 || first % 3 != 1) {
            return false;
        }
        for (std::size_t i = 1; i < bet.count; ++i) {
            if (bet.numbers[i].value != first + i) {
                return false;
            }
        }
        return true;
    case BetKind::Dozen:
        if (first != 1 && first != 13 && first != 25) {
            return false;
        }
        for (std::size_t i = 1; i < bet.count; ++i) {
            if (bet.numbers[i].value != first + i) {
                return false;
            }
        }
        return true;
    case BetKind::Column:
        if (first < 1 || first > 3) {
            return false;
        }
        for (std::size_t i = 1; i < bet.count; ++i) {
            if (bet.numbers[i].value != first + 3 * i) {
                return false;
            }
        }
        return true;
    case BetKind::Red:
    case BetKind::Black:
    case BetKind::Odd:
    case BetKind::Even:
    case BetKind::Low:
    case BetKind::High:
        for (std::size_t i = 0; i < bet.count; ++i) {
            if (!even_money_match(bet.kind, bet.numbers[i])) {
                return false;
            }
        }
        return true;
    }
    return false;
}

} // namespace

Error canonicalize(const BetSelection& input, BetSelection& out) noexcept {
    const std::uint8_t needed = expected_count(input.kind);
    if (needed == 0) {
        return Error::InvalidBetKind;
    }
    if (input.count != needed) {
        return Error::InvalidGeometry;
    }

    BetSelection candidate{};
    candidate.kind = input.kind;
    candidate.count = input.count;
    for (std::size_t i = 0; i < input.count; ++i) {
        if (input.numbers[i].value >= kPocketCount) {
            return Error::InvalidNumber;
        }
        candidate.numbers[i] = input.numbers[i];
    }

    // Insertion sort needs no temporary allocation; there are at most 18 values.
    for (std::size_t i = 1; i < candidate.count; ++i) {
        const Number current = candidate.numbers[i];
        std::size_t position = i;
        while (position > 0 && candidate.numbers[position - 1].value > current.value) {
            candidate.numbers[position] = candidate.numbers[position - 1];
            --position;
        }
        candidate.numbers[position] = current;
    }
    for (std::size_t i = 1; i < candidate.count; ++i) {
        if (candidate.numbers[i].value == candidate.numbers[i - 1].value) {
            return Error::InvalidGeometry;
        }
    }
    if (!has_geometry(candidate)) {
        return Error::InvalidGeometry;
    }
    out = candidate;
    return Error::Ok;
}

Error make_inside_bet(BetKind kind, const Number* numbers,
                      std::size_t count, BetSelection& out) noexcept {
    if (kind != BetKind::Straight && kind != BetKind::Split && kind != BetKind::Street
        && kind != BetKind::Corner && kind != BetKind::SixLine) {
        return Error::InvalidBetKind;
    }
    if (numbers == nullptr || count == 0 || count > 6) {
        return Error::InvalidGeometry;
    }
    BetSelection candidate{};
    candidate.kind = kind;
    candidate.count = static_cast<std::uint8_t>(count);
    for (std::size_t i = 0; i < count; ++i) {
        candidate.numbers[i] = numbers[i];
    }
    return canonicalize(candidate, out);
}

Error make_dozen(std::uint8_t group, BetSelection& out) noexcept {
    if (group < 1 || group > 3) {
        return Error::InvalidGeometry;
    }
    BetSelection candidate{};
    candidate.kind = BetKind::Dozen;
    candidate.count = 12;
    for (std::size_t i = 0; i < candidate.count; ++i) {
        candidate.numbers[i] = Number{static_cast<std::uint8_t>((group - 1) * 12 + i + 1)};
    }
    return canonicalize(candidate, out);
}

Error make_column(std::uint8_t group, BetSelection& out) noexcept {
    if (group < 1 || group > 3) {
        return Error::InvalidGeometry;
    }
    BetSelection candidate{};
    candidate.kind = BetKind::Column;
    candidate.count = 12;
    for (std::size_t i = 0; i < candidate.count; ++i) {
        candidate.numbers[i] = Number{static_cast<std::uint8_t>(group + 3 * i)};
    }
    return canonicalize(candidate, out);
}

Error make_even_money_bet(BetKind kind, BetSelection& out) noexcept {
    if (expected_count(kind) != 18) {
        return Error::InvalidBetKind;
    }
    BetSelection candidate{};
    candidate.kind = kind;
    for (std::uint8_t value = 1; value < kPocketCount; ++value) {
        if (even_money_match(kind, Number{value})) {
            candidate.numbers[candidate.count++] = Number{value};
        }
    }
    return canonicalize(candidate, out);
}

Error standard_profit_odds(BetKind kind, std::uint8_t& out) noexcept {
    switch (kind) {
    case BetKind::Straight: out = 35; break;
    case BetKind::Split: out = 17; break;
    case BetKind::Street: out = 11; break;
    case BetKind::Corner: out = 8; break;
    case BetKind::SixLine: out = 5; break;
    case BetKind::Dozen:
    case BetKind::Column: out = 2; break;
    case BetKind::Red:
    case BetKind::Black:
    case BetKind::Odd:
    case BetKind::Even:
    case BetKind::Low:
    case BetKind::High: out = 1; break;
    default: return Error::InvalidBetKind;
    }
    return Error::Ok;
}

Error covers(const BetSelection& selection, Number result, bool& out) noexcept {
    if (result.value >= kPocketCount) {
        return Error::InvalidNumber;
    }
    BetSelection canonical{};
    const Error error = canonicalize(selection, canonical);
    if (error != Error::Ok) {
        return error;
    }
    bool found = false;
    for (std::size_t i = 0; i < canonical.count; ++i) {
        if (canonical.numbers[i].value == result.value) {
            found = true;
        }
    }
    out = found;
    return Error::Ok;
}

Error gross_return(const AcceptedBet& bet, Number result, Credits& out) noexcept {
    if (bet.request.stake <= 0) {
        return Error::InvalidAmount;
    }
    std::uint8_t expected_odds = 0;
    const Error odds_error = standard_profit_odds(bet.request.selection.kind, expected_odds);
    if (odds_error != Error::Ok) {
        return odds_error;
    }
    if (bet.profit_odds != expected_odds) {
        return Error::InvalidConfig;
    }
    bool winning = false;
    const Error coverage_error = covers(bet.request.selection, result, winning);
    if (coverage_error != Error::Ok) {
        return coverage_error;
    }
    if (!winning) {
        out = 0;
        return Error::Ok;
    }
    return checked_multiply(bet.request.stake, Credits{bet.profit_odds} + 1, out);
}

} // namespace mini_etg
