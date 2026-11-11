#include "mini_etg/lucky.hpp"

#include <limits>

namespace mini_etg {
namespace {

std::uint32_t total_weight(const LuckyConfig& config) noexcept {
    // Called only after validation has checked that this sum fits uint32.
    std::uint32_t total = 0;
    for (std::size_t i = 0; i < config.table_count; ++i) total += config.table[i].weight;
    return total;
}

Error validate_bet(const AcceptedBet& bet, Number result, const LuckyConfig& config,
                   bool& winning, std::uint16_t& base_multiplier) noexcept {
    std::uint8_t expected_odds = 0;
    const Error odds_error = base_profit_odds(bet.request.selection.kind, config, expected_odds);
    if (odds_error != Error::Ok) return odds_error;
    if (bet.request.stake <= 0) return Error::InvalidAmount;
    if (bet.profit_odds != expected_odds) return Error::InvalidConfig;
    const Error coverage_error = covers(bet.request.selection, result, winning);
    if (coverage_error != Error::Ok) return coverage_error;
    base_multiplier = static_cast<std::uint16_t>(expected_odds + 1);
    return Error::Ok;
}

Error return_credits(const AcceptedBet& bet, bool winning, std::uint16_t multiplier,
                     Credits& out) noexcept {
    if (!winning) {
        out = 0;
        return Error::Ok;
    }
    return checked_multiply(bet.request.stake, Credits{multiplier}, out);
}

} // namespace

Error validate_lucky_config(const LuckyConfig& config) noexcept {
    if (config.base_straight_profit_odds > 35 || config.min_count == 0 ||
        config.min_count > config.max_count || config.max_count > kMaxLuckyNumbers ||
        config.table_count == 0 || config.table_count > kMaxMultiplierEntries) {
        return Error::InvalidConfig;
    }
    std::uint64_t weight_sum = 0;
    for (std::size_t i = 0; i < config.table_count; ++i) {
        const MultiplierWeight& entry = config.table[i];
        if (entry.weight == 0 || entry.gross_multiplier <= config.base_straight_profit_odds + 1) {
            return Error::InvalidConfig;
        }
        for (std::size_t earlier = 0; earlier < i; ++earlier) {
            if (config.table[earlier].gross_multiplier == entry.gross_multiplier) {
                return Error::InvalidConfig;
            }
        }
        weight_sum += entry.weight;
    }
    return weight_sum <= std::numeric_limits<std::uint32_t>::max()
        ? Error::Ok : Error::InvalidConfig;
}

Error validate_lucky_draw(const LuckyConfig& config, const LuckyDraw& draw) noexcept {
    const Error config_error = validate_lucky_config(config);
    if (config_error != Error::Ok) return config_error;
    if (config.enabled) {
        if (draw.count < config.min_count || draw.count > config.max_count) return Error::InvalidConfig;
    } else if (draw.count != 0) {
        return Error::InvalidConfig;
    }
    for (std::size_t i = 0; i < draw.entries.size(); ++i) {
        const LuckyNumber& entry = draw.entries[i];
        if (i >= draw.count) {
            if (entry.number.value != 0 || entry.gross_multiplier != 0) return Error::InvalidConfig;
            continue;
        }
        if (entry.number.value >= kPocketCount) return Error::InvalidConfig;
        bool supported = false;
        for (std::size_t table = 0; table < config.table_count; ++table) {
            supported = supported || config.table[table].gross_multiplier == entry.gross_multiplier;
        }
        if (!supported) return Error::InvalidConfig;
        for (std::size_t earlier = 0; earlier < i; ++earlier) {
            if (draw.entries[earlier].number.value == entry.number.value) return Error::InvalidConfig;
        }
    }
    return Error::Ok;
}

Error draw_lucky_numbers(IRandom& random, const LuckyConfig& config, LuckyDraw& out) noexcept {
    const Error config_error = validate_lucky_config(config);
    if (config_error != Error::Ok) return config_error;
    LuckyDraw next{};
    if (!config.enabled) {
        out = next;
        return Error::Ok;
    }
    std::uint32_t count_offset = 0;
    Error error = uniform_below(random, config.max_count - config.min_count + 1, count_offset);
    if (error != Error::Ok) return error;
    next.count = static_cast<std::uint8_t>(config.min_count + count_offset);

    std::array<Number, kPocketCount> remaining{};
    for (std::size_t i = 0; i < remaining.size(); ++i) {
        remaining[i] = Number{static_cast<std::uint8_t>(i)};
    }
    const std::uint32_t weight_sum = total_weight(config);
    for (std::size_t i = 0; i < next.count; ++i) {
        // Choose a position from the unchosen suffix and swap it to the front.
        std::uint32_t offset = 0;
        error = uniform_below(random, static_cast<std::uint32_t>(remaining.size() - i), offset);
        if (error != Error::Ok) return error;
        const std::size_t selected = i + offset;
        const Number number = remaining[selected];
        remaining[selected] = remaining[i];
        remaining[i] = number;

        std::uint32_t ticket = 0;
        error = uniform_below(random, weight_sum, ticket);
        if (error != Error::Ok) return error;
        // Weight intervals are [0,w0), [w0,w0+w1), ... with no gaps.
        for (std::size_t table = 0; table < config.table_count; ++table) {
            if (ticket < config.table[table].weight) {
                next.entries[i] = {number, config.table[table].gross_multiplier};
                break;
            }
            ticket -= config.table[table].weight;
        }
    }
    out = next;
    return Error::Ok;
}

Error base_profit_odds(BetKind kind, const LuckyConfig& config, std::uint8_t& out) noexcept {
    const Error config_error = validate_lucky_config(config);
    if (config_error != Error::Ok) return config_error;
    std::uint8_t odds = 0;
    const Error odds_error = standard_profit_odds(kind, odds);
    if (odds_error != Error::Ok) return odds_error;
    if (config.enabled && kind == BetKind::Straight) odds = config.base_straight_profit_odds;
    out = odds;
    return Error::Ok;
}

Error base_gross_return(const AcceptedBet& bet, Number result,
                        const LuckyConfig& config, Credits& out) noexcept {
    bool winning = false;
    std::uint16_t multiplier = 0;
    const Error error = validate_bet(bet, result, config, winning, multiplier);
    return error == Error::Ok ? return_credits(bet, winning, multiplier, out) : error;
}

Error maximum_gross_return(const AcceptedBet& bet, Number result,
                           const LuckyConfig& config, Credits& out) noexcept {
    bool winning = false;
    std::uint16_t multiplier = 0;
    const Error error = validate_bet(bet, result, config, winning, multiplier);
    if (error != Error::Ok) return error;
    if (config.enabled && bet.request.selection.kind == BetKind::Straight) {
        for (std::size_t i = 0; i < config.table_count; ++i) {
            if (config.table[i].gross_multiplier > multiplier) multiplier = config.table[i].gross_multiplier;
        }
    }
    return return_credits(bet, winning, multiplier, out);
}

Error lucky_gross_return(const AcceptedBet& bet, Number result, const LuckyConfig& config,
                         const LuckyDraw& draw, Credits& out) noexcept {
    const Error draw_error = validate_lucky_draw(config, draw);
    if (draw_error != Error::Ok) return draw_error;
    bool winning = false;
    std::uint16_t multiplier = 0;
    const Error error = validate_bet(bet, result, config, winning, multiplier);
    if (error != Error::Ok) return error;
    if (config.enabled && bet.request.selection.kind == BetKind::Straight) {
        for (std::size_t i = 0; i < draw.count; ++i) {
            if (draw.entries[i].number.value == result.value) multiplier = draw.entries[i].gross_multiplier;
        }
    }
    return return_credits(bet, winning, multiplier, out);
}

Error analyze_lucky(const LuckyConfig& config, LuckyMath& out) noexcept {
    const Error error = validate_lucky_config(config);
    if (error != Error::Ok) return error;
    LuckyMath next{};
    const double pockets = static_cast<double>(kPocketCount);
    double base = 36.0;
    double selected_probability = 0.0;
    double multiplier_second_moment = 0.0;
    if (config.enabled) {
        base = config.base_straight_profit_odds + 1.0;
        next.expected_count = (config.min_count + config.max_count) / 2.0;
        selected_probability = next.expected_count / pockets;
        const double weights = total_weight(config);
        for (std::size_t i = 0; i < config.table_count; ++i) {
            const double multiplier = config.table[i].gross_multiplier;
            const double probability = config.table[i].weight / weights;
            next.expected_multiplier += probability * multiplier;
            multiplier_second_moment += probability * multiplier * multiplier;
        }
    }
    // The fixed bet wins with probability 1/37, independently of its Lucky status.
    next.straight_rtp = (base + selected_probability * (next.expected_multiplier - base)) / pockets;
    const double second_moment = (base * base + selected_probability *
        (multiplier_second_moment - base * base)) / pockets;
    next.straight_return_variance = second_moment - next.straight_rtp * next.straight_rtp;
    out = next;
    return Error::Ok;
}

} // namespace mini_etg
