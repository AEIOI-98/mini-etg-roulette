# Build and review the first wager

Start with a player who has 1,000 credits and places 10 on red. Take that wager
from an input to a recorded result and a settled balance on the desktop, in the
browser and on an ESP32. Add the remaining bets, bots and animations once this
round works. This is the recommended build order. The development log records
the order in which the portfolio was built. [Development log](AI_WORKFLOW.md)

## First milestone

| Target | Build first | Show at review |
| --- | --- | --- |
| Desktop | A C++ round, a test wheel and a RAM journal. Start native CI with this test. | Accept 10 on red and show 990 available. A red win returns 20 credits including the stake, leaving 1,010. A black or zero result leaves 990. Escrow clears at settlement. |
| Browser | Expose that round through embind and render a Red button, chip selector and balance. Add the browser build and tests to pull requests. | Click the 10-credit chip and Red. Read the result, then check the seat balance and escrow on the operator dashboard. |
| ESP32 | Boot the merged image in Wokwi, accept the same wager from a button, count the wheel pulses, and read the journal at reset. Add the firmware build to CI. | Show the refund, a recorded result and a second reset on the OLED. Explain what the saved state tells the controller to do. |

Run the repeatable desktop and WebAssembly example from a fresh checkout with
a C++17 compiler, Python 3, Node 24 and an activated Emscripten 4.0.22 SDK.

```sh
cd web
npm ci
npm run wasm
npm run test:wager
```

The command runs the C++ session natively and through the browser's production
WebAssembly bindings. It checks the player's 1,000 → 990 → 1,000 refund, then
replays a second wager. Seed 3 yields red 36. Recovery pays the recorded win to
1,010 and another restart leaves 1,010. The browser table includes its seven
bots, so the test reconciles the house, escrow and all eight seats after each
command. The separate Chrome test clicks the chip and Red and checks the
settled balance against the colour actually displayed.
[Wager test](../web/test/bridge.test.mjs) · [Chrome test](../web/test/browser.test.mjs)

The ESP32 demo funds one seat and gets the result from a count of simulated
sensor pulses. Run `pio run -e esp32dev` from the repository root and start the
Wokwi simulator, or use the published firmware in a browser. Its result can
differ from the seeded fixture. [Run the controller](PHASE_4_GUIDE.md)

## Expand one phase at a time

| Phase | Build and test | Lead the review |
| --- | --- | --- |
| 1 · Engine | Extend the first round to all bets, eight seats, table limits, faults and recovery. Check conservation after every accepted change. | Point to the credits held in escrow. Show what the engine does to them when a sensor fails. |
| 2 · Math | Add Lucky Numbers. Check the calculated RTP against 36/37 and report the seeded experiment with its uncertainty. | Explain why the standard bet on zero measured 96.909480% in the saved run. Show the seed and interval beside it. [Math acceptance criteria](MATH.md#what-passes-review) |
| 3 · Desktop simulator | Add bot strategies, a seed and fault injection. Put the CLI integration tests alongside the native checks. | Repeat a seed, then inject a fault and show the refund in the round log. |
| 3B · Browser | Add the complete betting layout, wheel animation, local storage and operator dashboard. Test the production bundle in Chrome on each pull request. | Place a split, close the round and locate the recorded result before the animation settles. |
| 4 · Controller | Add pulse-count faults, serial framing, the watchdog and flash recovery. Run the serial monitor tests alongside the firmware build and demonstrate the result in Wokwi. | Perform the reset demonstration below and identify the journal write responsible for the balance. |
| 5 · Publish | Generate charts from the measured CSVs and deploy the tested browser build from main. | Open the public demo, place a chip and verify the same balance from the operator screen. |

## Lead the recovery demonstration

1. **Show the stake.** Start at 1,000 and tap **Bets**. The OLED shows 990
   available and 10 staked. Hold **Launch** for 1.2 seconds while betting is
   open. The reboot refunds the 10 and shows **Recovered: refunded**. Tap
   **Bets** to acknowledge.
2. **Show the recorded result.** Tap **Bets** to wager again, tap it once more
   to close, then tap **Launch** when the bets are closed. Hold **Launch** as
   soon as the result appears. From a starting balance of 1,000, red settles to
   1,010, while black or zero settles to 990. Explain the outcome you see.
3. **Show a second reset.** Hold **Launch** again. Point to **Already paid**
   and read out the balance. In the engine, `tick()` publishes the pocket and
   the Result state together. On boot, `recover()` refunds a wager interrupted
   before Result, settles a saved Result, or recognizes an existing Payout.
   The ESP32 publishes each new ledger by writing the NVS record named
   `head` last. [Record the result](../core/src/engine.cpp#L547) ·
   [Choose the recovery action](../core/src/engine.cpp#L293) ·
   [Publish the flash ledger](../firmware/src/nvs_journal.cpp#L182)
