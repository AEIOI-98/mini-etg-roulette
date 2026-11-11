#pragma once

#include "mini_etg/model.hpp"

#include <array>

namespace mini_etg {

enum class Command : std::uint8_t {
    Initialize, Recover, CreditIn, CreditOut, OpenBets, BeginClosing,
    CloseBets, PlaceBet, StartSpin, Tick, Settle, FinishRound, RaiseFault,
    AcknowledgeFault
};

enum class EventKind : std::uint8_t {
    Initialized, StateChanged, BetAccepted, BetSettled, BetRefunded,
    CreditLoaded, CreditWithdrawn, CommandRejected, ResultRecorded, RecoveryApplied,
    LuckyNumbersDrawn
};

// Flat fields keep the educational format simple; unused fields are zero/default.
// BetAccepted includes full selection, so history survives later round clearing.
struct Event {
    Sequence sequence{0};
    Revision revision{0};
    RoundId round_id{0};
    EventKind kind{EventKind::Initialized};
    Command command{Command::Initialize};
    RoundState from{RoundState::Idle};
    RoundState to{RoundState::Idle};
    Error error{Error::Ok};
    FaultCode fault{FaultCode::None};
    SeatId seat{0};
    BetId bet_id{0};
    BetSelection selection{};
    std::uint8_t profit_odds{0}; // Recorded on BetAccepted along with selection/stake.
    Credits amount{0}; // Stake, gross return, refund, or external transfer by kind.
    PocketIndex pocket{};
    Number number{};
    LuckyDraw lucky{}; // Full reveal in LuckyNumbersDrawn; retained in history.
};

// Enough for all 128 bet settlements/refunds plus transition/recovery events.
inline constexpr std::size_t kMaxEventsPerCommit = kMaxBets + 8;

struct EventBatch {
    std::array<Event, kMaxEventsPerCommit> entries{};
    std::size_t count{0};
};

} // namespace mini_etg
