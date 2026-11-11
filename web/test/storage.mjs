// Run against the actual embind module and its IStorage adapter. The injected
// Web Storage implementation can deny reads or refuse an atomic replacement.
import assert from 'node:assert/strict'
import { execFileSync } from 'node:child_process'
import { mkdtempSync, readFileSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { after, test } from 'node:test'
import { fileURLToPath } from 'node:url'
import createModule from '../public/engine/roulette.mjs'

const repo = fileURLToPath(new URL('../../', import.meta.url))
const directory = mkdtempSync(join(tmpdir(), 'etg-storage-'))
const fixture = join(directory, 'storage-fixture')
after(() => rmSync(directory, { recursive: true, force: true }))
execFileSync(process.env.CXX || 'c++', ['-std=c++17', '-Wall', '-Wextra', '-Werror',
  '-Icore/include', '-Iweb/wasm', 'web/test/storage-fixture.cpp', '-o', fixture], { cwd: repo })
const wasm = await createModule({ wasmBinary: readFileSync(new URL('../public/engine/roulette.wasm', import.meta.url)) })
const key = 'mini-etg-roulette.journal.v1'
const largeStake = 2n ** 53n + 1n

class Store {
  constructor(saved = null) { this.saved = saved; this.limit = Infinity; this.denied = false }
  getItem(name) {
    assert.equal(name, key)
    if (this.denied) throw new DOMException('Unavailable', 'SecurityError')
    return this.saved
  }
  setItem(name, value) {
    assert.equal(name, key)
    if (value.length > this.limit) throw new DOMException('Out of space', 'QuotaExceededError')
    this.saved = value
  }
}
function start(store) {
  Object.defineProperty(globalThis, 'localStorage', { value: store, configurable: true })
  return new wasm.BrowserSession(42)
}
function mustStart(store) {
  const session = start(store)
  const error = session.start(0)
  if (error) session.delete()
  assert.equal(error, '')
  return session
}
const view = session => JSON.parse(session.view())
function tick(session, ...times) {
  for (const now of times) assert.equal(session.tick(now), '')
}
function fundedStore() {
  return new Store(execFileSync(fixture, ['funded']).toString('base64'))
}
function corrupt(mode, good) {
  return execFileSync(fixture, [mode], { input: Buffer.from(good, 'base64') }).toString('base64')
}

test('int64 stake, gross payout and negative house profit survive Embind and repeated reload', () => {
  const store = fundedStore()
  let session = mustStart(store)
  try {
    assert.equal(view(session).seats[0].credits, String(largeStake + 10n))
    tick(session, 500)
    // mt19937(seed=42) puts the first ball at pocket index 12, number 13.
    const straight = JSON.parse(session.catalog()).selections.find(s => s.kind === 'Straight' && s.numbers[0] === 13)
    assert.equal(session.place(straight.id, largeStake, 501), '')
    assert.equal(view(session).escrow, String(largeStake))
    assert.equal(view(session).seats[0].credits, '10')
    tick(session, 20500, 21700, 23300, 24300)
    assert.deepEqual(view(session).result, { number: 13, pocket: 12 })
    // Restrict the maximum replacement to space already paid for at Result.
    // Recovery has to settle and persist its changed repeat pattern inside it.
    store.limit = store.saved.length
    session.delete()
    session = mustStart(store)
    const paid = view(session)
    assert.equal(paid.state, 'Payout')
    assert.equal(paid.house, String(-35n * largeStake))
    assert.equal(paid.playerReturn, String(36n * largeStake))
    assert.equal(paid.seats[0].credits, String(36n * largeStake + 10n))
    assert.equal(paid.canRepeat, true)
    assert.equal(paid.seats[0].gamesPlayed, '1')
    const once = store.saved
    session.delete()
    session = mustStart(store)
    assert.equal(store.saved, once)
    assert.deepEqual(view(session).seats, paid.seats)
    assert.deepEqual(view(session).charts, paid.charts)
  } finally { if (!session.isDeleted()) session.delete() }
})

test('a quota-rejected placement preserves the previous history and reserved refund space', () => {
  const store = new Store()
  let session = mustStart(store)
  try {
    tick(session, 500)
    assert.equal(session.place(0, 5n, 501), '')
    const before = view(session)
    const envelope = store.saved
    store.limit = envelope.length
    assert.match(session.place(1, 10n, 502), /journal is full/i)
    assert.equal(store.saved, envelope)
    assert.deepEqual(view(session).bets, before.bets)
    session.delete()
    session = mustStart(store)
    const refunded = view(session)
    assert.equal(refunded.state, 'Fault')
    assert.equal(refunded.fault, 'PowerLoss')
    assert.equal(refunded.escrow, '0')
    assert.equal(refunded.events.filter(e => e.kind === 'BetRefunded').length, before.bets.length)
    assert.ok(refunded.seats.every(s => s.credits === '1000' && s.gamesPlayed === '0'))
    const once = store.saved
    session.delete()
    session = mustStart(store)
    assert.equal(store.saved, once)
    assert.deepEqual(view(session).events, refunded.events)
  } finally { if (!session.isDeleted()) session.delete() }
})

test('malformed envelopes and checksummed invalid event payloads fail before any write', () => {
  const good = new Store()
  let session = mustStart(good)
  let open, paid
  try {
    tick(session, 500)
    open = good.saved
    tick(session, 20500, 21700, 23300, 24300, 29300)
    paid = good.saved
  } finally { if (!session.isDeleted()) session.delete() }
  const bitFlip = Buffer.from(open, 'base64')
  bitFlip[12] ^= 1
  for (const value of ['', 'not base64', Buffer.from([0, 0, 0]).toString('base64'),
    bitFlip.toString('base64'), corrupt('bad-kind', open), corrupt('negative-return', paid)]) {
    const broken = new Store(value)
    session = start(broken)
    try {
      assert.match(session.start(0), /damaged/i)
      assert.equal(broken.saved, value)
    } finally { if (!session.isDeleted()) session.delete() }
  }
})

test('unavailable Web Storage leaves the journal untouched', () => {
  const store = fundedStore()
  const original = store.saved
  store.denied = true
  const session = start(store)
  try {
    assert.match(session.start(0), /storage is unavailable/i)
    assert.equal(store.saved, original)
  } finally { if (!session.isDeleted()) session.delete() }
})
