#pragma once

#include "mini_etg/storage.hpp"

#include <deque>
#include <vector>
#include <ostream>
#include <string>

namespace mini_etg::web {

const char* state_name(RoundState state);
const char* fault_name(FaultCode fault);
const char* error_text(Error error);

// Display-only projection of immutable events. Rebuilding after a reload uses
// the same path as advancing the cursor during a live session.
class History {
public:
    Error sync(IStorage& storage, const Snapshot& snapshot);
    void write_json_fields(std::ostream& out) const;
private:
    struct ActiveBet { BetId id; Credits stake; BetKind kind; };
    struct Point { std::size_t round; double observed; double theoretical; };
    Error apply(const Event& event, double straight_rtp);
    // At most UINT64_MAX events each carrying at most INT64_MAX credits. The
    // lifetime chart totals therefore fit in 128 unsigned bits, even if the
    // same credit is wagered many times. Core balances remain signed int64.
    using Total = unsigned __int128;
    Total settled_stake_{0}, straight_stake_{0}, returns_{0};
    std::vector<ActiveBet> active_;
    Sequence cursor_{0};
    std::size_t rounds_{0};
    std::array<std::size_t, kPocketCount> histogram_{};
    std::uint8_t result_{0};
    Credits player_return_{0};
    std::deque<Event> log_;
    std::deque<unsigned> recent_;
    std::vector<Point> series_;
};

} // namespace mini_etg::web
