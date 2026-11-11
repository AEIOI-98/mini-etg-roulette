#include "mini_etg/model.hpp"
#include "mini_etg/wheel.hpp"

namespace mini_etg {
namespace {

bool valid_state(RoundState state) noexcept {
    return static_cast<unsigned>(state) <= static_cast<unsigned>(RoundState::Fault);
}

bool valid_fault(FaultCode fault) noexcept {
    return static_cast<unsigned>(fault) <= static_cast<unsigned>(FaultCode::OperatorInjected);
}

bool same_selection(const BetSelection& a, const BetSelection& b) noexcept {
    if (a.kind != b.kind || a.count != b.count) {
        return false;
    }
    for (std::size_t i = 0; i < a.numbers.size(); ++i) {
        if (a.numbers[i].value != b.numbers[i].value) {
            return false;
        }
    }
    return true;
}

bool valid_round_shape(const Round& round) noexcept {
    if (!valid_state(round.state) || !valid_fault(round.fault) ||
        static_cast<unsigned>(round.resolution) > static_cast<unsigned>(Resolution::Refunded) ||
        round.bet_count > kMaxBets) {
        return false;
    }

    switch (round.state) {
    case RoundState::Idle:
        if (round.resolution == Resolution::Pending || round.escrow != 0) {
            return false;
        }
        break;
    case RoundState::BetsOpen:
    case RoundState::BetsClosing:
    case RoundState::BetsClosed:
    case RoundState::Spinning:
        if (round.resolution != Resolution::Pending || round.has_result ||
            round.fault != FaultCode::None) {
            return false;
        }
        break;
    case RoundState::Result:
        if (round.resolution != Resolution::Pending || !round.has_result ||
            round.fault != FaultCode::None) {
            return false;
        }
        break;
    case RoundState::Payout:
        if (round.resolution != Resolution::Paid || !round.has_result ||
            round.escrow != 0 || round.fault != FaultCode::None) {
            return false;
        }
        break;
    case RoundState::Fault:
        if ((round.resolution != Resolution::None && round.resolution != Resolution::Refunded) ||
            round.has_result || round.escrow != 0 || round.fault == FaultCode::None) {
            return false;
        }
        break;
    }

    if (round.resolution == Resolution::None) {
        return round.bet_count == 0 && !round.has_result && round.escrow == 0;
    }
    if (round.id == 0) {
        return false;
    }
    if (round.resolution == Resolution::Paid && !round.has_result) {
        return false;
    }
    if (round.resolution == Resolution::Refunded && round.has_result) {
        return false;
    }
    return true;
}

bool valid_lucky_phase(const Snapshot& snapshot) noexcept {
    const Round& round = snapshot.round;
    const LuckyConfig& config = snapshot.config.lucky;
    if (!config.enabled) return validate_lucky_draw(config, round.lucky) == Error::Ok;

    const bool must_have_draw = round.state == RoundState::BetsClosed ||
        round.state == RoundState::Spinning || round.state == RoundState::Result ||
        round.resolution == Resolution::Paid;
    const bool must_be_empty = round.state == RoundState::BetsOpen ||
        round.state == RoundState::BetsClosing || round.resolution == Resolution::None;
    if (round.lucky.count == 0) {
        if (must_have_draw) return false;
        LuckyConfig disabled = config;
        disabled.enabled = false;
        return validate_lucky_draw(disabled, round.lucky) == Error::Ok;
    }
    return !must_be_empty && validate_lucky_draw(config, round.lucky) == Error::Ok;
}

} // namespace

Error validate_config(const EngineConfig& config) noexcept {
    const TableLimits& limits = config.limits;
    constexpr TickMs half_range = TickMs{1} << 31;
    if (limits.min_per_bet <= 0 || limits.max_per_bet < limits.min_per_bet ||
        limits.max_total_per_seat < limits.min_per_bet ||
        config.no_spin_timeout_ms == 0 ||
        config.no_spin_timeout_ms >= config.ball_timeout_ms ||
        config.ball_timeout_ms >= half_range ||
        validate_lucky_config(config.lucky) != Error::Ok) {
        return Error::InvalidConfig;
    }
    return Error::Ok;
}

Error check_accounting(const Snapshot& snapshot) noexcept {
    Credits available = 0;
    Credits credits_in = 0;
    Credits credits_out = 0;
    for (const Seat& seat : snapshot.seats) {
        if (seat.credits < 0 || seat.meters.credits_in < 0 || seat.meters.credits_out < 0 ||
            seat.meters.games_won > seat.meters.games_played) {
            return Error::AccountingMismatch;
        }
        if (checked_add(available, seat.credits, available) != Error::Ok ||
            checked_add(credits_in, seat.meters.credits_in, credits_in) != Error::Ok ||
            checked_add(credits_out, seat.meters.credits_out, credits_out) != Error::Ok) {
            return Error::ArithmeticOverflow;
        }
    }
    if (snapshot.round.escrow < 0 || snapshot.round.bet_count > kMaxBets) {
        return Error::AccountingMismatch;
    }

    Credits stakes = 0;
    for (std::size_t i = 0; i < snapshot.round.bet_count; ++i) {
        const Credits stake = snapshot.round.bets[i].request.stake;
        if (stake <= 0) {
            return Error::AccountingMismatch;
        }
        if (checked_add(stakes, stake, stakes) != Error::Ok) {
            return Error::ArithmeticOverflow;
        }
    }
    const Credits expected_escrow =
        snapshot.round.resolution == Resolution::Pending ? stakes : 0;
    if (snapshot.round.escrow != expected_escrow) {
        return Error::AccountingMismatch;
    }

    Credits actual = 0;
    Credits expected = 0;
    if (checked_add(available, snapshot.round.escrow, actual) != Error::Ok ||
        checked_add(actual, snapshot.house_result, actual) != Error::Ok ||
        checked_subtract(credits_in, credits_out, expected) != Error::Ok) {
        return Error::ArithmeticOverflow;
    }
    if (actual != expected) {
        return Error::AccountingMismatch;
    }
    const bool has_current_round = snapshot.round.state != RoundState::Idle &&
                                   snapshot.round.resolution != Resolution::None;
    if (has_current_round && actual != snapshot.round.opening_total) {
        return Error::AccountingMismatch;
    }
    return Error::Ok;
}

Error validate_snapshot(const Snapshot& snapshot) noexcept {
    if (snapshot.format_version != 2 || snapshot.revision == 0 ||
        snapshot.last_sequence < snapshot.revision || snapshot.next_bet_id == 0 ||
        validate_config(snapshot.config) != Error::Ok ||
        !valid_round_shape(snapshot.round) || !valid_lucky_phase(snapshot)) {
        return Error::StorageCorrupt;
    }

    std::array<Credits, kSeatCount> seat_stakes{};
    std::array<std::size_t, kSeatCount> seat_bets{};
    BetId previous_id = 0;
    for (std::size_t i = 0; i < snapshot.round.bet_count; ++i) {
        const AcceptedBet& bet = snapshot.round.bets[i];
        const BetRequest& request = bet.request;
        if (bet.id <= previous_id || bet.id >= snapshot.next_bet_id || request.seat >= kSeatCount ||
            request.stake < snapshot.config.limits.min_per_bet ||
            request.stake > snapshot.config.limits.max_per_bet) {
            return Error::StorageCorrupt;
        }
        previous_id = bet.id;
        BetSelection canonical{};
        std::uint8_t expected_odds = 0;
        if (canonicalize(request.selection, canonical) != Error::Ok ||
            !same_selection(request.selection, canonical) ||
            base_profit_odds(request.selection.kind, snapshot.config.lucky, expected_odds) != Error::Ok ||
            bet.profit_odds != expected_odds) {
            return Error::StorageCorrupt;
        }
        if (++seat_bets[request.seat] > kMaxBetsPerSeat ||
            checked_add(seat_stakes[request.seat], request.stake,
                        seat_stakes[request.seat]) != Error::Ok ||
            seat_stakes[request.seat] > snapshot.config.limits.max_total_per_seat) {
            return Error::StorageCorrupt;
        }
        Credits matching_stake = request.stake;
        for (std::size_t earlier = 0; earlier < i; ++earlier) {
            const BetRequest& other = snapshot.round.bets[earlier].request;
            if (other.seat == request.seat && same_selection(other.selection, request.selection)) {
                if (checked_add(matching_stake, other.stake, matching_stake) != Error::Ok ||
                    matching_stake > snapshot.config.limits.max_per_bet) {
                    return Error::StorageCorrupt;
                }
            }
        }
    }

    if (snapshot.round.has_result) {
        Number mapped{};
        if (number_at(snapshot.round.result_index, mapped) != Error::Ok ||
            mapped.value != snapshot.round.result_number.value) {
            return Error::StorageCorrupt;
        }
    }
    return check_accounting(snapshot) == Error::Ok ? Error::Ok : Error::StorageCorrupt;
}

} // namespace mini_etg
