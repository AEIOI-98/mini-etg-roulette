#include "mini_etg/engine.hpp"

#include <limits>

namespace mini_etg {
namespace {

constexpr TickMs kHalfClockRange = TickMs{1} << 31;

bool pre_result(RoundState state) noexcept {
    return state == RoundState::BetsOpen || state == RoundState::BetsClosing ||
           state == RoundState::BetsClosed || state == RoundState::Spinning;
}

bool valid_fault(FaultCode fault) noexcept {
    return fault >= FaultCode::NoSpin && fault <= FaultCode::OperatorInjected;
}

bool same_selection(const BetSelection& a, const BetSelection& b) noexcept {
    if (a.kind != b.kind || a.count != b.count) return false;
    for (std::size_t i = 0; i < a.count; ++i) {
        if (a.numbers[i].value != b.numbers[i].value) return false;
    }
    return true;
}

Error append(JournalCommit& transaction, Event event) noexcept {
    if (transaction.events.count >= kMaxEventsPerCommit) return Error::StorageFull;
    transaction.events.entries[transaction.events.count++] = event;
    return Error::Ok;
}

Error change_state(JournalCommit& transaction, RoundState to, Command command) noexcept {
    Event event{};
    event.kind = EventKind::StateChanged;
    event.command = command;
    event.from = transaction.next.round.state;
    event.to = to;
    event.fault = transaction.next.round.fault;
    const Error error = append(transaction, event);
    if (error == Error::Ok) transaction.next.round.state = to;
    return error;
}

Error opening_total(const Snapshot& snapshot, Credits& out) noexcept {
    Credits total = 0;
    for (const Seat& seat : snapshot.seats) {
        const Error error = checked_add(total, seat.credits, total);
        if (error != Error::Ok) return error;
    }
    return checked_add(total, snapshot.house_result, out);
}

// Small fixed arrays, reused for each of the 37 possible outcomes.
struct Settlement {
    std::array<Credits, kSeatCount> stakes{};
    std::array<Credits, kSeatCount> returns{};
    std::array<Credits, kSeatCount> credits{};
    Credits house{0};
};

enum class ReturnCase { Recorded, Base, Maximum };

Error settlement_for(const Snapshot& snapshot, Number result, Settlement& out,
                     ReturnCase return_case = ReturnCase::Recorded) noexcept {
    Settlement next{};
    Credits total_returns = 0;
    for (std::size_t i = 0; i < snapshot.round.bet_count; ++i) {
        const AcceptedBet& bet = snapshot.round.bets[i];
        const SeatId seat = bet.request.seat;
        Credits returned = 0;
        Error error = Error::Ok;
        if (return_case == ReturnCase::Base) {
            error = base_gross_return(bet, result, snapshot.config.lucky, returned);
        } else if (return_case == ReturnCase::Maximum) {
            error = maximum_gross_return(bet, result, snapshot.config.lucky, returned);
        } else {
            error = lucky_gross_return(bet, result, snapshot.config.lucky,
                                      snapshot.round.lucky, returned);
        }
        if (error != Error::Ok) return error;
        error = checked_add(next.stakes[seat], bet.request.stake, next.stakes[seat]);
        if (error != Error::Ok) return error;
        error = checked_add(next.returns[seat], returned, next.returns[seat]);
        if (error != Error::Ok) return error;
        error = checked_add(total_returns, returned, total_returns);
        if (error != Error::Ok) return error;
    }
    Credits change = 0;
    Error error = checked_subtract(snapshot.round.escrow, total_returns, change);
    if (error != Error::Ok) return error;
    error = checked_add(snapshot.house_result, change, next.house);
    if (error != Error::Ok) return error;
    Credits total_credits = 0;
    for (std::size_t i = 0; i < kSeatCount; ++i) {
        error = checked_add(snapshot.seats[i].credits, next.returns[i], next.credits[i]);
        if (error != Error::Ok) return error;
        error = checked_add(total_credits, next.credits[i], total_credits);
        if (error != Error::Ok) return error;
        const Meters& meters = snapshot.seats[i].meters;
        if (next.stakes[i] > 0 && meters.games_played == std::numeric_limits<std::uint64_t>::max()) {
            return Error::CounterExhausted;
        }
        if (next.returns[i] > next.stakes[i] &&
            meters.games_won == std::numeric_limits<std::uint64_t>::max()) {
            return Error::CounterExhausted;
        }
    }
    Credits conserved = 0;
    error = checked_add(total_credits, next.house, conserved);
    if (error != Error::Ok) return error;
    if (conserved != snapshot.round.opening_total) return Error::AccountingMismatch;
    out = next;
    return Error::Ok;
}

Error all_results_fit(const Snapshot& snapshot) noexcept {
    Settlement settlement{};
    for (std::uint8_t number = 0; number < kPocketCount; ++number) {
        Error error = settlement_for(snapshot, Number{number}, settlement, ReturnCase::Base);
        if (error != Error::Ok) return error;
        if (snapshot.config.lucky.enabled) {
            // A given result gives all winning straights the same multiplier.
            // Checking both endpoints covers seat/house ranges and win meters.
            error = settlement_for(snapshot, Number{number}, settlement, ReturnCase::Maximum);
            if (error != Error::Ok) return error;
        }
    }
    return Error::Ok;
}

// Stage one placement. Single bets and a repeated pattern use exactly the same
// credit, geometry, limit and capacity checks. The caller checks all possible
// settlements once the complete proposed edit is ready.
Error stage_bet(JournalCommit& transaction, const BetRequest& request, Command command) noexcept {
    Snapshot& next = transaction.next;
    if (request.seat >= kSeatCount) return Error::InvalidSeat;
    if (request.stake <= 0) return Error::InvalidAmount;
    BetSelection selection{};
    Error error = canonicalize(request.selection, selection);
    if (error != Error::Ok) return error;
    const TableLimits& limits = next.config.limits;
    if (request.stake < limits.min_per_bet || request.stake > limits.max_per_bet) return Error::LimitExceeded;
    if (request.stake > next.seats[request.seat].credits) return Error::InsufficientCredits;
    Credits seat_stake = request.stake;
    Credits selection_stake = request.stake;
    std::size_t seat_bets = 0;
    for (std::size_t i = 0; i < next.round.bet_count; ++i) {
        const BetRequest& previous = next.round.bets[i].request;
        if (previous.seat != request.seat) continue;
        ++seat_bets;
        error = checked_add(seat_stake, previous.stake, seat_stake);
        if (error != Error::Ok) return error;
        if (same_selection(previous.selection, selection)) {
            error = checked_add(selection_stake, previous.stake, selection_stake);
            if (error != Error::Ok) return error;
        }
    }
    if (seat_bets >= kMaxBetsPerSeat || next.round.bet_count >= kMaxBets) return Error::BetCapacityReached;
    if (seat_stake > limits.max_total_per_seat || selection_stake > limits.max_per_bet) return Error::LimitExceeded;
    if (next.next_bet_id == std::numeric_limits<BetId>::max()) return Error::CounterExhausted;

    AcceptedBet& bet = next.round.bets[next.round.bet_count];
    bet.id = next.next_bet_id++;
    bet.request = {request.seat, selection, request.stake};
    error = base_profit_odds(selection.kind, next.config.lucky, bet.profit_odds);
    if (error != Error::Ok) return error;
    ++next.round.bet_count;
    Credits& credits = next.seats[request.seat].credits;
    error = checked_subtract(credits, request.stake, credits);
    if (error == Error::Ok) error = checked_add(next.round.escrow, request.stake, next.round.escrow);
    if (error != Error::Ok) return error;

    Event event{};
    event.kind = EventKind::BetAccepted;
    event.command = command;
    event.seat = request.seat;
    event.bet_id = bet.id;
    event.selection = selection;
    event.profit_odds = bet.profit_odds;
    event.amount = request.stake;
    return append(transaction, event);
}

} // namespace

Engine::Engine(IWheel& wheel, IStorage& storage) noexcept : wheel_(wheel), storage_(storage) {}
Engine::Engine(IWheel& wheel, IStorage& storage, IRandom& random) noexcept
    : wheel_(wheel), storage_(storage), random_(&random) {}

RuntimeStatus Engine::status() const noexcept { return status_; }
const Snapshot& Engine::snapshot() const noexcept { return current_; }

void Engine::begin_commit() noexcept {
    pending_.next = current_;
    pending_.events.count = 0;
}

Error Engine::publish() noexcept {
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    if (pending_.events.count == 0 || pending_.events.count > kMaxEventsPerCommit) {
        status_ = RuntimeStatus::RecoveryRequired;
        return Error::AccountingMismatch;
    }
    if (current_.revision == maximum || maximum - current_.last_sequence < pending_.events.count) {
        status_ = RuntimeStatus::RecoveryRequired;
        wheel_.stop();
        return Error::CounterExhausted;
    }
    pending_.next.revision = current_.revision + 1;
    pending_.next.last_sequence = current_.last_sequence + pending_.events.count;
    for (std::size_t i = 0; i < pending_.events.count; ++i) {
        Event& event = pending_.events.entries[i];
        event.sequence = current_.last_sequence + i + 1;
        event.revision = pending_.next.revision;
        event.round_id = pending_.next.round.id;
    }
    ResolutionReserve reserve{};
    Error error = required_resolution_reserve(pending_.next, reserve);
    if (error == Error::Ok &&
        (maximum - pending_.next.revision < reserve.revisions ||
         maximum - pending_.next.last_sequence < reserve.event_sequences)) {
        error = Error::CounterExhausted;
    }
    if (error == Error::Ok) error = validate_snapshot(pending_.next);
    if (error == Error::Ok) error = storage_.commit(pending_);
    if (error != Error::Ok) {
        status_ = RuntimeStatus::RecoveryRequired;
        wheel_.stop();
        return error;
    }
    // Durable storage is the authority. Publish RAM only after commit succeeds.
    current_ = pending_.next;
    status_ = RuntimeStatus::Ready;
    return Error::Ok;
}

Error Engine::reject(Command command, Error reason) noexcept {
    if (status_ != RuntimeStatus::Ready) return Error::NotReady;
    begin_commit();
    Event event{};
    event.kind = EventKind::CommandRejected;
    event.command = command;
    event.error = reason;
    event.from = current_.round.state;
    event.to = current_.round.state;
    Error error = append(pending_, event);
    if (error == Error::Ok) error = publish();
    return error == Error::Ok ? reason : error;
}

Error Engine::initialize(const EngineConfig& config,
                         const std::array<Credits, kSeatCount>& initial_credits) noexcept {
    if (status_ == RuntimeStatus::Ready) return reject(Command::Initialize, Error::AlreadyInitialized);
    wheel_.stop();
    const Error loaded = storage_.load_latest(pending_.next);
    if (loaded == Error::Ok) return Error::AlreadyInitialized;
    if (loaded != Error::NoJournal) {
        status_ = RuntimeStatus::RecoveryRequired;
        return loaded;
    }
    // Preserve the actual storage diagnostic; an ambiguous earlier write still
    // requires an explicit recovery even if the store now reports empty.
    if (status_ == RuntimeStatus::RecoveryRequired) return Error::NotReady;
    Error error = validate_config(config);
    if (error != Error::Ok) return error;
    if (config.lucky.enabled && random_ == nullptr) return Error::InvalidConfig;
    current_ = Snapshot{};
    begin_commit();
    pending_.next.config = config;
    Event initialized{};
    initialized.kind = EventKind::Initialized;
    initialized.command = Command::Initialize;
    error = append(pending_, initialized);
    if (error != Error::Ok) return error;
    for (std::size_t i = 0; i < kSeatCount; ++i) {
        if (initial_credits[i] < 0) return Error::InvalidAmount;
        pending_.next.seats[i].credits = initial_credits[i];
        pending_.next.seats[i].meters.credits_in = initial_credits[i];
        if (initial_credits[i] == 0) continue;
        Event event{};
        event.kind = EventKind::CreditLoaded;
        event.command = Command::Initialize;
        event.seat = static_cast<SeatId>(i);
        event.amount = initial_credits[i];
        error = append(pending_, event);
        if (error != Error::Ok) return error;
    }
    error = check_accounting(pending_.next);
    return error == Error::Ok ? publish() : error;
}

Error Engine::recover(RecoveryReport& out) noexcept {
    wheel_.stop();
    status_ = RuntimeStatus::RecoveryRequired;
    Error error = storage_.load_latest(pending_.next);
    if (error != Error::Ok) {
        if (error == Error::NoJournal) status_ = RuntimeStatus::Uninitialized;
        return error;
    }
    error = validate_snapshot(pending_.next);
    if (error != Error::Ok) return error;
    current_ = pending_.next;
    status_ = RuntimeStatus::Ready;
    spin_started_ms_ = 0;
    last_observed_elapsed_ms_ = 0;
    motion_seen_ = false;
    RecoveryReport report{};
    report.round_id = current_.round.id;
    if (pre_result(current_.round.state)) {
        error = refund_pending_round(FaultCode::PowerLoss, Command::Recover);
        report.action = RecoveryAction::Refunded;
    } else if (current_.round.state == RoundState::Result) {
        error = settle_recorded_result(Command::Recover);
        report.action = RecoveryAction::PayoutCompleted;
    } else if (current_.round.state == RoundState::Payout) {
        report.action = RecoveryAction::AlreadyPaid;
    }
    if (error != Error::Ok) return error;
    report.restored_state = current_.round.state;
    out = report;
    return Error::Ok;
}

Error Engine::credit_in(SeatId seat, Credits amount) noexcept {
    if (status_ != RuntimeStatus::Ready) return Error::NotReady;
    if (current_.round.state != RoundState::Idle) return reject(Command::CreditIn, Error::IllegalTransition);
    if (seat >= kSeatCount) return reject(Command::CreditIn, Error::InvalidSeat);
    if (amount <= 0) return reject(Command::CreditIn, Error::InvalidAmount);
    begin_commit();
    Seat& next = pending_.next.seats[seat];
    Error error = checked_add(next.credits, amount, next.credits);
    if (error == Error::Ok) error = checked_add(next.meters.credits_in, amount, next.meters.credits_in);
    if (error == Error::Ok) error = check_accounting(pending_.next);
    if (error != Error::Ok) return reject(Command::CreditIn, error);
    Event event{};
    event.kind = EventKind::CreditLoaded;
    event.command = Command::CreditIn;
    event.seat = seat;
    event.amount = amount;
    error = append(pending_, event);
    return error == Error::Ok ? publish() : error;
}

Error Engine::credit_out(SeatId seat, Credits amount) noexcept {
    if (status_ != RuntimeStatus::Ready) return Error::NotReady;
    if (current_.round.state != RoundState::Idle) return reject(Command::CreditOut, Error::IllegalTransition);
    if (seat >= kSeatCount) return reject(Command::CreditOut, Error::InvalidSeat);
    if (amount <= 0) return reject(Command::CreditOut, Error::InvalidAmount);
    if (amount > current_.seats[seat].credits) return reject(Command::CreditOut, Error::InsufficientCredits);
    begin_commit();
    Seat& next = pending_.next.seats[seat];
    Error error = checked_subtract(next.credits, amount, next.credits);
    if (error == Error::Ok) error = checked_add(next.meters.credits_out, amount, next.meters.credits_out);
    if (error == Error::Ok) error = check_accounting(pending_.next);
    if (error != Error::Ok) return reject(Command::CreditOut, error);
    Event event{};
    event.kind = EventKind::CreditWithdrawn;
    event.command = Command::CreditOut;
    event.seat = seat;
    event.amount = amount;
    error = append(pending_, event);
    return error == Error::Ok ? publish() : error;
}

Error Engine::transition(RoundState next, Command command) noexcept {
    begin_commit();
    const Error error = change_state(pending_, next, command);
    return error == Error::Ok ? publish() : error;
}

Error Engine::open_bets() noexcept {
    if (status_ != RuntimeStatus::Ready) return Error::NotReady;
    if (current_.round.state != RoundState::Idle) return reject(Command::OpenBets, Error::IllegalTransition);
    if (current_.config.lucky.enabled && random_ == nullptr) return reject(Command::OpenBets, Error::InvalidConfig);
    if (current_.round.id == std::numeric_limits<RoundId>::max()) return reject(Command::OpenBets, Error::CounterExhausted);
    begin_commit();
    pending_.next.round = Round{};
    pending_.next.round.id = current_.round.id + 1;
    pending_.next.round.resolution = Resolution::Pending;
    Error error = opening_total(current_, pending_.next.round.opening_total);
    if (error != Error::Ok) return reject(Command::OpenBets, error);
    error = change_state(pending_, RoundState::BetsOpen, Command::OpenBets);
    return error == Error::Ok ? publish() : error;
}

Error Engine::begin_closing() noexcept {
    if (status_ != RuntimeStatus::Ready) return Error::NotReady;
    if (current_.round.state != RoundState::BetsOpen) return reject(Command::BeginClosing, Error::IllegalTransition);
    return transition(RoundState::BetsClosing, Command::BeginClosing);
}

Error Engine::close_bets() noexcept {
    if (status_ != RuntimeStatus::Ready) return Error::NotReady;
    if (current_.round.state != RoundState::BetsClosing) return reject(Command::CloseBets, Error::IllegalTransition);
    if (!current_.config.lucky.enabled) return transition(RoundState::BetsClosed, Command::CloseBets);
    if (random_ == nullptr) return reject(Command::CloseBets, Error::InvalidConfig);
    begin_commit();
    Error error = change_state(pending_, RoundState::BetsClosed, Command::CloseBets);
    if (error == Error::Ok) {
        error = draw_lucky_numbers(*random_, current_.config.lucky, pending_.next.round.lucky);
    }
    if (error != Error::Ok) return reject(Command::CloseBets, error);
    Event event{};
    event.kind = EventKind::LuckyNumbersDrawn;
    event.command = Command::CloseBets;
    event.lucky = pending_.next.round.lucky;
    error = append(pending_, event);
    return error == Error::Ok ? publish() : error;
}

Error Engine::publish_bet_edit(Command command, Error staged) noexcept {
    if (staged == Error::Ok) staged = all_results_fit(pending_.next);
    return staged == Error::Ok ? publish() : reject(command, staged);
}

Error Engine::place_bet(const BetRequest& request, BetId& accepted_id) noexcept {
    if (status_ != RuntimeStatus::Ready) return Error::NotReady;
    if (current_.round.state != RoundState::BetsOpen) return reject(Command::PlaceBet, Error::IllegalTransition);
    begin_commit();
    const Error staged = stage_bet(pending_, request, Command::PlaceBet);
    const Error error = publish_bet_edit(Command::PlaceBet, staged);
    if (error == Error::Ok) accepted_id = current_.next_bet_id - 1;
    return error;
}

Error Engine::cancel_bets(SeatId seat, Command command) noexcept {
    if (status_ != RuntimeStatus::Ready) return Error::NotReady;
    if (current_.round.state != RoundState::BetsOpen) return reject(command, Error::IllegalTransition);
    if (seat >= kSeatCount) return reject(command, Error::InvalidSeat);

    begin_commit();
    Round& round = pending_.next.round;
    std::size_t last = round.bet_count;
    while (last > 0 && round.bets[last - 1].request.seat != seat) --last;
    if (last == 0) return reject(command, Error::NoBets);

    std::size_t retained = 0;
    for (std::size_t i = 0; i < round.bet_count; ++i) {
        const AcceptedBet& bet = round.bets[i];
        if (bet.request.seat != seat || (command == Command::UndoLastBet && i != last - 1)) {
            round.bets[retained++] = bet;
            continue;
        }
        Event event{};
        event.kind = EventKind::BetCancelled;
        event.command = command;
        event.seat = seat;
        event.bet_id = bet.id;
        event.amount = bet.request.stake;
        Credits& credits = pending_.next.seats[seat].credits;
        Error error = checked_add(credits, event.amount, credits);
        if (error == Error::Ok) error = checked_subtract(round.escrow, event.amount, round.escrow);
        if (error == Error::Ok) error = append(pending_, event);
        if (error != Error::Ok) return reject(command, error);
    }
    for (std::size_t i = retained; i < round.bet_count; ++i) round.bets[i] = {};
    round.bet_count = retained;
    // A losing stake may have offset another seat's win at the arithmetic limit.
    return publish_bet_edit(command, Error::Ok);
}

Error Engine::undo_last_bet(SeatId seat) noexcept {
    return cancel_bets(seat, Command::UndoLastBet);
}

Error Engine::clear_bets(SeatId seat) noexcept {
    return cancel_bets(seat, Command::ClearBets);
}

Error Engine::repeat_last_bets(SeatId seat) noexcept {
    constexpr auto command = Command::RepeatLastBets;
    if (status_ != RuntimeStatus::Ready) return Error::NotReady;
    if (current_.round.state != RoundState::BetsOpen) return reject(command, Error::IllegalTransition);
    if (seat >= kSeatCount) return reject(command, Error::InvalidSeat);
    const BetPattern& pattern = current_.last_bets[seat];
    if (pattern.count == 0) return reject(command, Error::NoBets);

    begin_commit();
    for (std::size_t i = 0; i < pattern.count; ++i) {
        const PatternBet& bet = pattern.bets[i];
        const Error error = stage_bet(pending_, {seat, bet.selection, bet.stake}, command);
        if (error != Error::Ok) return reject(command, error);
    }
    // Only the complete pattern can be committed, so test the final portfolio.
    return publish_bet_edit(command, Error::Ok);
}

Error Engine::start_spin(TickMs now_ms) noexcept {
    if (status_ != RuntimeStatus::Ready) return Error::NotReady;
    if (current_.round.state != RoundState::BetsClosed) return reject(Command::StartSpin, Error::IllegalTransition);
    Error error = transition(RoundState::Spinning, Command::StartSpin);
    if (error != Error::Ok) return error;
    spin_started_ms_ = now_ms;
    last_observed_elapsed_ms_ = 0;
    motion_seen_ = false;
    error = wheel_.start(current_.round.id, now_ms);
    if (error == Error::Ok) return Error::Ok;
    error = refund_pending_round(FaultCode::WheelStartFailed, Command::StartSpin);
    return error == Error::Ok ? Error::WheelError : error;
}

Error Engine::tick(TickMs now_ms) noexcept {
    if (status_ != RuntimeStatus::Ready) return Error::NotReady;
    if (current_.round.state != RoundState::Spinning) return reject(Command::Tick, Error::IllegalTransition);
    const auto fault = [this](FaultCode code) noexcept {
        const Error error = refund_pending_round(code, Command::Tick);
        return error == Error::Ok ? Error::WheelError : error;
    };
    const TickMs elapsed_now = now_ms - spin_started_ms_;
    if (elapsed_now >= kHalfClockRange) return fault(FaultCode::InvalidSensor);
    const WheelBatch batch = wheel_.poll(now_ms);
    if (batch.count > kMaxWheelReportsPerPoll) return fault(FaultCode::InvalidSensor);
    // Validate the complete batch before trusting its terminal report.
    bool saw_motion = motion_seen_;
    TickMs previous_elapsed = last_observed_elapsed_ms_;
    for (std::size_t i = 0; i < batch.count; ++i) {
        const WheelReport& report = batch.reports[i];
        if (report.round_id != current_.round.id) return fault(FaultCode::StaleWheelReport);
        const TickMs elapsed = report.observed_at_ms - spin_started_ms_;
        if (elapsed >= kHalfClockRange || elapsed > elapsed_now || elapsed < previous_elapsed) return fault(FaultCode::InvalidSensor);
        previous_elapsed = elapsed;
        if (report.signal == WheelSignal::MotionDetected) {
            if (saw_motion) return fault(FaultCode::InvalidSensor);
            saw_motion = true;
        } else if (report.signal == WheelSignal::BallSettled) {
            if (!saw_motion || report.pocket.value >= kPocketCount || i + 1 != batch.count) return fault(FaultCode::InvalidSensor);
        } else if (report.signal == WheelSignal::Fault) {
            if (i + 1 != batch.count ||
                (report.fault != FaultCode::NoSpin && report.fault != FaultCode::BallTimeout &&
                 report.fault != FaultCode::InvalidSensor)) return fault(FaultCode::InvalidSensor);
        } else {
            return fault(FaultCode::InvalidSensor);
        }
    }
    for (std::size_t i = 0; i < batch.count; ++i) {
        const WheelReport& report = batch.reports[i];
        const TickMs elapsed = report.observed_at_ms - spin_started_ms_;
        if (!motion_seen_ && elapsed >= current_.config.no_spin_timeout_ms) return fault(FaultCode::NoSpin);
        if (elapsed >= current_.config.ball_timeout_ms) return fault(FaultCode::BallTimeout);
        last_observed_elapsed_ms_ = elapsed;
        if (report.signal == WheelSignal::MotionDetected) {
            motion_seen_ = true;
        } else if (report.signal == WheelSignal::Fault) {
            return fault(report.fault);
        } else {
            begin_commit();
            pending_.next.round.has_result = true;
            pending_.next.round.result_index = report.pocket;
            Error error = number_at(report.pocket, pending_.next.round.result_number);
            if (error != Error::Ok) return fault(FaultCode::InvalidSensor);
            Event event{};
            event.kind = EventKind::ResultRecorded;
            event.command = Command::Tick;
            event.pocket = report.pocket;
            event.number = pending_.next.round.result_number;
            error = append(pending_, event);
            if (error == Error::Ok) error = change_state(pending_, RoundState::Result, Command::Tick);
            if (error == Error::Ok) error = publish();
            if (error == Error::Ok) wheel_.stop();
            return error;
        }
    }
    if (!motion_seen_ && elapsed_now >= current_.config.no_spin_timeout_ms) return fault(FaultCode::NoSpin);
    if (elapsed_now >= current_.config.ball_timeout_ms) return fault(FaultCode::BallTimeout);
    return Error::Ok;
}

Error Engine::settle_recorded_result(Command command) noexcept {
    Settlement settlement{};
    Error error = settlement_for(current_, current_.round.result_number, settlement);
    if (error != Error::Ok) {
        status_ = RuntimeStatus::RecoveryRequired;
        wheel_.stop();
        return error;
    }
    begin_commit();
    // A void, an empty round, or another seat's bets must preserve this seat's pattern.
    for (std::size_t i = 0; i < kSeatCount; ++i) {
        if (settlement.stakes[i] > 0) pending_.next.last_bets[i] = {};
    }
    for (std::size_t i = 0; i < current_.round.bet_count; ++i) {
        const AcceptedBet& bet = current_.round.bets[i];
        BetPattern& pattern = pending_.next.last_bets[bet.request.seat];
        pattern.bets[pattern.count++] = {bet.request.selection, bet.request.stake};
        Event event{};
        event.kind = EventKind::BetSettled;
        event.command = command;
        event.seat = bet.request.seat;
        event.bet_id = bet.id;
        event.number = current_.round.result_number;
        event.pocket = current_.round.result_index;
        error = lucky_gross_return(bet, event.number, current_.config.lucky,
                                  current_.round.lucky, event.amount);
        if (error == Error::Ok) error = append(pending_, event);
        if (error != Error::Ok) return error;
    }
    for (std::size_t i = 0; i < kSeatCount; ++i) {
        Seat& seat = pending_.next.seats[i];
        seat.credits = settlement.credits[i];
        if (settlement.stakes[i] > 0) ++seat.meters.games_played;
        if (settlement.returns[i] > settlement.stakes[i]) ++seat.meters.games_won;
    }
    pending_.next.house_result = settlement.house;
    pending_.next.round.escrow = 0;
    pending_.next.round.resolution = Resolution::Paid;
    error = change_state(pending_, RoundState::Payout, command);
    if (error != Error::Ok) return error;
    if (command == Command::Recover) {
        Event event{};
        event.kind = EventKind::RecoveryApplied;
        event.command = command;
        event.to = RoundState::Payout;
        error = append(pending_, event);
        if (error != Error::Ok) return error;
    }
    return publish();
}

Error Engine::settle() noexcept {
    if (status_ != RuntimeStatus::Ready) return Error::NotReady;
    if (current_.round.state != RoundState::Result) return reject(Command::Settle, Error::IllegalTransition);
    return settle_recorded_result(Command::Settle);
}

Error Engine::finish_round() noexcept {
    if (status_ != RuntimeStatus::Ready) return Error::NotReady;
    if (current_.round.state != RoundState::Payout) return reject(Command::FinishRound, Error::IllegalTransition);
    return transition(RoundState::Idle, Command::FinishRound);
}

Error Engine::refund_pending_round(FaultCode code, Command command) noexcept {
    wheel_.stop();
    begin_commit();
    for (std::size_t i = 0; i < current_.round.bet_count; ++i) {
        const AcceptedBet& bet = current_.round.bets[i];
        Seat& seat = pending_.next.seats[bet.request.seat];
        Error error = checked_add(seat.credits, bet.request.stake, seat.credits);
        if (error != Error::Ok) {
            status_ = RuntimeStatus::RecoveryRequired;
            return error;
        }
        Event event{};
        event.kind = EventKind::BetRefunded;
        event.command = command;
        event.seat = bet.request.seat;
        event.bet_id = bet.id;
        event.amount = bet.request.stake;
        event.fault = code;
        error = append(pending_, event);
        if (error != Error::Ok) return error;
    }
    pending_.next.round.escrow = 0;
    pending_.next.round.resolution = Resolution::Refunded;
    pending_.next.round.fault = code;
    Error error = change_state(pending_, RoundState::Fault, command);
    if (error != Error::Ok) return error;
    if (command == Command::Recover) {
        Event event{};
        event.kind = EventKind::RecoveryApplied;
        event.command = command;
        event.fault = code;
        event.to = RoundState::Fault;
        error = append(pending_, event);
        if (error != Error::Ok) return error;
    }
    return publish();
}

Error Engine::raise_fault(FaultCode code) noexcept {
    if (status_ != RuntimeStatus::Ready) return Error::NotReady;
    if (!valid_fault(code)) return reject(Command::RaiseFault, Error::WheelError);
    if (pre_result(current_.round.state)) return refund_pending_round(code, Command::RaiseFault);
    if (current_.round.state != RoundState::Idle) return reject(Command::RaiseFault, Error::IllegalTransition);
    wheel_.stop();
    begin_commit();
    pending_.next.round = Round{};
    pending_.next.round.id = current_.round.id; // Preserve ID history even for idle faults.
    pending_.next.round.fault = code;
    const Error error = change_state(pending_, RoundState::Fault, Command::RaiseFault);
    return error == Error::Ok ? publish() : error;
}

Error Engine::acknowledge_fault() noexcept {
    if (status_ != RuntimeStatus::Ready) return Error::NotReady;
    if (current_.round.state != RoundState::Fault) return reject(Command::AcknowledgeFault, Error::IllegalTransition);
    if (current_.round.escrow != 0) return reject(Command::AcknowledgeFault, Error::AccountingMismatch);
    wheel_.stop();
    return transition(RoundState::Idle, Command::AcknowledgeFault);
}

} // namespace mini_etg
