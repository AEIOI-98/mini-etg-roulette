# Phase 2: Lucky Numbers and return to player

This is an educational probability model with integer credits. It does not use
real money or claim certification. The calculation describes long-run expected
returns, not the result a player should expect from a short session.

## What a multiplier means

A **50× multiplier is a total return of 50 times the stake**, including the
original stake. It replaces the ordinary straight-up return on that number. It
does not multiply the 29:1 base odds and does not add another base payout.

For a five-credit winning straight:

- Standard mode: 35:1 profit returns `5 × 36 = 180` credits.
- Lucky mode, number not selected: 29:1 profit returns `5 × 30 = 150` credits.
- Lucky mode, selected at 50×: returns `5 × 50 = 250` credits.
- A losing straight returns zero in either mode.

Only straights change. Splits, streets, corners, six lines, dozens, columns and
even-money bets retain their standard payouts, even if they cover a Lucky Number.
Zero is eligible to be selected, just like every other wheel number.

## The standard baseline

A European wheel has 37 equally likely results. One unit straight bet wins on
one result and returns 36 units, including its stake. Its expected return is:

```text
RTP = probability of winning × total return on a win
    = (1 / 37) × 36
    = 36 / 37
    = 0.97297297297… = 97.297297297…%
```

The expected loss is `1 − RTP = 1/37`, or about 2.7027% of credits wagered. The
other standard bets have the same expectation: for example, a six line covers
six numbers and returns six times the stake, so `6/37 × 6 = 36/37`.

## Drawing the Lucky Numbers

After betting closes, draw a count `K` uniformly from 1 through 5. Then select
`K` distinct numbers uniformly from the 37 numbers, using a partial Fisher–Yates
shuffle. Each selected number receives an independently drawn weighted multiplier.
Selection has no knowledge of player bets or the eventual wheel result.

All bounded random draws use `IRandom` and rejection sampling. Modulo alone would
give some outputs more source words than others when the bound does not divide
the generator's 32-bit range. A full-range word in the excess prefix is rejected
before applying modulo. Drawing without replacement prevents duplicate numbers.

The default multiplier table is:

| Gross multiplier | Integer weight | Probability |
| --- | ---: | ---: |
| 50× | 48 | 48% |
| 100× | 30 | 30% |
| 200× | 20 | 20% |
| 500× | 2 | 2% |

Weights need not sum to 100. Each entry's probability is its weight divided by
the total weight. The configuration requires positive weights, unique multiplier
values, total weight fitting uint32, and multipliers above the reduced base gross
return. The count range is configurable within 1–5, the base profit odds within
0–35, and the table has at most eight entries.

The weighted mean multiplier is:

```text
mean M = (48×50 + 30×100 + 20×200 + 2×500) / 100
       = 104
```

The average count is `mean K = (1 + 2 + 3 + 4 + 5)/5 = 3`. By symmetry, one fixed
number is selected with probability `mean K / 37 = 3/37`. The draw and wheel
result are independent in the probability model.

## Tuning the expected return

Let `B` be the base **gross** return, `m = E[K]`, and `mu = E[M]`. Conditional on
winning the straight, its expected return is:

```text
base return + probability selected × average improvement
= B + (m / 37) × (mu − B)
```

The bet still wins only once in 37 results, giving the general formula used by
`analyze_lucky()`:

```text
straight RTP = [B + (m / 37) × (mu − B)] / 37
```

To preserve the standard return, the numerator must equal 36. With `B = 30` and
`m = 3`, solve for the needed weighted mean:

```text
30 + (3 / 37) × (mu − 30) = 36
mu = 30 + 37 × (36 − 30) / 3 = 104
```

The default table has exactly that mean, so the theoretical Lucky straight RTP is
exactly `36/37`. Its difference from the baseline is zero, meeting the requested
±0.1 **percentage point** tuning tolerance. The core calculator accepts a
`LuckyConfig`; a different count range, base or weight table is analyzed by the
same formula. A valid custom configuration need not meet the default RTP target.

Ledger amounts and payouts remain checked int64 credits. Floating point is used
only for probabilities, variance, percentages and statistical reporting.

## Enable the mode and preserve recovery

Given the wheel, storage and funding objects from the
[Phase 1 example](PHASE_1_GUIDE.md), supply an RNG when constructing the engine:

```cpp
#include "seeded_random.hpp" // Test adapter from test/support/.

mini_etg::test::SeededRandom random(12345);
Engine engine(wheel, storage, random);
EngineConfig config{};
config.lucky.enabled = true;
require_ok(engine.initialize(config, funding));
```

The borrowed RNG must remain alive while the engine uses it, just like the wheel
and storage. Set `config.lucky.base_straight_profit_odds`, `min_count`, `max_count`
or the `table` and `table_count` before initialization to analyze or run a custom
configuration. Configuration is fixed in the journal, so it cannot change between
accepting a bet and paying it. Standard mode still uses the two-argument engine
constructor and needs no Lucky random source.

`close_bets()` saves the draw, a `LuckyNumbersDrawn` event and BetsClosed in one
commit. The draw is available in `engine.snapshot().round.lucky`. Before accepting
a bet, the engine checks every possible winning number at both the base and largest
configured return. Since all winning straights share that number's multiplier,
these endpoints bound every possible payout and protect both seats and the house
from integer overflow. The journal also reserves space for the reveal event.

Recovery before Result refunds the stakes. Recovery after Result pays the saved
number using the saved multipliers; it does not draw again and needs no RNG to
complete that payout. The snapshot schema is now version 2. Old or unsupported
schemas return an error instead of resetting balances; there is no persisted
Phase 1 file-format migration because file/NVS/browser adapters do not yet exist.

## Why ten million spins still have sampling noise

Consider one credit on the same number in every spin. Most returns are zero, and
a few are large. With the default table:

```text
E[M²] = (48×50² + 30×100² + 20×200² + 2×500²) / 100 = 17200
E[R²] = [(1 − 3/37) × 30² + (3/37) × 17200] / 37 = 82200/1369
Var(R) = E[R²] − E[R]² = 80904/1369 ≈ 59.097151
SE(mean return) = sqrt(Var(R) / N)
```

At ten million spins, the fixed-number standard error is about **0.2431
percentage point**; a normal 95% interval has roughly 0.4765 percentage point
half-width. Therefore ±0.1 percentage point is an analytical tuning target, not
a defensible pass/fail band for that sampled RTP. The report retains the chosen
seed and observed result; it does not retry seeds to obtain a prettier number.

For an additional, lower-variance check, the harness also evaluates one credit on
**each of the 37 straights per spin**. Exactly one straight wins, so it only needs
to calculate that winner's shared-core payout; the other 36 return zero. Divide
the returned credits by the 37-credit wager. The expected per-credit return is
again `36/37`.

This portfolio's normalized per-spin variance is `34248/50653 ≈ 0.676130`. Its
ten-million-spin 95% half-width is about **0.05096 percentage point**. This gives
a more precise check of the same expectation. It changes the amount wagered and
the variance, not the expected loss. All 37 bets in a spin share one outcome;
there are **N independent spin samples, not 37N independent bets**.

The generated intervals use the sample standard deviation divided by `sqrt(N)`,
then multiply by 1.96 for a large-sample normal approximation. The z score uses
the analytical standard error and measures the sampled deviation in standard
errors. [NIST explains confidence limits for a mean](https://www.itl.nist.gov/div898/handbook/eda/section3/eda352.htm).
Repeated 95% interval procedures cover the true expectation about 95% of the
time under the model; a particular computed interval either contains it or does not.

## Wheel uniformity check

The harness counts each printed result in 37 bins. Every bin's expected count is
`E = N/37`. Pearson's statistic is:

```text
chi-square = sum over 37 numbers of (observed − expected)² / expected
```

No probabilities are fitted from the sample, so there are `37 − 1 = 36` degrees
of freedom. At upper-tail significance 1%, the critical value is **58.619**. A
value below this threshold means the experiment does not reject uniformity. It
does not prove a source is random, independent or suitable for gaming; this test
also does not detect serial correlation. A genuinely uniform source will exceed
the threshold in roughly 1% of repeated experiments. See NIST's
[test definition](https://www.itl.nist.gov/div898/handbook/eda/section3/eda35f.htm)
and [critical-value table](https://www.itl.nist.gov/div898/handbook/eda/section3/eda3674.htm).

## Reproduce the experiment

From the repository, using Python 3 and a C++17 compiler:

```sh
python3 tools/run_math.py --spins 10000000 --seed 20261111
```

The script uses only Python's standard library. It compiles the shared core and
math harness with optimization, strict warnings and exceptions/RTTI disabled.
Set `CXX` to choose a compiler, or use `--build-only` to compile without running.
`--output /path/to/directory` selects another destination. Each run replaces its
three generated files in that directory:

- [RESULTS.md](../tools/results/RESULTS.md): analytical and measured RTP, exact
  credit totals, intervals, seeds, wheel chi-square and draw diagnostics.
- [histogram.csv](../tools/results/histogram.csv): all 37 observed/expected counts.
- [convergence.csv](../tools/results/convergence.csv): cumulative returns and RTP
  every 100,000 spins, plus the final sample, ready for Phase 5 plotting.

The wheel uses an MT19937 stream seeded with the command-line seed. The Lucky
stream uses a separate MT19937 instance seeded with `seed XOR 0x9e3779b9`. All
bounded draws use the shared rejection helper. Standard and Lucky experiments
share the same wheel observations for comparison. The seed pair is recorded in
the result file; MT19937 is deterministic and is not a cryptographic generator.

The fast harness calls the actual core's selection, payout and analysis functions;
results arrive through an `IWheel` adapter. It does not journal ten million full
engine rounds. Native tests separately exercise the full state machine, accepted
bets, durable reveal, accounting, faults and recovery. This separation makes the
math experiment practical while keeping game rules in C++.

The optional tooling checks run with `python3 tools/test_math_tool.py`. Short
runs are labeled smoke runs and do not satisfy the ten-million-spin acceptance
requirement. PNG plotting and the eight-bot interactive CLI remain later phases.
