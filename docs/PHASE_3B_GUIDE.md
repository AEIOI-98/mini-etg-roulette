# Playing and operating the browser table

Open the terminal in a modern browser over HTTPS or localhost. Seat 0 starts with
1,000 virtual credits. The seven other seats use the same C++ bot strategies as
the desktop simulator. The browser table enables Lucky Numbers. Only demo credits
are used.

## Try a round

1. During the 20-second betting window, choose a chip and tap a number. A line
   between two numbers places a split. An intersection places a corner. The
   lower edge of a street selects three numbers, and the joint between two
   streets selects six. Tap **List** to see the exact numbers in any selection.
2. Try two 100-credit chips on the same number. The first placement moves credits
   to escrow. The second reaches the limit and the first chip stays on the table.
   Undo removes the newest placement. Clear returns all the active chips for your
   seat. Repeat last restores the stakes from your most recent paid round in
   which you played.
3. Watch the Lucky Numbers appear at betting close. The wheel starts its targeted
   reveal after the engine records the winning pocket. During payout the terminal
   shows the gross credits returned to your seat.

## Try a fault and a restart

Open **Operator** in the same tab to control the live table. Arm **Bad sensor
reading** or **Ball not detected** before the next wheel start. The first produces
an invalid pocket reading. The second lets the ball timeout expire. Look for the
void, the `BetRefunded` entries, the return of the credits and zero escrow.
Arming the control after the wheel has started affects the following spin.

Use **Reload & recover** while betting is open to see the round refunded. Reload
after the engine reaches Result to complete the recorded payout. Reload again
during Payout to see the same meters. The event journal, results and RTP history
survive a page reload. A second tab displays the writer's live view and takes
over when the writer tab closes. Routing between screens within the writer tab
preserves the same session.

## How a click reaches C++

Think of an embind method like a call to an imported JavaScript class. The class
implementation was compiled by Emscripten into a `.wasm` file. When you tap a
split, TypeScript sends its catalog ID, the chosen integer stake and a clock
sample into `BrowserSession.place`. C++ checks the betting deadline, validates
the request, saves the ledger and returns a display view. The page draws the
chip from the accepted bets in that view.

The C++ session owns the engine and supplies its wheel, random source, storage
and bot strategies. Embind gives JS a handle to the session. The page calls
`delete()` when it closes so C++ can release the owned objects. Read-only display
copies cross the boundary. Money and identifiers use BigInt or decimal strings
in transit, so values beyond JavaScript's safe integer range stay exact.

The engine accepts a detected pocket through `IWheel`, journals the result, and
runs settlement on its own schedule. The Canvas renderer reads the recorded
pocket and aims the final wheel and ball angles at it. Changing the frame rate,
switching routes or skipping a reduced-motion animation cannot change the
outcome or the payout.

## What is saved

`IStorage` groups the ledger and the new events in one commit. The browser adapter
writes explicit fixed-width integer fields, packs them with the previous event
history and a checksum, then replaces **one** base64 localStorage value. A Web
Lock gives a single tab authority to issue commands. BroadcastChannel sends
read-only display copies to the other tabs. Neither feature requires a server.

The append-only journal has space for 8,192 events. Each commit includes padding
for the worst remaining path to settlement or refund. That space is already part
of the saved value and can be used even when the quota has been reached.
Storage errors pause the table and leave the last successful commit available.
Malformed saves display an error. This demonstrates recovery from a browser
reload. Browser storage settings and eviction determine the lifetime of the save.

The RTP chart divides cumulative gross returns by settled stakes. Its reference
uses the actual stake mix and the shared Lucky math. A void or cancellation adds
nothing to these totals. The histogram adds one count per paid round with
expected frequency equal to completed rounds divided by 37. An empty chart is
normal before the first payout. Both charts replay committed history on reload.

## Build and run

Use Node 24, npm, Python 3, a C++17 compiler and Emscripten 4.0.22. Once the
[Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html) is
installed, run these commands in your SDK checkout:

```sh
./emsdk install 4.0.22
./emsdk activate 4.0.22
source ./emsdk_env.sh
```

Then run from the project's `web/` directory:

```sh
npm ci
npm run build
npm run preview
```

Use the Local URL printed by Vite. Add `#/operator` for the dashboard. For UI
development, `npm run dev` provides live reload after `npm run wasm` has
generated the C++ module. Rebuild WASM after changing C++.

```sh
npm test
npx --no-install playwright-core install chromium
npm run test:browser
```

An existing Chrome executable can be selected via `CHROME_BIN` for browser tests.
The production browser test serves the built files under a repository subpath.
The Pages workflow builds the native tests and WASM, runs the web tests, and
deploys `web/dist` using the repository's GitHub Actions Pages source.
