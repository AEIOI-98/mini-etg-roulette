# Phase 1 guide: understand and run the engine

Phase 1 is a portable C++17 engine with native tests. The tests drive complete
rounds, supply wheel observations, inject storage failures and recreate the
engine to exercise recovery. [Phase 2](MATH.md) now adds optional multipliers;
the example here keeps standard mode. The [eight-bot CLI](PHASE_3_GUIDE.md) is now
implemented. The browser, animated wheel and ESP32 firmware belong to later phases.

From the repository directory, after the README setup:

```sh
source .venv/bin/activate
pio test -e native
```

`core/` is the game library. `test/support/` contains the controlled wheel and
in-memory storage used by the tests. These adapters are useful for understanding
the engine without requiring physical hardware.

## A complete scripted round

This desktop example uses the actual public API and test adapters. The wheel is
scripted to report pocket index 23, which contains printed number 1. Choosing
that fixture is a test input through `IWheel`; the engine still validates and
settles the result itself.

```cpp
#include "mini_etg/engine.hpp"
#include "fake_wheel.hpp"
#include "memory_storage.hpp"
#include <array>
#include <cstdlib>
#include <iostream>

using namespace mini_etg;
using mini_etg::test::FakeWheel;
using mini_etg::test::MemoryStorage;

void require_ok(Error error) {
    if (error != Error::Ok) {
        std::cerr << "Engine error: " << static_cast<int>(error) << '\n';
        std::exit(1);
    }
}

int main() {
    // Fixed buffers use static storage rather than a small embedded task stack.
    static MemoryStorage storage;
    static FakeWheel wheel(&storage);
    static Engine engine(wheel, storage);

    std::array<Credits, kSeatCount> funding{};
    funding[0] = 100;
    require_ok(engine.initialize(EngineConfig{}, funding));
    require_ok(engine.open_bets());

    const Number numbers[] = {{1}};
    BetSelection selection{};
    require_ok(make_inside_bet(BetKind::Straight, numbers, 1, selection));
    BetId id{};
    require_ok(engine.place_bet(BetRequest{0, selection, 5}, id));
    require_ok(engine.begin_closing());
    require_ok(engine.close_bets());
    require_ok(engine.start_spin(1000));

    wheel.motion(1100);
    wheel.ball(1500, 23);
    require_ok(engine.tick(1600));
    require_ok(engine.settle());
    require_ok(engine.finish_round());

    const Snapshot& state = engine.snapshot();
    std::cout << "Seat 0: " << state.seats[0].credits
              << ", house: " << state.house_result << '\n';
}
```

Expected output: `Seat 0: 275, house: -175`.
The five-credit stake first reduces available credits from 100 to 95. A 35:1
profit payout returns 180 credits including the stake, producing 275. The house
has lost 175 credits, so `275 + (-175) == 100`. Credit meters and event records
are updated by the same engine commands.

The example needs `core/include`, `test/support`, and the `core/src/*.cpp` files
when compiled. The PlatformIO tests already arrange those include/library paths.
The example's printing and process-exit helper are desktop application code;
the core returns explicit errors and does not print or throw exceptions.

## What the C++ means for a JavaScript developer

| Construct | Meaning here |
| --- | --- |
| `.hpp` and `.cpp` | A header declares the available types/functions; the source file defines their behavior. Compilation and linking join them into an executable. |
| `Credits` | An alias for signed `int64_t`. All money is exact integer credits within a checked range. The house result can be negative; seat balances cannot. |
| `struct` | A group of named fields, similar to a typed object. `Number` and `PocketIndex` are distinct types so sensor position and printed number cannot be mixed accidentally. |
| `enum class` | A named set of typed values, such as `RoundState::Spinning` or `Error::LimitExceeded`. Values from external data still need validation. |
| `std::array<T, N>` | Exactly `N` elements stored inside the object. It does not grow or allocate storage during a round. |
| `IWheel&`, `IStorage&` | Borrowed references. The engine uses existing adapters and does not own/delete them. The adapters must remain alive while the engine uses them. |
| Virtual methods | The engine calls the adapter's implementation through its interface. A test wheel can later be replaced with a sensor wheel without rewriting the game. |
| `const Snapshot&` | A read-only view of the engine's state. It avoids a large copy; later commands update what the view shows. Check runtime status after failed operations. |
| `Error` and output parameters | A function returns a status and writes its output on success. For example, `place_bet()` writes a bet ID only after its commit succeeds. |
| `noexcept` and `= delete` | Functions promise not to throw; errors travel through return values. Engine copying is forbidden so a copied controller cannot accidentally share one wheel/ledger. |

## Why each command is a transaction

The engine first builds a proposed snapshot and its events in a fixed staging
buffer. It checks the accounting and the journal's remaining capacity, then calls
`IStorage::commit()`. Only a successful commit becomes the visible in-memory
state. The storage adapter must save the snapshot and event batch together.

Event sequences increase across rounds and reboots. Rejected commands are logged
when storage can accept them. The engine reserves enough space and counter values
to pay or refund an accepted round even if ordinary logging reaches its limit.
An uncertain commit stops progression until recovery determines what was saved.

`BetsOpen` is the only state that accepts bets. `BetsClosing` closes that window.
The engine receives results only through `IWheel`; callers cannot set a result
or assign the round state directly. Observations carry capture times, so a timely
sensor reading is not discarded merely because the next poll happens late.

The engine records `Result` before paying or displaying a targeted reveal. A
future browser/OLED animation will use that recorded pocket. Animation timing,
skipped frames or a reload must never select a different winner.

## What the crash tests actually prove

`MemoryStorage` is kept alive while an `Engine` object is destroyed and recreated.
That models the engine losing its RAM while its storage survives. It does **not**
prove persistence across a process exit, operating-system crash or physical power
loss: this adapter itself lives in RAM. Later NVS/browser adapters need their own
storage integration tests.

The adapter offers two one-shot failpoints. `fail_next_before_commit()` leaves the
previous snapshot/events intact and returns `StorageIoError`.
`fail_next_after_commit()` saves the complete new snapshot/events but returns
`CommitOutcomeUnknown`, modelling loss of acknowledgement after a successful write.
Tests then create another engine using that same adapter and call `recover()`.

| Last committed state | Recovery behavior |
| --- | --- |
| Bets open/closing/closed or spinning | Refund outstanding stakes once and enter Fault. |
| Result | Complete the recorded result's payout once and enter Payout. |
| Payout | Preserve the already-paid balances and meters. |
| Fault or Idle | Preserve the resolved ledger; do not repeat a refund or payout. |

Call `initialize()` only for genuinely empty storage. Corrupt storage must remain
an error, not become an excuse to reset credits. Repeated recovery is tested for
unchanged money and meters after the first resolution.

## Review boundary

Phase 1 implements base roulette, eight seats, limits, accounting, fault handling,
an append-only event log and the native recovery model. Phase 2 now adds multiplier
math; Phase 3 adds the simulator; Phase 3B adds the browser; Phase 4 adds hardware
adapters; Phase 5 adds the remaining presentation/CI polish. Undo/clear/repeat are
planned browser-phase extensions, not current engine commands. The original
Phase 1 review gate was followed; current work stops at the Phase 3 review boundary.
