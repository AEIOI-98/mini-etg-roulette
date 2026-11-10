# mini-etg-roulette

A proposed C++17 model of a European roulette game, with a shared engine intended
for a desktop simulator and an ESP32 wheel-controller demo.

**Educational demo only. No real money. Not certified gaming software.**
No company products or proprietary features are reproduced.

**Current status: Phase 1 interface proposal, awaiting review.** There are eight
public headers, a folder plan, and design notes. There are no game implementations,
test implementations, simulator, firmware, or build configuration yet.

Start with [the proposal and header index](docs/PHASE_1_PROPOSAL.md).
It explains the accounting, state machine, recovery rules, and relevant C++ ideas.

The acceptance command for implementation will be `pio test -e native`.
It is not runnable for this proposal: PlatformIO is not installed in the current
workspace and the native test target has not been created. A header syntax check
does not replace that requirement or mark Phase 1 complete.

## Built with AI assistance

The current folder plan and interfaces were proposed with AI assistance and reviewed
for design consistency by AI agents. They have not yet been approved by the user.
See [AI_WORKFLOW.md](docs/AI_WORKFLOW.md) for the actual session record.

Projekt je učni prikaz evropske rulete s skupnim jedrom v jeziku C++17.
Ne uporablja pravega denarja in ni certificirana programska oprema za igre na srečo.
Trenutno vsebuje le predlog javnih vmesnikov, ki čaka na pregled.
