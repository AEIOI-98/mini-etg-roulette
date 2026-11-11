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
