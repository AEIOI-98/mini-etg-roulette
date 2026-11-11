#include "history.hpp"
#include "catalog.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace mini_etg::web {
namespace {
const char* event_name(EventKind kind) {
    constexpr const char* names[] = {"Initialized", "StateChanged", "BetAccepted", "BetSettled",
        "BetRefunded", "CreditLoaded", "CreditWithdrawn", "CommandRejected", "ResultRecorded",
        "RecoveryApplied", "LuckyNumbersDrawn", "BetCancelled"};
    return names[static_cast<unsigned>(kind)];
}
const char* command_name(Command command) {
    constexpr const char* names[] = {"Initialize", "Recover", "CreditIn", "CreditOut", "OpenBets",
        "BeginClosing", "CloseBets", "PlaceBet", "StartSpin", "Tick", "Settle", "FinishRound",
        "RaiseFault", "AcknowledgeFault", "UndoLastBet", "ClearBets", "RepeatLastBets"};
    return names[static_cast<unsigned>(command)];
}
std::string event_detail(const Event& e) {
    std::ostringstream out;
    switch (e.kind) {
    case EventKind::Initialized: return "Table initialized";
    case EventKind::StateChanged:
        out << state_name(e.from) << " → " << state_name(e.to);
        if (e.fault != FaultCode::None) out << " · " << fault_name(e.fault);
        break;
    case EventKind::CreditLoaded: case EventKind::CreditWithdrawn:
        out << "Seat " << unsigned(e.seat) << (e.kind == EventKind::CreditLoaded ? " loaded " : " withdrew ")
            << e.amount << " credits"; break;
    case EventKind::BetAccepted:
        out << "Seat " << unsigned(e.seat) << " · " << selection_label(e.selection)
            << " · " << e.amount << " credits"; break;
    case EventKind::BetSettled: case EventKind::BetRefunded: case EventKind::BetCancelled:
        out << "Seat " << unsigned(e.seat) << " · bet #" << e.bet_id
            << (e.kind == EventKind::BetSettled ? " returned " : " refunded ")
            << e.amount << " credits"; break;
    case EventKind::CommandRejected:
        out << command_name(e.command) << " · " << error_text(e.error); break;
    case EventKind::ResultRecorded:
        out << "Number " << unsigned(e.number.value) << " · pocket " << unsigned(e.pocket.value); break;
    case EventKind::RecoveryApplied:
        out << "Journal recovered · " << state_name(e.to); break;
    case EventKind::LuckyNumbersDrawn:
        for (unsigned i = 0; i < e.lucky.count; ++i) {
            if (i) out << ", ";
            out << unsigned(e.lucky.entries[i].number.value) << " ×" << e.lucky.entries[i].gross_multiplier;
        }
        break;
    }
    return out.str();
}
} // namespace

const char* state_name(RoundState state) {
    constexpr const char* names[] = {"Idle", "BetsOpen", "BetsClosing", "BetsClosed", "Spinning", "Result", "Payout", "Fault"};
    return names[static_cast<unsigned>(state)];
}
const char* fault_name(FaultCode fault) {
    constexpr const char* names[] = {"None", "NoSpin", "BallTimeout", "InvalidSensor", "StaleWheelReport",
        "WheelStartFailed", "PowerLoss", "OperatorInjected"};
    return names[static_cast<unsigned>(fault)];
}
const char* error_text(Error error) {
    constexpr const char* messages[] = {"", "Table needs recovery. Reload to inspect its journal.",
        "Table already initialized.", "Invalid table configuration.", "Unknown seat.", "Unknown number.",
        "Unknown bet type.", "Unknown betting position.", "Choose a positive whole-credit stake within the signed 64-bit range.", "Invalid bound.",
        "Betting has closed for this round.", "This seat has insufficient credits.",
        "Table limit reached. Check the selection and seat limits.", "This seat already has 16 active bets.",
        "This wager would exceed the supported credit range.", "The journal counter is full.",
        "Wheel fault.", "No saved journal.", "Event missing from journal.",
        "The journal is full. Reload to recover any outstanding stakes.",
        "Browser storage is unavailable. Reload after storage access is restored.",
        "The saved journal is damaged. Play is paused.",
        "The last save is uncertain. Reload to recover the journal.",
        "The accounting check failed.", "No bets are available for this action."};
    return messages[static_cast<unsigned>(error)];
}

Error History::sync(IStorage& storage, const Snapshot& snapshot) {
    LuckyMath math{};
    Error error = analyze_lucky(snapshot.config.lucky, math);
    if (error != Error::Ok) return error;
    while (cursor_ < snapshot.last_sequence) {
        Event e{};
        error = storage.read_event(cursor_ + 1, e);
        if (error != Error::Ok) return error;
        error = apply(e, math.straight_rtp);
        if (error != Error::Ok) return error;
        log_.push_back(e);
        if (log_.size() > 80) log_.pop_front();
        ++cursor_;
    }
    return Error::Ok;
}

Error History::apply(const Event& e, double straight_rtp) {
    if (e.kind == EventKind::BetAccepted) {
        BetSelection canonical{};
        if (!e.bet_id || e.amount <= 0 || active_.size() >= kMaxBets ||
            canonicalize(e.selection, canonical) != Error::Ok)
            return Error::StorageCorrupt;
        active_.push_back({e.bet_id, e.amount, e.selection.kind});
    } else if (e.kind == EventKind::BetRefunded || e.kind == EventKind::BetCancelled || e.kind == EventKind::BetSettled) {
        const auto it = std::find_if(active_.begin(), active_.end(), [&](const ActiveBet& bet) { return bet.id == e.bet_id; });
        if (it == active_.end() || e.amount < 0 ||
            (e.kind != EventKind::BetSettled && it->stake != e.amount))
            return Error::StorageCorrupt;
        if (e.kind == EventKind::BetSettled) {
            if (e.seat == 0 && checked_add(player_return_, e.amount, player_return_) != Error::Ok)
                return Error::StorageCorrupt;
            settled_stake_ += static_cast<Total>(it->stake);
            returns_ += static_cast<Total>(e.amount);
            if (it->kind == BetKind::Straight) straight_stake_ += static_cast<Total>(it->stake);
        }
        active_.erase(it);
    } else if (e.kind == EventKind::ResultRecorded) {
        if (e.number.value >= kPocketCount) return Error::StorageCorrupt;
        result_ = e.number.value;
    } else if (e.kind == EventKind::StateChanged && e.to == RoundState::BetsOpen) {
        player_return_ = 0;
        if (!active_.empty()) return Error::StorageCorrupt;
    } else if (e.kind == EventKind::StateChanged && e.to == RoundState::Payout) {
        ++rounds_;
        ++histogram_[result_];
        recent_.push_front(result_);
        if (recent_.size() > 12) recent_.pop_back();
        if (settled_stake_ != 0) {
            const double stake = static_cast<double>(settled_stake_);
            const double straight_fraction = static_cast<double>(straight_stake_) / stake;
            const double theory = straight_fraction * straight_rtp + (1.0 - straight_fraction) * (36.0 / 37.0);
            series_.push_back({rounds_, static_cast<double>(returns_) / stake, theory});
        }
    }
    return Error::Ok;
}

void History::write_json_fields(std::ostream& out) const {
    out << R"(,"playerReturn":")" << player_return_ << R"(","recentResults":[)";
    for (std::size_t i = 0; i < recent_.size(); ++i) {
        if (i) out << ',';
        out << recent_[i];
    }
    out << R"(],"events":[)";
    for (std::size_t i = 0; i < log_.size(); ++i) {
        const auto& e = log_[i];
        if (i) out << ',';
        out << R"({"sequence":")" << e.sequence << R"(","roundId":")" << e.round_id
            << R"(","kind":")" << event_name(e.kind) << R"(","command":")" << command_name(e.command)
            << R"(","detail":")" << event_detail(e) << R"("})";
    }
    out << std::setprecision(15) << R"(],"charts":{"rounds":)" << rounds_
        << R"(,"expected":)" << (static_cast<double>(rounds_) / kPocketCount) << R"(,"histogram":[)";
    for (std::size_t i = 0; i < histogram_.size(); ++i) {
        if (i) out << ',';
        out << histogram_[i];
    }
    out << R"(],"series":[)";
    // Retain the exact history in bounded journal memory. Limit SVG/transport
    // points and always include both endpoints, even in a long session.
    const std::size_t count = std::min<std::size_t>(series_.size(), 240);
    for (std::size_t i = 0; i < count; ++i) {
        if (i) out << ',';
        const auto& p = series_[count == 1 ? 0 : i * (series_.size() - 1) / (count - 1)];
        out << R"({"round":)" << p.round << R"(,"observed":)" << p.observed
            << R"(,"theoretical":)" << p.theoretical << '}';
    }
    out << "]}";
}

} // namespace mini_etg::web
