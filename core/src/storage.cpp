#include "mini_etg/storage.hpp"

namespace mini_etg {

Error required_resolution_reserve(const Snapshot& next,
                                  ResolutionReserve& out) noexcept {
    if (next.round.bet_count > kMaxBets) {
        return Error::StorageCorrupt;
    }
    const Sequence bets = static_cast<Sequence>(next.round.bet_count);
    ResolutionReserve reserve{};
    switch (next.round.state) {
    case RoundState::Idle:
        break;
    case RoundState::BetsOpen:
        reserve = {6, bets + 8 + (next.config.lucky.enabled ? 1 : 0)};
        break;
    case RoundState::BetsClosing:
        reserve = {5, bets + 7 + (next.config.lucky.enabled ? 1 : 0)};
        break;
    case RoundState::BetsClosed:
        reserve = {4, bets + 6};
        break;
    case RoundState::Spinning:
        reserve = {3, bets + 5};
        break;
    case RoundState::Result:
        reserve = {2, bets + 3};
        break;
    case RoundState::Payout:
    case RoundState::Fault:
        reserve = {1, 1};
        break;
    default:
        return Error::StorageCorrupt;
    }
    out = reserve;
    return Error::Ok;
}

} // namespace mini_etg
