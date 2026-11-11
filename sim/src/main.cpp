#include "mini_etg/engine.hpp"
#include "mini_etg/sim/bots.hpp"
#include "mini_etg/sim/journal.hpp"
#include "mini_etg/sim/random.hpp"
#include "mini_etg/sim/wheel.hpp"

#include <array>
#include <charconv>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <string_view>

using namespace mini_etg;
using namespace mini_etg::sim;

namespace {

constexpr std::uint32_t kMaxRounds = 10000;
constexpr Credits kInitialCredits = 1000;
constexpr std::uint32_t kLuckySeedMask = 0x9e3779b9u;
constexpr std::uint32_t kBotSeedMask = 0x85ebca6bu;

enum class Injection { None, NoSpin, BallTimeout, InvalidSensor, StartFailure, PowerLoss };

struct Options {
    std::uint32_t seed{20261111};
    std::uint32_t rounds{10};
    bool lucky{false};
    bool help{false};
    Injection injection{Injection::None};
};

void usage(std::ostream& out) {
    out << "Usage: roulette_sim [--seed UINT32] [--rounds 1..10000] [--lucky-mode]\n"
        << "                    [--inject-fault=TYPE] [--help]\n"
        << "TYPE: none, no-spin, ball-timeout, invalid-sensor, start-failure, power-loss\n"
        << "Fault injection affects round 1 only; later rounds run normally.\n"
        << "Eight bots start with 1000 integer credits each; unaffordable bets are skipped.\n"
        << "Uses virtual time and an in-memory journal; no disk persistence or real money.\n";
}

bool parse_unsigned(std::string_view text, std::uint32_t& out) {
    if (text.empty()) return false;
    std::uint32_t next = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), next);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) return false;
    out = next;
    return true;
}

bool parse_injection(std::string_view text, Injection& out) {
    if (text == "none") out = Injection::None;
    else if (text == "no-spin") out = Injection::NoSpin;
    else if (text == "ball-timeout") out = Injection::BallTimeout;
    else if (text == "invalid-sensor") out = Injection::InvalidSensor;
    else if (text == "start-failure") out = Injection::StartFailure;
    else if (text == "power-loss") out = Injection::PowerLoss;
    else return false;
    return true;
}

bool parse_options(int argc, char** argv, Options& out) {
    bool seed_seen = false, rounds_seen = false, lucky_seen = false, injection_seen = false;
    for (int i = 1; i < argc; ++i) {
        std::string_view name = argv[i];
        if (name == "--help" || name == "-h") {
            if (out.help) return false;
            out.help = true;
            continue;
        }
        if (name == "--lucky-mode") {
            if (lucky_seen) return false;
            lucky_seen = out.lucky = true;
            continue;
        }
        const auto equals = name.find('=');
        std::string_view value;
        if (equals != std::string_view::npos) {
            value = name.substr(equals + 1);
            name = name.substr(0, equals);
        } else {
            if (++i >= argc) return false;
            value = argv[i];
        }
        if (name == "--seed" && !seed_seen) {
            if (!parse_unsigned(value, out.seed)) return false;
            seed_seen = true;
        } else if (name == "--rounds" && !rounds_seen) {
            if (!parse_unsigned(value, out.rounds) || out.rounds == 0 || out.rounds > kMaxRounds) return false;
            rounds_seen = true;
        } else if (name == "--inject-fault" && !injection_seen) {
            if (!parse_injection(value, out.injection)) return false;
            injection_seen = true;
        } else {
            return false;
        }
    }
    return true;
}

const char* state_name(RoundState state) {
    switch (state) {
    case RoundState::Idle: return "Idle";
    case RoundState::BetsOpen: return "BetsOpen";
    case RoundState::BetsClosing: return "BetsClosing";
    case RoundState::BetsClosed: return "BetsClosed";
    case RoundState::Spinning: return "Spinning";
    case RoundState::Result: return "Result";
    case RoundState::Payout: return "Payout";
    case RoundState::Fault: return "Fault";
    }
    return "InvalidState";
}

const char* fault_name(FaultCode fault) {
    switch (fault) {
    case FaultCode::None: return "None";
    case FaultCode::NoSpin: return "NoSpin";
    case FaultCode::BallTimeout: return "BallTimeout";
    case FaultCode::InvalidSensor: return "InvalidSensor";
    case FaultCode::StaleWheelReport: return "StaleWheelReport";
    case FaultCode::WheelStartFailed: return "WheelStartFailed";
    case FaultCode::PowerLoss: return "PowerLoss";
    case FaultCode::OperatorInjected: return "OperatorInjected";
    }
    return "InvalidFault";
}

const char* bet_name(BetKind kind) {
    switch (kind) {
    case BetKind::Straight: return "Straight";
    case BetKind::Split: return "Split";
    case BetKind::Street: return "Street";
    case BetKind::Corner: return "Corner";
    case BetKind::SixLine: return "SixLine";
    case BetKind::Dozen: return "Dozen";
    case BetKind::Column: return "Column";
    case BetKind::Red: return "Red";
    case BetKind::Black: return "Black";
    case BetKind::Odd: return "Odd";
    case BetKind::Even: return "Even";
    case BetKind::Low: return "Low";
    case BetKind::High: return "High";
    }
    return "InvalidBet";
}

bool require_ok(Error error, const char* operation) {
    if (error == Error::Ok) return true;
    std::cerr << operation << " failed (engine error " << static_cast<unsigned>(error) << ").\n";
    return false;
}

SimFault wheel_fault(Injection injection) {
    switch (injection) {
    case Injection::NoSpin: return SimFault::NoSpin;
    case Injection::BallTimeout: return SimFault::BallTimeout;
    case Injection::InvalidSensor: return SimFault::InvalidSensor;
    case Injection::StartFailure: return SimFault::StartFailure;
    default: return SimFault::None;
    }
}

FaultCode expected_fault(Injection injection) {
    switch (injection) {
    case Injection::NoSpin: return FaultCode::NoSpin;
    case Injection::BallTimeout: return FaultCode::BallTimeout;
    case Injection::InvalidSensor: return FaultCode::InvalidSensor;
    case Injection::StartFailure: return FaultCode::WheelStartFailed;
    case Injection::PowerLoss: return FaultCode::PowerLoss;
    default: return FaultCode::None;
    }
}

bool run_spin(Engine& engine, SimulatedWheel& wheel, Injection injection, TickMs& now) {
    if (!require_ok(wheel.arm_fault(wheel_fault(injection)), "Arm wheel")) return false;
    Error error = engine.start_spin(now);
    if (error == Error::Ok && injection == Injection::PowerLoss) {
        now += 250;
        RecoveryReport report{};
        error = engine.recover(report); // Lose volatile engine state; retain the RAM journal.
        if (!require_ok(error, "Power-loss recovery")) return false;
        if (report.action != RecoveryAction::Refunded) {
            std::cerr << "Power-loss demo did not refund the pending round.\n";
            return false;
        }
    } else if (error == Error::Ok) {
        const EngineConfig& config = engine.snapshot().config;
        if (injection == Injection::NoSpin) now += config.no_spin_timeout_ms;
        else if (injection == Injection::BallTimeout) now += config.ball_timeout_ms;
        else now += 1000; // SimulatedWheel's default result capture delay.
        error = engine.tick(now);
    }
    // WheelError can mean the engine successfully voided and refunded. Never
    // swallow storage errors or uncertain commits just because a fault was armed.
    if ((error == Error::Ok || error == Error::WheelError) &&
        engine.status() == RuntimeStatus::Ready && engine.snapshot().round.state == RoundState::Fault) {
        if (injection != Injection::None && engine.snapshot().round.fault == expected_fault(injection)) return true;
        std::cerr << "Unexpected fault: " << fault_name(engine.snapshot().round.fault) << '\n';
        return false;
    }
    if (!require_ok(error, "Spin")) return false;
    if (injection != Injection::None || engine.snapshot().round.state != RoundState::Result) {
        std::cerr << "Spin did not reach its expected result or fault.\n";
        return false;
    }
    return require_ok(engine.settle(), "Settle");
}

struct SeatAmounts { Credits staked{0}; Credits returned{0}; Credits refunded{0}; };

bool print_round(const Engine& engine, IStorage& storage, Sequence first_event,
                 const std::array<Credits, kSeatCount>& before) {
    const Snapshot& snapshot = engine.snapshot();
    const Round& round = snapshot.round;
    std::array<SeatAmounts, kSeatCount> amounts{};
    std::cout << "\nROUND " << round.id << " | events #" << first_event << "..#" << snapshot.last_sequence << '\n';
    std::cout << "  states: Idle";
    for (Sequence sequence = first_event;; ++sequence) {
        Event event{};
        if (!require_ok(storage.read_event(sequence, event), "Read round event")) return false;
        if (event.kind == EventKind::StateChanged) std::cout << " > " << state_name(event.to);
        if (event.kind == EventKind::BetAccepted || event.kind == EventKind::BetSettled || event.kind == EventKind::BetRefunded) {
            if (event.seat >= kSeatCount) return false;
            SeatAmounts& seat = amounts[event.seat];
            Credits& total = event.kind == EventKind::BetAccepted ? seat.staked
                : event.kind == EventKind::BetSettled ? seat.returned : seat.refunded;
            if (!require_ok(checked_add(total, event.amount, total), "Sum event credits")) return false;
        }
        if (sequence == snapshot.last_sequence) break;
    }
    std::cout << "\n  lucky:";
    if (!snapshot.config.lucky.enabled) std::cout << " off";
    else for (std::size_t i = 0; i < round.lucky.count; ++i) {
        std::cout << ' ' << static_cast<unsigned>(round.lucky.entries[i].number.value)
                  << '@' << round.lucky.entries[i].gross_multiplier << 'x';
    }
    std::cout << '\n';
    if (round.resolution == Resolution::Paid) {
        std::cout << "  result: number=" << static_cast<unsigned>(round.result_number.value)
                  << " pocket=" << static_cast<unsigned>(round.result_index.value) << '\n';
    } else {
        std::cout << "  VOID: " << fault_name(round.fault) << " | all accepted stakes refunded\n";
    }
    for (SeatId seat = 0; seat < kSeatCount; ++seat) {
        std::cout << "  seat " << static_cast<unsigned>(seat) << " (" << bot_name(seat) << "): ";
        const AcceptedBet* bet = nullptr;
        for (std::size_t i = 0; i < round.bet_count; ++i) {
            if (round.bets[i].request.seat == seat) bet = &round.bets[i];
        }
        if (bet == nullptr) std::cout << "skipped (no affordable stake)";
        else {
            const BetSelection& selection = bet->request.selection;
            std::cout << bet_name(selection.kind) << '[';
            for (std::size_t i = 0; i < selection.count; ++i) {
                if (i != 0) std::cout << ',';
                std::cout << static_cast<unsigned>(selection.numbers[i].value);
            }
            std::cout << ']';
        }
        Credits net = 0;
        if (!require_ok(checked_subtract(snapshot.seats[seat].credits, before[seat], net), "Seat net")) return false;
        std::cout << " stake=" << amounts[seat].staked << " gross=" << amounts[seat].returned
                  << " refund=" << amounts[seat].refunded << " net=" << net
                  << " credits=" << snapshot.seats[seat].credits << '\n';
    }
    std::cout << "  house=" << snapshot.house_result << " | conservation=OK\n";
    return static_cast<bool>(std::cout);
}

bool print_final(const Engine& engine, const MemoryJournal& journal,
                 std::uint32_t settled, std::uint32_t voided) {
    std::cout << "\nFINAL METERS | settled=" << settled << " voided=" << voided << '\n';
    std::cout << "seat credits credits_in credits_out games_played games_won\n";
    Credits total = 0;
    for (SeatId i = 0; i < kSeatCount; ++i) {
        const Seat& seat = engine.snapshot().seats[i];
        if (!require_ok(checked_add(total, seat.credits, total), "Final credits")) return false;
        std::cout << static_cast<unsigned>(i) << ' ' << seat.credits << ' ' << seat.meters.credits_in
                  << ' ' << seat.meters.credits_out << ' ' << seat.meters.games_played
                  << ' ' << seat.meters.games_won << '\n';
    }
    const Credits house = engine.snapshot().house_result;
    Credits conserved = 0;
    if (!require_ok(checked_add(total, house, conserved), "Final conservation")) return false;
    std::cout << "seat_total=" << total << " house=" << house << " combined=" << conserved
              << " initial=" << kInitialCredits * kSeatCount << '\n';
    std::cout << "journal_events=" << journal.event_count() << " | final_state="
              << state_name(engine.snapshot().round.state) << " | conservation=OK\n";
    return static_cast<bool>(std::cout);
}

} // namespace

int main(int argc, char** argv) {
    Options options{};
    if (!parse_options(argc, argv, options)) {
        std::cerr << "Invalid or duplicate option.\n";
        usage(std::cerr);
        return 2;
    }
    if (options.help) { usage(std::cout); return 0; }

    // 9 initialization events; at most 26 per round including eight placements,
    // Lucky reveal, all payouts, one recovery event and the final Idle transition.
    // The parser's 10,000-round bound also bounds this startup allocation.
    const std::size_t event_capacity = 9 + std::size_t{26} * options.rounds;
    std::unique_ptr<Event[]> events(new (std::nothrow) Event[event_capacity]);
    if (!events) { std::cerr << "Cannot allocate the bounded RAM journal.\n"; return 1; }
    MemoryJournal journal(events.get(), event_capacity);
    if (!require_ok(journal.status(), "Create journal")) return 1;
    SeededRandom wheel_random(options.seed);
    SeededRandom lucky_random(options.seed ^ kLuckySeedMask);
    SeededRandom bot_random(options.seed ^ kBotSeedMask);
    SimulatedWheel wheel(wheel_random);
    Engine engine(wheel, journal, lucky_random);
    EngineConfig config{};
    config.lucky.enabled = options.lucky;
    std::array<Credits, kSeatCount> initial{};
    initial.fill(kInitialCredits);
    if (!require_ok(engine.initialize(config, initial), "Initialize")) return 1;
    std::cout << "mini-etg-roulette | educational simulation | no real money\n"
              << "rounds=" << options.rounds << " lucky=" << (options.lucky ? "on" : "off")
              << " wheel_seed=" << options.seed << " lucky_seed=" << (options.seed ^ kLuckySeedMask)
              << " bot_seed=" << (options.seed ^ kBotSeedMask) << '\n'
              << "Virtual time; RAM journal; injection applies only to round 1.\n";

    std::uint32_t settled = 0, voided = 0;
    TickMs now = 0;
    for (std::uint32_t attempt = 0; attempt < options.rounds; ++attempt) {
        std::array<Credits, kSeatCount> before{};
        for (std::size_t seat = 0; seat < kSeatCount; ++seat) before[seat] = engine.snapshot().seats[seat].credits;
        const Sequence first_event = engine.snapshot().last_sequence + 1;
        if (!require_ok(engine.open_bets(), "Open bets")) return 1;
        for (SeatId seat = 0; seat < kSeatCount; ++seat) {
            BotDecision decision{};
            if (!require_ok(choose_bot_bet(engine.snapshot(), seat, bot_random, decision), "Bot decision")) return 1;
            if (!decision.place) continue;
            BetId accepted_id = 0;
            if (!require_ok(engine.place_bet(decision.request, accepted_id), "Place bot bet")) return 1;
        }
        if (!require_ok(engine.begin_closing(), "Begin closing") ||
            !require_ok(engine.close_bets(), "Close bets")) return 1;
        const Injection injection = attempt == 0 ? options.injection : Injection::None;
        if (!run_spin(engine, wheel, injection, now)) return 1;
        if (engine.snapshot().round.state == RoundState::Payout) {
            ++settled;
            if (!require_ok(engine.finish_round(), "Finish round")) return 1;
        } else {
            ++voided;
            if (!require_ok(engine.acknowledge_fault(), "Acknowledge fault")) return 1;
        }
        if (!require_ok(check_accounting(engine.snapshot()), "Round conservation") ||
            !print_round(engine, journal, first_event, before)) return 1;
        now += 1000;
    }
    return print_final(engine, journal, settled, voided) ? 0 : 1;
}
