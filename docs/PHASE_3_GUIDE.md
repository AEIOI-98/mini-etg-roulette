# Phase 3: run and understand the desktop simulator

The simulator runs the same C++ engine tested in Phases 1–2. Eight bots submit
bet requests, a simulated wheel supplies observations, and the engine records
results, settles payouts or refunds faults. The CLI prints what actually happened.
It is an educational demo with integer credits and no real money.

## Run it

From the repository directory, with Python 3 and a C++17 compiler installed:

```sh
python3 sim/run.py --seed 42 --rounds 3
python3 sim/run.py --seed 42 --rounds 3 --lucky-mode
python3 sim/run.py --seed 42 --rounds 3 --lucky-mode --inject-fault=ball-timeout
python3 sim/run.py --seed 42 --rounds 3 --inject-fault=power-loss
```

The Python wrapper uses only the standard library. It builds the core, shared
simulator code and CLI into `sim/.build/roulette_sim`, then forwards the arguments
to that executable. It does not implement any game logic. Set `CXX` to choose a
compiler. `python3 sim/run.py --build-only` only builds the program.

For an explanation of the printed output, see the
[saved Lucky-mode fault demo](../sim/examples/lucky-fault-demo.txt).

| Flag | Default | Accepted values or effect |
| --- | --- | --- |
| `--seed` | `20261111` | Integer from 0 through 4294967295. |
| `--rounds` | `10` | Integer from 1 through 10000, including voided attempts. |
| `--lucky-mode` | Off | Enable the Phase 2 default multiplier configuration. |
| `--inject-fault=TYPE` | `none` | Inject one fault into round 1; subsequent rounds run normally. |
| `--help`, `-h` | Off | Print usage without starting a session. |

Value options accept either `--seed 42` or `--seed=42` form. Unknown flags,
duplicate options, missing values and out-of-range numbers fail with exit code 2.
Successful simulations return 0; runtime/build errors return 1. An intentionally
injected, successfully refunded fault is part of a successful demo.

## The eight strategies

Each seat starts with 1000 credits and proposes at most one bet per round.
Strategies choose bets; they do not change balances or decide winners.

| Seat | Strategy | Requested stake |
| ---: | --- | ---: |
| 0 | Straight on zero every round | 5 |
| 1 | Uniformly chosen straight | 5 |
| 2 | Red every round | 10 |
| 3 | Odd, then even, alternating | 10 |
| 4 | Dozens 1, 2, 3, repeating | 10 |
| 5 | Columns 1, 2, 3, repeating | 10 |
| 6 | Uniformly chosen horizontal split from the 24 layout edges | 5 |
| 7 | Street, corner, six line, repeating while moving across the layout | 5 |

The bot reduces its requested stake to fit available credits and table limits.
If that amount is below the minimum, it skips. It also skips a seat that already
has a bet, so repeated calls cannot add duplicate strategy placements. There are
no automatic credit top-ups, loans or stake increases after losses. A bankrupt
seat can remain inactive while the other seats continue.

The bot uses the core's selection builders; `Engine::place_bet()` still validates
the resulting request, limits and payout headroom. This keeps the authoritative
rules in one place.

## Virtual time and the wheel boundary

There are no real-time sleeps. The CLI passes a millisecond counter into the
engine and advances it to the next relevant observation or deadline. A simulated
wheel normally reports motion at 100 ms after start and a settled ball at 1000 ms.
A delayed poll retains those capture times rather than replacing them with poll
time. The adapter also handles uint32 clock rollover within the core's time limits.

`IWheel` is like a typed JavaScript object with required methods: `start`, `poll`
and `stop`. The engine calls that interface without knowing whether the adapter
is simulated or sensor-based. The simulated adapter supplies a physical pocket
index; the core maps it to a printed number and settles bets. The CLI never calls
a result setter or calculates a winning payout for display.

Three separate MT19937 instances provide deterministic streams:

- Wheel seed: the supplied `--seed`.
- Lucky seed: `seed XOR 0x9e3779b9`.
- Bot seed: `seed XOR 0x85ebca6b`.

All bounded choices use the core's rejection-sampling helper. Switching Lucky
mode cannot consume the wheel's random words. Each valid wheel start attempt,
including a faulted attempt, consumes one bounded pocket draw. Thus later wheel
results stay aligned when a fault is injected into the first round. Separate
streams support repeatable debugging; MT19937 is not a cryptographic generator.

## Fault demonstrations

| Injection | What the simulator does | Engine outcome |
| --- | --- | --- |
| `no-spin` | Suppress all observations through the motion deadline. | Fault `NoSpin`; refund stakes. |
| `ball-timeout` | Report motion but never report the ball. | Fault `BallTimeout`; refund stakes. |
| `invalid-sensor` | Report a settled pocket index outside 0–36. | Fault `InvalidSensor`; refund stakes. |
| `start-failure` | Return an error from the attempted wheel start. | Fault `WheelStartFailed`; refund stakes. |
| `power-loss` | Invoke recovery after start, before a result is recorded. | Fault `PowerLoss`; refund stakes. |

The power-loss command calls `recover()` against the surviving RAM journal. It
resets the engine's volatile wheel/timing state and loads its committed snapshot;
it does **not** terminate/restart the process or test physical disk durability.
The earlier native recovery tests also recreate engine objects and exercise
failures on both sides of commits.

After a fault, the CLI checks that the engine is Ready in the expected Fault state,
then acknowledges it and returns to Idle. A storage error or uncertain commit is
not treated as a successful injected fault. After a recorded result, settlement
remains compulsory; a reveal cannot be replaced with a refund.

## Read the output

Each round prints its contiguous event sequence range, actual state transitions,
saved Lucky Numbers, result or void reason, and all eight seats. The amounts mean:

- `stake`: credits accepted into that round's escrow.
- `gross`: total payout, including the original stake when a bet wins.
- `refund`: stakes returned because the round was voided.
- `net`: ending balance minus the balance before that round.

Stake, payout and refund totals are read from committed events. They are not
recomputed from a second set of roulette rules. The final table prints credits,
external credits in/out and games played/won from the engine's meters. Voided
rounds do not increase played/won. After each round the engine's accounting check
verifies conservation; at the end seat balances plus house result total 8000.

## Memory ownership and the review boundary

The CLI allocates `9 + 26 × rounds` event slots once before initialization: nine
for initial funding and up to 26 per round, covering eight accepted bets, payouts,
Lucky reveal, state changes, a recovery event and return to Idle. The adapter
preserves the engine's terminal event/counter reserve on every commit.

`std::unique_ptr<Event[]>` owns that array and automatically releases it when the
session ends, including early error returns. `new (std::nothrow)` returns a null
pointer if allocation fails, so the CLI can report an error before initializing.
`MemoryJournal` only borrows the buffer, which must outlive it. Core and shared
round processing use fixed storage with no heap allocation; console formatting
is outside that guarantee. The journal does not survive process exit.

Run validation with `pio test -e native` and `python3 sim/test_cli.py`. The native
suite includes 123 cases covering the earlier engine and new shared components.
All nine CLI checks also pass, exercising complete executable runs and output
contracts, including the maximum 10,000-round run.

The reusable bots, RNG adapter, wheel and journal live in `sim/shared/`; argument
parsing and printing live in `sim/src/`. Phase 3B will use the same C++ simulation
components in WASM, with browser storage and rendering adapters. It will not put
the CLI parser or game decisions in TypeScript. Phase 3 stops here for review
before the browser terminal, deployment and later hardware work.
