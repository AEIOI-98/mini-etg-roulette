// C++ copies these display values out of WASM. Monetary amounts and ledger IDs
// are decimal strings on the JSON wire and bigint in the application.
export type RoundState = 'Idle' | 'BetsOpen' | 'BetsClosing' | 'BetsClosed' | 'Spinning' | 'Result' | 'Payout' | 'Fault'
export type Colour = 'red' | 'black' | 'green'
export type BetKind = 'Straight' | 'Split' | 'Street' | 'Corner' | 'SixLine' | 'Dozen' | 'Column' | 'Red' | 'Black' | 'Odd' | 'Even' | 'Low' | 'High'
export type DemoFault = 'BallTimeout' | 'InvalidSensor'
export interface Selection { id: number; kind: BetKind; numbers: number[]; label: string }
export interface Catalog { selections: Selection[]; wheel: { number: number; colour: Colour }[] }
export interface Seat {
  seat: number; name: string; credits: bigint; creditsIn: bigint; creditsOut: bigint
  gamesPlayed: bigint; gamesWon: bigint
}
export interface LogEvent {
  sequence: bigint; roundId: bigint; kind: string; command: string; detail: string
}
export interface SessionView {
  revision: bigint; roundId: bigint; state: RoundState; fault: string; recovery: string
  luckyEnabled: boolean; lucky: { number: number; multiplier: number }[]
  result: { number: number; pocket: number } | null
  seats: Seat[]; bets: { betId: bigint; seat: number; selectionId: number; stake: bigint }[]
  limits: { min: bigint; max: bigint; seatMax: bigint }
  canRepeat: boolean; house: bigint; escrow: bigint; playerReturn: bigint
  events: LogEvent[]; armedFault: string; recentResults: number[]
  charts: { rounds: number; histogram: number[]; expected: number
    series: { round: number; observed: number; theoretical: number }[] }
}
export interface BrowserSession {
  start(nowMs: number): string
  tick(nowMs: number): string
  place(id: number, stake: bigint, nowMs: number): string
  undo(nowMs: number): string
  clear(nowMs: number): string
  repeat(nowMs: number): string
  armFault(type: DemoFault, nowMs: number): string
  remainingMs(nowMs: number): number
  revision(): bigint
  view(): string
  catalog(): string
  delete(): void
}
const exactFields = new Set(['revision', 'roundId', 'betId', 'sequence', 'credits', 'creditsIn',
  'creditsOut', 'gamesPlayed', 'gamesWon', 'stake', 'min', 'max', 'seatMax', 'escrow', 'house', 'playerReturn'])
export function parseView(json: string): SessionView {
  return JSON.parse(json, (key, value: unknown) =>
    exactFields.has(key) && typeof value === 'string' ? BigInt(value) : value) as SessionView
}
