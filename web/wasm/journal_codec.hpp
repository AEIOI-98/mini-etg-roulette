// Version 1 wire format. Fixed-width little-endian fields avoid compiler padding,
// pointer widths, enum layout and lossy JavaScript number conversions.
#pragma once

#include "mini_etg/storage.hpp"
#include <cstring>
#include <type_traits>
#include <vector>

namespace mini_etg::web::wire {
using Bytes = std::vector<std::uint8_t>;

struct Writer {
    Bytes& bytes;
    template<class T> void scalar(T& value) {
        std::uint64_t bits = static_cast<std::uint64_t>(value);
        for (std::size_t i = 0; i < sizeof(T); ++i) {
            bytes.push_back(static_cast<std::uint8_t>(bits));
            bits >>= 8;
        }
    }
};

struct Reader {
    const std::uint8_t* data;
    std::size_t remaining;
    bool ok{true};
    template<class T> void scalar(T& value) {
        if (remaining < sizeof(T)) { ok = false; return; }
        std::uint64_t bits = 0;
        for (std::size_t i = 0; i < sizeof(T); ++i)
            bits |= std::uint64_t{*data++} << (i * 8);
        remaining -= sizeof(T);
        if constexpr (std::is_same_v<T, bool>) {
            if (bits > 1) ok = false;
            value = bits != 0;
        } else if constexpr (std::is_same_v<T, Credits>) {
            std::memcpy(&value, &bits, sizeof(value));
        } else {
            value = static_cast<T>(bits);
        }
    }
};

template<class Archive, class... Fields> void fields(Archive& a, Fields&... values) {
    (a.scalar(values), ...);
}

template<class Archive> void size(Archive& a, std::size_t& count) {
    auto fixed = static_cast<std::uint32_t>(count);
    a.scalar(fixed);
    count = fixed;
}

template<class Archive> void selection(Archive& a, BetSelection& value) {
    fields(a, value.kind, value.count);
    for (auto& n : value.numbers) a.scalar(n.value);
}

template<class Archive> void draw(Archive& a, LuckyDraw& value) {
    a.scalar(value.count);
    for (auto& n : value.entries) fields(a, n.number.value, n.gross_multiplier);
}

template<class Archive> void snapshot(Archive& a, Snapshot& s) {
    fields(a, s.format_version, s.revision, s.last_sequence, s.next_bet_id, s.house_result);
    auto& c = s.config;
    fields(a, c.limits.min_per_bet, c.limits.max_per_bet, c.limits.max_total_per_seat,
           c.no_spin_timeout_ms, c.ball_timeout_ms, c.lucky.enabled,
           c.lucky.base_straight_profit_odds, c.lucky.min_count, c.lucky.max_count);
    size(a, c.lucky.table_count);
    for (auto& m : c.lucky.table) fields(a, m.gross_multiplier, m.weight);
    for (auto& seat : s.seats)
        fields(a, seat.credits, seat.meters.credits_in, seat.meters.credits_out,
               seat.meters.games_played, seat.meters.games_won);
    auto& r = s.round;
    fields(a, r.id, r.state, r.resolution, r.fault, r.has_result, r.result_index.value,
           r.result_number.value, r.escrow, r.opening_total);
    size(a, r.bet_count);
    draw(a, r.lucky);
    for (auto& bet : r.bets) {
        fields(a, bet.id, bet.request.seat, bet.request.stake, bet.profit_odds);
        selection(a, bet.request.selection);
    }
    for (auto& pattern : s.last_bets) {
        a.scalar(pattern.count);
        for (auto& bet : pattern.bets) { selection(a, bet.selection); a.scalar(bet.stake); }
    }
}

template<class Archive> void event(Archive& a, Event& e) {
    fields(a, e.sequence, e.revision, e.round_id, e.kind, e.command, e.from, e.to, e.error,
           e.fault, e.seat, e.bet_id, e.profit_odds, e.amount, e.pocket.value, e.number.value);
    selection(a, e.selection);
    draw(a, e.lucky);
}

// FNV-1a detects accidental corruption. This demo journal is under the user's control.
inline std::uint32_t checksum(const Bytes& bytes, std::size_t count) {
    std::uint32_t hash = 2166136261u;
    for (std::size_t i = 0; i < count; ++i) hash = (hash ^ bytes[i]) * 16777619u;
    return hash;
}
} // namespace mini_etg::web::wire
