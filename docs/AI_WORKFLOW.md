# AI-assisted development record

Only actions that actually occurred in this session belong here. Entries are
chronological: earlier proposal-only status statements describe that point in
the session, not the current implementation. See README for current status.

## 2026-11-11 — Phase 1 proposal

### User direction

The user supplied a five-phase portfolio brief and explicitly asked to start with
the folder structure and public C++ headers only, then wait for OK before
implementing. The brief requires native PlatformIO tests, small logical commits,
integer credits, hardware-independent interfaces, durable recovery, and clear
interview explanations.

### Proposed by AI

- A portable `core/`, tests beside it in the repository, and later simulator,
  firmware, tools and CI folders.
- Eight declaration headers for types, randomness, wheel reports, bets, model,
  events, atomic storage and guarded engine commands.
- Fixed capacity of eight seats and sixteen accepted placements per seat.
- Explicit covered-number selections and canonical repeated-exposure limits.
- Zero trios represented as Street and first-four as Corner.
- A durable ledger/event commit before publishing any change in RAM.
- Refund before durable result; complete recorded-result payouts once; retain
  resolution independently of the displayed round state.
- Meter definitions: external transfers only for credits in/out; participating
  settled rounds for games played; positive net profit for games won.

### Review that actually happened

Two AI reviewers examined domain boundaries and recovery/accounting independently,
then inspected the drafted headers. Their reviews prompted these clarifications:

- Separate physical pocket index from printed number.
- Check aggregate exposure on canonical duplicate selections.
- Require motion evidence before accepting a settled ball; reject malformed and
  stale sensor reports.
- Save credit movements and append-only events together, not in separate writes.
- Check worst-case payout arithmetic before accepting stakes.
- Protect terminal settlement/refund capacity from ordinary rejection logs.
- Do not compare the retained historical round baseline during Idle transfers.
- State that durable rejection logging cannot succeed on broken/full storage;
  return the storage failure and stop progression.

The assistant also consulted a State Library archive for wheel order and primary
roulette rules/layout material for the zero combinations. Direct references are
in the proposal. These references do not establish certification or compliance.

### Validation and current boundary

The workspace was empty and was not an existing Git repository. A C++ compiler
was found at `/usr/bin/c++`; `pio` and `platformio` were not found on PATH.

No game tests have been written or run. No runtime function bodies, PlatformIO
configuration, simulator, firmware, multiplier calculations, or serial codec
have been implemented. Phase 1 is not complete.

All eight headers passed independent C++17 syntax checks using `/usr/bin/c++`,
with `-Wall -Wextra -Werror -pedantic -fno-exceptions -fno-rtti -fsyntax-only`.
Each was included alone to check that it supplies its own required includes.
No syntax errors were found. This did not link a program or exercise game behavior.
Relative links in the three Markdown documents were also checked and passed.

The proposal was saved as one logical Git commit; implementation remains pending.

### User changes or rejections

None so far. The folder structure, public interfaces, capacities, zero-bet
classification, meter semantics and post-result fault policy await user review.

## 2026-11-11 — Bug review requested by user

The user requested a bug review without approving implementation. The assistant
and two independent AI reviewers inspected the existing headers and proposal.
They identified two design-contract issues:

- `WheelReport` has no observation timestamp, while the proposal gives timeouts
  priority over queued reports. Motion detected at 1,999 ms but polled at the
  2,000 ms deadline would incorrectly trigger NoSpin. A ball report can suffer
  the same race. Proposed correction: timestamp meaningful observations, consume
  reports that occurred before the deadline, and then evaluate the timeout.
- The storage contract reserves space for settlement/refund but does not reserve
  revision and event-sequence values. A rejection commit can use the final
  revision while stakes remain outstanding, preventing any later resolution
  commit. Proposed correction: preserve terminal counter headroom on every
  non-terminal commit, including rejection logging.

These findings follow from the declared contracts; no runtime reproduction or
new test run was claimed. Header syntax was already checked in the preceding
proposal step. The headers and proposal remain unchanged; only this workflow
record was updated. Both findings are open, and implementation still awaits OK.

## 2026-11-11 — Browser phase and later-phase additions

### User changes

The user added Phase 3B: the same C++ engine compiled to WASM with embind; a Vite
and TypeScript player terminal with seven simulator bots; a separate operator
route with meters, events, fault injection, localStorage recovery and charts;
optional hardware-only Web Serial; and GitHub Pages deployment. The user also
added OLED wheel/result rendering to Phase 4, and Monte Carlo PNG plotting plus
web/Wokwi recording slots to Phase 5.

### Planning performed

The assistant added `PHASE_3B_PROPOSAL.md`, extended the planned folder structure,
and updated README with the result-first presentation principle, planned live-link
location, bridge explanation and media slots. An AI reviewer checked the core/API
implications. The plan includes C++ undo/clear/atomic repeat, shared simulator
components, exact 64-bit bridge values, one ledger writer, engine-owned statistics,
and recovery independent of animation. The earlier two review findings remain open.

Official Emscripten, Vite, browser storage, Web Locks and serial documentation was
consulted to check the proposed bridge and hosting/persistence boundaries; links
are included in the new plan. The current request was treated as a scope addition
under the existing review gate. No game/bridge/UI/firmware implementation or
deployment was performed, and no new public header declarations were added.

The reviewer inspected the saved additions and found no coverage or consistency
blockers. Local documentation links, paired code fences and `git diff --check`
passed. No game or browser tests were run for this documentation-only change.

## 2026-11-11 — Requested fixes and another bug review

The user asked to fix the earlier findings and look for new bugs. This authorizes
corrections to the header/design proposal; the original runtime-implementation
gate remains in place. Two AI reviewers independently audited the timing/reserve
contracts and the expanded browser plan.

### Wheel timing correction

Added observation timestamps to `WheelReport` and replaced single-report polling
with a fixed two-report `WheelBatch`: first motion and one terminal observation.
The contract now consumes queued observations by their capture times before
timing out missing events at the poll time. It defines deadline equality, delayed
polling, timestamp order, rollover limits and lost-input behavior. The engine
declaration now tracks the last observed elapsed time across batches. The original
timing finding is corrected in the proposal; runtime verification remains pending.

### Journal counter correction

Added `ResolutionReserve` and a declaration for the shared
`required_resolution_reserve()` calculation. Every commit now preserves enough
revision and event-sequence values for settlement/refund and return to Idle,
including a possible recovery event. Rejection logging follows the same rule.
The proposal gives exact Phase 1 budgets by state and uses subtraction-based
overflow-safe checks. Exhaustion refuses the ordinary commit without attempting
another rejection log, leaving the reserved resolution path available. The
earlier counter finding is corrected in the proposal; runtime tests are pending.

### New finding and correction

The browser review found that undo/clear could remove an offsetting stake and
leave another seat's possible payout outside int64 house accounting range. A
concrete example uses house result `-INT64_MAX + 35`, a 35-credit black bet, and
a two-credit straight bet on red number 1. All outcomes fit before cancellation;
cancelling black makes a number-1 win produce house result `-INT64_MAX - 35`.

Updated the proposed undo/clear contract to preflight all 37 remaining settlements
across all seats, house accounting and meters, then commit atomically or reject
with `ArithmeticOverflow`. Added this boundary case to the planned native tests
and clarified that a fully cleared seat does not increment games played/won.
This newly found issue is corrected at the design level, not runtime-tested.

### Verification actually performed

- All eight public headers passed independent C++17 syntax checks with warnings
  as errors, exceptions disabled and RTTI disabled. No function implementations
  were linked or executed.
- An independent scratch arithmetic model enumerated 99 terminal paths for zero,
  one and 128 bets, including near-uint64-maximum counters; the documented reserve
  table covered each path and rejected attempts to spend protected IDs.
- A scratch timing model checked 20 delayed/equal/late, timestamp-order and timer
  rollover scenarios. The corrected rule preserved timely reports and rejected
  late reports. This verifies the proposed rule, not an existing engine function.
- Independently checked the cancellation example, including valid initial seat
  balances and conservation for each possible payout class before cancellation.
- Both reviewers inspected the revised contracts and reported no remaining
  concrete blockers. Documentation links, code fences and whitespace checks passed.

The two earlier findings and the new cancellation finding are resolved in the
proposal. `pio test -e native` remains pending the authorized implementation and
native test setup; Phase 1 is not complete. No runtime behavior is claimed proven.

## 2026-11-11 — Follow-up code review

The user requested another bug review. The assistant reviewed the public headers
and Phase 1/3B contracts at commit `3ba56e5`. Two fresh AI reviewers independently
checked the core API and cross-phase consistency without receiving the prior
conversation history.

No new actionable correctness findings were established. The review checked
observation-time ordering and deadlines, bounded wheel batches, resolution counter
reserves, accounting/cancellation boundaries, reload settlement, single-writer
browser ownership and exact-integer bridge semantics. The earlier corrections
remain consistent with the stated requirements.

This was a contract/code-inspection review. There are still no runtime function
bodies or native game tests. The unchanged header declarations were not compiled
again; their earlier independent C++17 syntax checks remain the latest compiler
validation. No game behavior was exercised. Only this workflow log was changed.

## 2026-11-11 — Approved Phase 1 implementation: build and domain

The user asked whether the project could run. After the assistant explained that
only headers existed and proposed implementing Phase 1 with native test setup,
the user replied "yes". This authorized Phase 1 implementation; the requirement
to stop for review before later phases remains in place.

The assistant set up a repository-local Python virtual environment and pinned
PlatformIO 6.2.0, the native platform 1.2.1, and C++17 compilation with warnings as
errors, exceptions disabled and RTTI disabled. The initial restricted-network pip
attempt failed DNS resolution; the authorized network-enabled retry installed
the tools successfully. Build/cache directories are local and ignored by Git.

Implementation was split among the assistant (engine), a domain agent, a
model/storage agent, and an engine-test agent. No runtime code existed before
this step. The domain implementation adds checked int64 arithmetic, unbiased
bounded random draws, the European wheel mapping/colours, canonical bet geometry,
standard profit odds and checked gross returns. A seeded MT19937 test adapter
keeps random sequences reproducible.

The 13 domain test cases pass in the native PlatformIO suite. They enumerate all
37 straights, all 1,369 ordered split pairs, all 7,770 three-number combinations,
all 66,045 four-number combinations, and all 157 valid selections against every
result. They also exercise invalid selections, unchanged outputs on failure,
RNG rejection boundaries and arithmetic extrema. An independent scratch UBSan
check compared one million full-range int64 input pairs against compiler overflow
builtins for addition, subtraction and multiplication; it passed. The scratch
check is supplemental, not a replacement for committed native tests.

## 2026-11-11 — Phase 1 ledger and atomic test journal

Implemented configuration and snapshot validation, accounting invariants and the
shared terminal reservation calculation. Snapshot validation rejects unsupported
versions, invalid round shapes, malformed bets/results, inconsistent counters and
accounting corruption before recovery can publish the loaded state.

The fixed-capacity test journal stores a complete snapshot and append-only event
batch atomically. It can fail before writing, report an uncertain outcome after
writing, enforce finite capacity and inject corruption. Counter and event-space
reservations cover payout/refund, optional recovery logging and return to Idle.

All 15 storage/model native test cases passed. They check validation, conservation,
event/revision continuity, unchanged outputs on read failure, earlier event
immutability, old-or-new crash visibility, corrupt journals and preserved terminal
reserves. The in-memory adapter models an atomic storage contract; it does not
claim process-exit durability or implement NVS/localStorage.

The only public model addition was `validate_snapshot()`, so the engine and test
adapter share the same structural and accounting checks. No user-requested rule
was changed or rejected.

## 2026-11-11 — Phase 1 engine, faults and recovery

Implemented all guarded round commands and eight-seat credit/meter updates.
Accepted stakes move into escrow; acceptance checks every possible result's
settlement arithmetic and meter headroom. State, money and event changes share
one atomic journal commit, published to RAM only after success. Explicit errors
replace exceptions, and engine snapshots/event buffers have fixed capacity.

Wheel observations are validated as a whole bounded batch before a result is
trusted. Capture times determine deadlines, including delayed polling and uint32
rollover. Faults before Result refund; faults after a durable Result are rejected
so recovery must settle the recorded outcome. Storage/commit errors stop ordinary
progression until recovery.

The first full native run passed 65 of 66 cases. Its corrupt-storage case showed
that `initialize()` returned generic `NotReady` after failed recovery, hiding the
underlying `StorageCorrupt` diagnostic. The assistant moved the initialization
status check after the storage read. Corrupt storage remains a concrete error;
an uncertain prior write still requires recovery even if storage appears empty.
The existing test was retained, and the subsequent full suite passed.

Four more focused cases were added for empty rounds, invalid explicit fault codes,
half-range clock ambiguity and a deterministic 50-round accounting campaign. The
campaign independently tracks seat balances, house result and meters across eight
seats, transfers, normal payouts, operator refunds and power-loss recovery. Its
draws use the existing seeded `IRandom` adapter and rejection-sampling helper.

Final `pio test -e native` result after all code changes: **70 passed, 0 failed**
(13 domain, 15 storage/model, 42 engine). Engine coverage includes every illegal
command/state pair, all 128 bet slots, crash before/after each money or state
commit, crash during recovery, repeated recovery without duplicate payments,
terminal storage/counter reserves, event continuity and zero observed C++ heap
allocations in the exercised round path.

Three agents independently reviewed the implementation's domain, engine and
storage integration after it was written. They found no additional concrete
reachable defect. This is not proof that the software is bug-free, and native
memory-journal tests do not establish physical storage durability.

## 2026-11-11 — Phase 1 run instructions and review handoff

Updated README and the design document to reflect the implemented Phase 1,
actual test layout and run commands. Added a JavaScript-oriented C++ walkthrough
with a complete scripted round. The guide's example was extracted, compiled with
all core sources using strict C++17 flags and run successfully; it printed
`Seat 0: 275, house: -175`, matching its stated accounting example. Local Markdown
links, paired code fences and `git diff --check` passed.

The implementation was saved in separate logical commits for domain/build,
ledger/storage, engine/recovery and documentation. The assistant stopped at the
Phase 1 review boundary. No multiplier implementation, CLI, browser application,
firmware, hosted demo or real-device durability test was completed or claimed.

## 2026-11-11 — Approved Phase 2: Lucky Numbers and probability model

After the Phase 1 handoff, the user said "proceed". The assistant continued with
Phase 2 while retaining the stop-for-review boundary before Phase 3. Work was
split among core selection/payout implementation, independent math and engine
integration tests, and the analysis tooling; the assistant integrated the feature.

The implementation makes multiplier semantics explicit: 50× means 50 times the
stake returned in total, replacing the reduced straight-up gross return. It is
not multiplied by base odds and is not added to another payout. Only straights
change. Default Lucky mode uses 29:1 base profit, a uniform count from one through
five distinct numbers, and gross multipliers 50/100/200/500 with weights
48/30/20/2. The mode is off by default.

The new Lucky API validates configurations and recorded draws, uses rejection
sampling and a partial Fisher–Yates shuffle, provides base/maximum/recorded payout
helpers, and calculates analytical RTP and variance. Number selection is uniform
without replacement; multiplier tickets are independent draws. All ledger
amounts remain int64; floating point is confined to probability analysis.

Independent calculation confirmed E[K] = 3, E[M] = 104, and exact default RTP 36/37.
It also identified an important verification constraint: ten million fixed-number
unit bets have a roughly ±0.4765 percentage-point 95% interval, wider than the
requested analytical ±0.1-point tuning target. The analysis therefore reports
that sampling uncertainty and also measures a clearly labeled 37-straight
portfolio, treating spins rather than its correlated bets as independent samples.
No seeds are retried to improve the reported RTP.

All 15 new Lucky domain test cases passed, including every 37×100 pocket/ticket
choice, all 1,332 ordered distinct pairs, all count ranges, seeded draws, every
straight outcome/multiplier combination, unchanged non-straight payouts, maximum
uint32 weight totals, uint16 multiplier 65535, checked payout overflow, and
analytical formulas compared against independently enumerated expected values.

## 2026-11-11 — Phase 2 engine integration and crash tests

Added a borrowed `IRandom` constructor overload while retaining the standard-mode
two-argument constructor. Enabled rounds need an RNG to initialize/open; recovery
of a recorded result needs no RNG. `close_bets()` now saves the enabled draw,
its full reveal event and the BetsClosed transition in one commit. Recovery
refunds pre-result rounds or settles the saved result and multipliers, never
redrawing them. The config and draw are part of snapshot schema version 2;
unsupported version 1 is rejected rather than interpreted as an empty ledger.

Before accepting a bet, the engine checks each of 37 possible results at both
base and maximum configured gross returns. All winning straights on a result
share the same multiplier, so these endpoints cover every possible seat, house
and meter outcome. The reserve budget adds one reveal event while bets remain
open/closing. Full draw validation checks the count, supported values, uniqueness,
unused fields and whether that state can contain a draw.

The full `pio test -e native` run passed **101 test cases, 0 failures**: all 70
Phase 1 cases, 15 Lucky domain cases and 16 Lucky engine integration cases. The
new integration tests cover close/result/payout commit failures before and after
the write, recovery without randomness, base and boosted payouts, 65535× returns,
corrupt recorded draws, overflow headroom, exact event capacity, near-maximum
sequence counters and the enabled no-allocation path. The old-schema fixture was
updated to reject version 1 after the intentional schema change.

Two agents independently reviewed the engine/model/storage integration and found
no concrete reachable defect. No native test failure was observed in Phase 2;
the review-driven changes were the explicit payout definition, uncertainty-aware
math validation, complete draw persistence, payout headroom and reveal reserves.

## 2026-11-11 — Phase 2 Monte Carlo run and reproducible results

Added a C++ analysis harness using the shared Lucky selection, payout and analytical
functions. A seeded `IWheel` supplies the result; an independent seeded `IRandom`
stream supplies Lucky selection. It measures a fixed-number standard bet, a
fixed-number Lucky bet and a separately labeled portfolio covering all 37
straights. This fast probability harness does not journal full engine rounds;
the native integration tests cover that behavior.

The Python build/run wrapper uses only the standard library. Four tooling
integration checks passed: invalid arguments, deterministic output/accounting,
the final partial convergence interval, and an invalid output path. Review found
and corrected a report link that assumed the default output directory; generated
reports now refer to the repository's math document without a broken relative
link when a custom destination is used. Build artifacts are ignored.

Ran `python3 tools/run_math.py --spins 10000000 --seed 20261111` successfully,
using Lucky seed 2667729230. The saved results are:

| Experiment | Observed RTP | Approximate 95% interval |
| --- | ---: | --- |
| Standard fixed number | 96.909480% | 96.548349%–97.270611% |
| Lucky fixed number | 96.848700% | 96.371746%–97.325654% |
| Lucky all-37 portfolio | 97.270592% | 97.219625%–97.321559% |

Theoretical RTP is 97.297297% for all three. The standard fixed-number interval
misses it in this run; the result and seed were retained rather than rerun to
force agreement. The Lucky portfolio differs by about −0.0267 percentage point
and its interval contains theory. Wheel chi-square was 39.909041 with 36 degrees
of freedom, below the 1% critical value 58.619, so uniformity was not rejected.
NIST's primary references for the test, critical value and confidence intervals
were consulted and linked in `MATH.md` and the generated report.

Independent checks confirmed the histogram totals ten million, all 37 bins are
present, the 100 convergence rows end at ten million spins, standard returns
equal 36 times the zero count, and every final RTP matches its exact credit
totals. An exact rational calculation independently recovered RTP 36/37 and the
reported variance formulas. CSV outputs are saved for the later plotting phase.

## 2026-11-11 — Phase 2 review handoff

Updated README with current scope, the 101 passing native tests, all three
observed RTP rows, the wheel statistic and reproducible commands. `MATH.md`
explains the calculation step by step, includes a C++ enabling example and
documents persistence, overflow and statistical boundaries. Earlier phase guides
now point to the implemented extension without implying the browser exists.

Local links and fenced blocks passed in all seven Markdown documents, and
whitespace checks passed across the complete Phase 2 diff. Changes were saved
as logical commits for the Lucky domain/math API, engine persistence/recovery,
Monte Carlo tooling/results and documentation. The assistant stopped for Phase 2
review. No Phase 3 CLI, browser, firmware or PNG plotting was implemented.

## 2026-11-11 — Approved Phase 3: shared simulation components

After the Phase 2 handoff, the user said "continue". The assistant proceeded with
Phase 3 and retained the review boundary before Phase 3B. Implementation was split
into shared wheel/bots/RNG, the session journal, native tests, and the CLI; the
assistant implemented the CLI and coordinated integration and documentation.

Added `sim/shared/` as a portable PlatformIO library for later browser reuse.
It supplies a seeded MT19937 adapter, a timestamped simulated `IWheel`, and eight
fixed betting strategies. The wheel supports one-shot no-spin, ball-timeout,
invalid-sensor and start-failure faults. Each valid start attempt consumes a
bounded pocket draw, even on a fault, so subsequent results stay aligned.
Polling preserves observation times, handles rollover and emits each report once.

Bots choose one request per seat per round using core selection builders. They
reduce fixed five/ten-credit stakes to affordable table limits, skip existing or
unaffordable bets, and never change credits or calculate payouts. The eight
strategies cover fixed/random straights, red, alternating odd/even, rotating
dozens/columns, random horizontal splits and cycling street/corner/six-line bets.

All 14 new shared-component tests passed. They cover known/repeatable RNG output,
partial/delayed/wrapped polling, cancellation, invalid inputs, every wheel fault
through the real engine, subsequent result alignment, all bot strategies and
limits, 100 complete Lucky rounds, and no observed C++ allocations in the shared
round path. Existing core game logic was reused without production changes.
