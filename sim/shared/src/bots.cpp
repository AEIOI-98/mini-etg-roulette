#include "mini_etg/sim/bots.hpp"

namespace mini_etg::sim {
namespace {

Error cycling_inside_bet(RoundId cycle, BetSelection& out) noexcept {
    const RoundId row_cycle = cycle / 3;
    std::array<Number, 6> numbers{};
    const auto kind = cycle % 3;
    if (kind == 0) {
        const auto first = static_cast<std::uint8_t>(1 + 3 * (row_cycle % 12));
        for (std::size_t i = 0; i < 3; ++i) {
            numbers[i] = Number{static_cast<std::uint8_t>(first + i)};
        }
        return make_inside_bet(BetKind::Street, numbers.data(), 3, out);
    }
    if (kind == 1) {
        const auto first = static_cast<std::uint8_t>(1 + 3 * (row_cycle % 11) + (row_cycle / 11) % 2);
        numbers[0] = Number{first};
        numbers[1] = Number{static_cast<std::uint8_t>(first + 1)};
        numbers[2] = Number{static_cast<std::uint8_t>(first + 3)};
        numbers[3] = Number{static_cast<std::uint8_t>(first + 4)};
        return make_inside_bet(BetKind::Corner, numbers.data(), 4, out);
    }
    const auto first = static_cast<std::uint8_t>(1 + 3 * (row_cycle % 11));
    for (std::size_t i = 0; i < numbers.size(); ++i) {
        numbers[i] = Number{static_cast<std::uint8_t>(first + i)};
    }
    return make_inside_bet(BetKind::SixLine, numbers.data(), numbers.size(), out);
}

Error select_bet(RoundId cycle, SeatId seat, IRandom& random, BetSelection& out) noexcept {
    std::uint32_t choice = 0;
    switch (seat) {
    case 0: {
        const Number zero{0};
        return make_inside_bet(BetKind::Straight, &zero, 1, out);
    }
    case 1: {
        const Error error = uniform_below(random, static_cast<std::uint32_t>(kPocketCount), choice);
        if (error != Error::Ok) return error;
        const Number number{static_cast<std::uint8_t>(choice)};
        return make_inside_bet(BetKind::Straight, &number, 1, out);
    }
    case 2:
        return make_even_money_bet(BetKind::Red, out);
    case 3:
        return make_even_money_bet(cycle % 2 == 0 ? BetKind::Odd : BetKind::Even, out);
    case 4:
        return make_dozen(static_cast<std::uint8_t>(1 + cycle % 3), out);
    case 5:
        return make_column(static_cast<std::uint8_t>(1 + cycle % 3), out);
    case 6: {
        // Twelve rows each have two horizontal edges. This excludes the gap
        // between adjacent rows, such as 3/4, without inventing a new validator.
        const Error error = uniform_below(random, 24, choice);
        if (error != Error::Ok) return error;
        const auto first = static_cast<std::uint8_t>(1 + 3 * (choice / 2) + choice % 2);
        const Number numbers[] = {{first}, {static_cast<std::uint8_t>(first + 1)}};
        return make_inside_bet(BetKind::Split, numbers, 2, out);
    }
    case 7:
        return cycling_inside_bet(cycle, out);
    default:
        return Error::InvalidSeat;
    }
}

} // namespace

const char* bot_name(SeatId seat) noexcept {
    constexpr const char* names[] = {
        "fixed-zero", "random-straight", "flat-red", "alternating-odd-even",
        "rotating-dozen", "rotating-column", "random-horizontal-split", "cycling-inside"
    };
    return seat < kSeatCount ? names[seat] : "unknown";
}

Error choose_bot_bet(const Snapshot& snapshot, SeatId seat,
                     IRandom& random, BotDecision& out) noexcept {
    if (snapshot.round.state != RoundState::BetsOpen) return Error::IllegalTransition;
    if (seat >= kSeatCount) return Error::InvalidSeat;
    const Error config_error = validate_config(snapshot.config);
    if (config_error != Error::Ok) return config_error;
    if (snapshot.round.bet_count > kMaxBets) return Error::StorageCorrupt;
    if (snapshot.seats[seat].credits < 0) return Error::AccountingMismatch;

    BotDecision next{};
    for (std::size_t i = 0; i < snapshot.round.bet_count; ++i) {
        if (snapshot.round.bets[i].request.seat == seat) {
            out = next;
            return Error::Ok;
        }
    }
    if (snapshot.round.bet_count == kMaxBets) {
        out = next;
        return Error::Ok;
    }

    Credits stake = seat == 0 || seat == 1 || seat == 6 || seat == 7 ? 5 : 10;
    const TableLimits& limits = snapshot.config.limits;
    if (stake > snapshot.seats[seat].credits) stake = snapshot.seats[seat].credits;
    if (stake > limits.max_per_bet) stake = limits.max_per_bet;
    if (stake > limits.max_total_per_seat) stake = limits.max_total_per_seat;
    if (stake < limits.min_per_bet) {
        out = next;
        return Error::Ok;
    }

    next.request.seat = seat;
    next.request.stake = stake;
    // Real rounds start at one; zero is treated as the first pattern in a
    // caller-built strategy fixture. The engine still validates every request.
    const RoundId cycle = snapshot.round.id == 0 ? 0 : snapshot.round.id - 1;
    const Error error = select_bet(cycle, seat, random, next.request.selection);
    if (error != Error::Ok) return error;
    next.place = true;
    out = next;
    return Error::Ok;
}

} // namespace mini_etg::sim
