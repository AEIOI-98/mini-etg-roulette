# AI-assisted development record

Only actions that actually occurred in this session belong here. Planned tests
are documented separately in `PHASE_1_PROPOSAL.md`.

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
