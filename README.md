# mini-etg-roulette

**Live demo:** not deployed yet. The verified GitHub Pages link will go here after
the Phase 3B deployment succeeds.

A proposed C++17 model of a European roulette game, with a shared engine intended
for a desktop simulator, a browser player terminal/operator dashboard, and an
ESP32 wheel-controller demo.

**Educational demo only. No real money. Not certified gaming software.**
No company products or proprietary features are reproduced.

**Current status: Phase 1 interface proposal, awaiting review.** There are eight
public headers, a folder plan, and design notes. There are no game implementations,
test implementations, simulator, firmware, or build configuration yet.

Start with [the proposal and header index](docs/PHASE_1_PROPOSAL.md).
It explains the accounting, state machine, recovery rules, and relevant C++ ideas.
The added [Phase 3B plan](docs/PHASE_3B_PROPOSAL.md) covers the browser interface,
WASM bridge, GitHub Pages deployment, and the Phase 4–5 additions.

The phase order is **1 → 2 → 3 → 3B → 4 → 5**, with user review after each phase.

## Design principle: the engine decides, the presentation follows

The C++ engine produces and journals the result first. The browser then computes
wheel rotation, ball deceleration and settling to land on that recorded pocket.
Animation timing, skipped frames, reduced-motion settings and page reloads never
choose a result, reroll it or calculate a payout. The dashboard exposes the actual
engine state even while a result-reveal animation is still playing.

Emscripten will compile the same C++ engine into WebAssembly, which the browser
can run. Embind supplies callable bindings between TypeScript and that compiled
code. A click sends a command into C++; TypeScript displays the returned state.
Game rules, bot decisions, credit changes and payouts remain in C++.

The acceptance command for implementation will be `pio test -e native`.
It is not runnable for this proposal: PlatformIO is not installed in the current
workspace and the native test target has not been created. A header syntax check
does not replace that requirement or mark Phase 1 complete.

## Planned demo media and results

- Web terminal GIF: slot reserved for a recording of the implemented terminal.
- Wokwi recording: slot reserved for the ESP32 demo, OLED wheel/result and recovery.
- Monte Carlo charts: Phase 5 `tools/plots.py` will generate RTP convergence and
  result-distribution PNGs from Phase 2 measurements for this README.

## Built with AI assistance

The current folder plan and interfaces were proposed with AI assistance and reviewed
for design consistency by AI agents. They have not yet been approved by the user.
See [AI_WORKFLOW.md](docs/AI_WORKFLOW.md) for the actual session record.

Projekt je učni prikaz evropske rulete s skupnim jedrom v jeziku C++17.
Ne uporablja pravega denarja in ni certificirana programska oprema za igre na srečo.
Trenutno vsebuje le predlog javnih vmesnikov, ki čaka na pregled.
