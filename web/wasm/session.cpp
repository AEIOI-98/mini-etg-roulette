#include "session.hpp"
#include "mini_etg/sim/bots.hpp"

#include <sstream>

namespace mini_etg::web {
namespace {

TickMs duration(const Snapshot& s) {
    switch (s.round.state) {
    case RoundState::Idle: return 500;
    case RoundState::BetsOpen: return 20000;
    case RoundState::BetsClosing: return 1200;
    case RoundState::BetsClosed: return 1600;
    case RoundState::Spinning: return s.config.ball_timeout_ms;
    case RoundState::Result: return 5000;
    case RoundState::Payout: return 3500;
    case RoundState::Fault: return 6500;
    }
    return 0;
}

} // namespace

Session::Session(std::shared_ptr<IStorage> storage, std::uint32_t seed)
    : storage_(std::move(storage)), wheel_random_(seed), lucky_random_(seed ^ 0x9e3779b9u),
      bot_random_(seed ^ 0x85ebca6bu), wheel_(wheel_random_), engine_(wheel_, *storage_, lucky_random_) {}

std::string Session::start(TickMs now) {
    if (started_) return error_text(Error::AlreadyInitialized);
    RecoveryReport report{};
    Error error = engine_.recover(report);
    if (error == Error::NoJournal) {
        EngineConfig config{};
        config.lucky.enabled = true;
        config.no_spin_timeout_ms = 1500;
        config.ball_timeout_ms = 4000;
        std::array<Credits, kSeatCount> funding{};
        funding.fill(1000);
        error = engine_.initialize(config, funding);
    } else if (error == Error::Ok) {
        recovery_ = "Restored round " + std::to_string(report.round_id);
        switch (report.action) {
        case RecoveryAction::Refunded: recovery_ += " · outstanding stakes refunded."; break;
        case RecoveryAction::PayoutCompleted: recovery_ += " · recorded result paid."; break;
        case RecoveryAction::AlreadyPaid: recovery_ += " · payout already recorded."; break;
        case RecoveryAction::None: recovery_ += " · journal verified."; break;
        }
    }
    if (error != Error::Ok) return error_text(error);
    started_ = true;
    entered_at_ = now;
    return after(error);
}

TickMs Session::remaining_ms(TickMs now) const {
    const TickMs interval = duration(engine_.snapshot());
    const TickMs elapsed = now - entered_at_;
    return elapsed < interval ? interval - elapsed : 0;
}

Error Session::open_with_bots() {
    Error error = engine_.open_bets();
    if (error != Error::Ok) return error;
    for (SeatId seat = 1; seat < kSeatCount; ++seat) {
        sim::BotDecision next{};
        error = sim::choose_bot_bet(engine_.snapshot(), seat, bot_random_, next);
        if (error != Error::Ok) return error;
        if (next.place) {
            BetId id{};
            error = engine_.place_bet(next.request, id);
            if (error != Error::Ok) return error;
        }
    }
    return Error::Ok;
}

std::string Session::after(Error error) {
    if (engine_.status() == RuntimeStatus::Ready) {
        const auto history_error = history_.sync(*storage_, engine_.snapshot());
        if (history_error != Error::Ok) return error_text(history_error);
    }
    if (error == Error::LimitExceeded) {
        const auto& limits = engine_.snapshot().config.limits;
        return "Table limit reached. Minimum placement " + std::to_string(limits.min_per_bet) +
            " credits, maximum per selection " + std::to_string(limits.max_per_bet) +
            ", maximum per seat " + std::to_string(limits.max_total_per_seat) + ".";
    }
    return error_text(error);
}

std::string Session::tick(TickMs now) {
    if (!started_ || engine_.status() != RuntimeStatus::Ready) return error_text(Error::NotReady);
    const auto before = engine_.snapshot().round.state;
    if (before != RoundState::Spinning && remaining_ms(now) > 0) return {};
    Error error = Error::Ok;
    switch (before) {
    case RoundState::Idle: error = open_with_bots(); break;
    case RoundState::BetsOpen: error = engine_.begin_closing(); break;
    case RoundState::BetsClosing: error = engine_.close_bets(); break;
    case RoundState::BetsClosed:
        error = engine_.start_spin(now);
        if (error == Error::Ok) armed_fault_.clear();
        break;
    case RoundState::Spinning: error = engine_.tick(now); break;
    case RoundState::Result: error = engine_.settle(); break;
    case RoundState::Payout: error = engine_.finish_round(); break;
    case RoundState::Fault: error = engine_.acknowledge_fault(); break;
    }
    if (engine_.snapshot().round.state != before) entered_at_ = now;
    if (error == Error::WheelError && engine_.status() == RuntimeStatus::Ready &&
        engine_.snapshot().round.state == RoundState::Fault) error = Error::Ok;
    return after(error);
}

std::string Session::place(unsigned selection_id, Credits stake, TickMs now) {
    auto error = tick(now);
    if (!error.empty()) return error;
    const auto* selection = catalog_.at(selection_id);
    // Unknown IDs enter the engine as an invalid empty selection, so rejection
    // is journalled under the same contract as a native request.
    BetId id{};
    return after(engine_.place_bet({0, selection ? *selection : BetSelection{}, stake}, id));
}

std::string Session::undo(TickMs now) {
    auto error = tick(now);
    return error.empty() ? after(engine_.undo_last_bet(0)) : error;
}

std::string Session::clear(TickMs now) {
    auto error = tick(now);
    return error.empty() ? after(engine_.clear_bets(0)) : error;
}

std::string Session::repeat(TickMs now) {
    auto error = tick(now);
    return error.empty() ? after(engine_.repeat_last_bets(0)) : error;
}

std::string Session::arm_fault(const std::string& name, TickMs now) {
    auto error = tick(now);
    if (!error.empty()) return error;
    sim::SimFault fault{};
    if (name == "BallTimeout") fault = sim::SimFault::BallTimeout;
    else if (name == "InvalidSensor") fault = sim::SimFault::InvalidSensor;
    else return "Unknown simulated fault.";
    const Error armed = wheel_.arm_fault(fault);
    if (armed == Error::Ok) armed_fault_ = name;
    return error_text(armed);
}

std::string Session::view() const {
    const Snapshot& s = engine_.snapshot();
    const Round& r = s.round;
    std::ostringstream out;
    // All inserted strings are generated locally from enums, numbers and fixed
    // messages. No user-entered text is interpolated into the JSON.
    out << R"({"revision":")" << s.revision << R"(","roundId":")" << r.id
        << R"(","state":")" << state_name(r.state) << R"(","fault":")" << fault_name(r.fault)
        << R"(","recovery":")" << recovery_ << R"(","luckyEnabled":)" << (s.config.lucky.enabled ? "true" : "false")
        << R"(,"lucky":[)";
    for (unsigned i = 0; i < r.lucky.count; ++i) {
        if (i) out << ',';
        out << R"({"number":)" << unsigned(r.lucky.entries[i].number.value)
            << R"(,"multiplier":)" << r.lucky.entries[i].gross_multiplier << '}';
    }
    out << R"(],"result":)";
    if (r.has_result) out << R"({"number":)" << unsigned(r.result_number.value) << R"(,"pocket":)" << unsigned(r.result_index.value) << '}';
    else out << "null";
    out << R"(,"seats":[)";
    for (SeatId i = 0; i < kSeatCount; ++i) {
        const Seat& seat = s.seats[i];
        if (i) out << ',';
        out << R"({"seat":)" << unsigned(i) << R"(,"name":")" << (i == 0 ? "Player" : sim::bot_name(i))
            << R"(","credits":")" << seat.credits << R"(","creditsIn":")" << seat.meters.credits_in
            << R"(","creditsOut":")" << seat.meters.credits_out << R"(","gamesPlayed":")" << seat.meters.games_played
            << R"(","gamesWon":")" << seat.meters.games_won << R"("})";
    }
    out << R"(],"bets":[)";
    for (std::size_t i = 0; i < r.bet_count; ++i) {
        const auto& bet = r.bets[i];
        if (i) out << ',';
        out << R"({"betId":")" << bet.id << R"(","seat":)" << unsigned(bet.request.seat)
            << R"(,"selectionId":)" << catalog_.id_for(bet.request.selection)
            << R"(,"stake":")" << bet.request.stake << R"("})";
    }
    const auto& limits = s.config.limits;
    out << R"(],"limits":{"min":")" << limits.min_per_bet << R"(","max":")" << limits.max_per_bet
        << R"(","seatMax":")" << limits.max_total_per_seat << R"("},"canRepeat":)"
        << (s.last_bets[0].count ? "true" : "false")
        << R"(,"house":")" << s.house_result << R"(","escrow":")" << r.escrow
        << R"(","armedFault":")" << armed_fault_ << '"';
    history_.write_json_fields(out);
    out << '}';
    return out.str();
}

} // namespace mini_etg::web
