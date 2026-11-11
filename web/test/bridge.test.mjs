// Run after npm run wasm. Compare the production bindings and localStorage
// adapter to the same timestamped trace compiled as native C++ with a RAM journal.
import assert from 'node:assert/strict'
import { execFileSync } from 'node:child_process'
import { mkdtempSync, readFileSync, readdirSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { fileURLToPath } from 'node:url'
import { test } from 'node:test'
import createModule from '../public/engine/roulette.mjs'

const root = fileURLToPath(new URL('../../', import.meta.url))
const fixture = []
function step(command, now, ...args) {
  fixture.push([command, now, ...args].join(' '))
  return fixture.length - 1
}
step('start', 0, 42)
step('tick', 500)
step('place', 510, 0, 5)
step('undo', 520)
step('place', 530, 1, 25)
step('clear', 540)
step('place', 550, 2, 50)
const exceeded = step('place', 560, 2, 100)
const huge = step('place', 570, 0, '9007199254740993')
const late = step('place', 20500, 3, 1)
step('tick', 21700)
step('tick', 23300)
const recorded = step('tick', 24300)
const recoveredPaid = step('start', 0, 43)
const alreadyPaid = step('start', 0, 44)
step('tick', 3500)
step('tick', 4000)
const repeated = step('repeat', 4100)
const recoveredFault = step('start', 0, 45)
const alreadyRefunded = step('start', 0, 46)
step('tick', 6500)
step('tick', 7000)
step('fault', 7050, 'InvalidSensor')
step('tick', 27000)
step('tick', 28200)
step('tick', 29800)
const badSensor = step('tick', 30800)
step('tick', 37300)
step('tick', 37800)
step('fault', 37900, 'BallTimeout')
step('tick', 57800)
step('tick', 59000)
step('tick', 60600)
const noBall = step('tick', 64600)
step('tick', 71100)
step('tick', 71600)
step('tick', 91600)
step('tick', 92800)
step('tick', 94400)
step('tick', 95400)
const paid = step('tick', 100400)

function nativeTrace(customTable = false) {
  const dir = mkdtempSync(join(tmpdir(), 'roulette-parity-'))
  const sources = dir => readdirSync(join(root, dir)).filter(s => s.endsWith('.cpp')).map(s => join(dir, s))
  try {
    const binary = join(dir, 'native-trace')
    execFileSync(process.env.CXX || 'c++', ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
      '-Icore/include', '-Isim/shared/include', '-Iweb/wasm', ...sources('core/src'), ...sources('sim/shared/src'),
      'web/wasm/catalog.cpp', 'web/wasm/history.cpp', 'web/wasm/session.cpp', 'web/test/native_trace.cpp',
      '-o', binary], { cwd: root, stdio: 'inherit' })
    return execFileSync(binary, customTable ? ['custom-table'] : [], { input: fixture.join('\n') + '\n', encoding: 'utf8', maxBuffer: 10e6 })
      .trimEnd().split('\n').map(line => {
        const [answer, view] = line.split('\t')
        return { answer, view: JSON.parse(view) }
      })
  } finally { rmSync(dir, { recursive: true, force: true }) }
}

async function isolatedEngine() {
  const saved = new Map()
  Object.defineProperty(globalThis, 'localStorage', { configurable: true, value: {
    getItem: key => saved.get(key) ?? null,
    setItem: (key, value) => saved.set(key, value)
  } })
  const wasmBinary = readFileSync(new URL('../public/engine/roulette.wasm', import.meta.url))
  return createModule({ wasmBinary })
}

test('native and WASM agree on all meters, bets, pockets, events, recovery and RTP', async () => {
  const wasm = await isolatedEngine()
  const expected = nativeTrace()
  let session
  try {
    const states = fixture.map((line, i) => {
      const [command, clock, arg, stake] = line.split(' ')
      const now = Number(clock)
      let answer
      if (command === 'start') {
        session?.delete()
        session = new wasm.BrowserSession(Number(arg))
        answer = session.start(now)
      } else if (command === 'place') answer = session.place(Number(arg), BigInt(stake), now)
      else if (command === 'fault') answer = session.armFault(arg, now)
      else answer = session[command](now)
      const view = JSON.parse(session.view())
      assert.equal(session.revision(), BigInt(view.revision))
      assert.deepEqual({ answer, view }, expected[i], line)
      return { answer, view }
    })
    const view = index => states[index].view
    assert.match(states[exceeded].answer, /limit/i)
    assert.match(states[huge].answer, /limit/i)
    assert.equal(view(late).state, 'BetsClosing')
    assert.match(states[late].answer, /closed/i)
    assert.equal(view(recorded).state, 'Result')
    assert.equal(view(recorded).charts.rounds, 0)
    assert.equal(view(recoveredPaid).state, 'Payout')
    assert.equal(view(recoveredPaid).charts.rounds, 1)
    assert.deepEqual(view(alreadyPaid).seats, view(recoveredPaid).seats)
    assert.deepEqual(view(alreadyPaid).charts, view(recoveredPaid).charts)
    assert.equal(view(repeated).bets.filter(b => b.seat === 0)[0].stake, '50')
    assert.equal(view(recoveredFault).fault, 'PowerLoss')
    assert.deepEqual(view(alreadyRefunded).seats, view(recoveredFault).seats)
    for (const [index, fault] of [[badSensor, 'InvalidSensor'], [noBall, 'BallTimeout']]) {
      assert.equal(view(index).state, 'Fault')
      assert.equal(view(index).fault, fault)
      assert.equal(view(index).escrow, '0')
      assert.deepEqual(view(index).charts, view(recoveredPaid).charts)
    }
    assert.equal(view(paid).state, 'Payout')
    assert.equal(view(paid).charts.rounds, 2)
    assert.equal(view(paid).charts.histogram.reduce((a, b) => a + b), 2)
    const catalog = JSON.parse(session.catalog())
    assert.equal(catalog.selections.length, 157)
    assert.equal(new Set(catalog.selections.map(s => s.kind + s.numbers.join(','))).size, 157)
    assert.equal(new Set(catalog.wheel.map(p => p.number)).size, 37)
  } finally { session?.delete() }
})

test('theoretical line follows the settled wager mix under a different Lucky configuration', () => {
  const { view } = nativeTrace(true)[recoveredPaid]
  // Native fixture: 5/37 boosted numbers at 100x, remaining numbers at 6x.
  // First round settled one 50-credit human straight and one 5-credit bot
  // straight, with 50 credits in outside/inside non-straight bot wagers.
  const straightRtp = (6 + 5 / 37 * (100 - 6)) / 37
  const expected = (straightRtp * 55 + (36 / 37) * 50) / 105
  assert.ok(Math.abs(view.charts.series[0].theoretical - expected) < 1e-12)
  assert.ok(Math.abs(view.charts.series[0].observed - (105 - Number(view.house)) / 105) < 1e-12)
})

test('reload before detection refunds once at every pre-result state; Idle preserves funds', async () => {
  for (const [before, times] of [
    ['Idle', []], ['BetsOpen', [500]], ['BetsClosing', [500, 20500]],
    ['BetsClosed', [500, 20500, 21700]], ['Spinning', [500, 20500, 21700, 23300]]
  ]) {
    const wasm = await isolatedEngine()
    function newSession() {
      const instance = new wasm.BrowserSession(42)
      assert.equal(instance.start(0), '')
      return instance
    }
    let session = newSession()
    try {
      for (const now of times) {
        assert.equal(session.tick(now), '')
        if (now === 500) assert.equal(session.place(0, 25n, 505), '')
      }
      assert.equal(JSON.parse(session.view()).state, before)
      session.delete()
      session = newSession()
      const first = JSON.parse(session.view())
      assert.equal(first.state, before === 'Idle' ? 'Idle' : 'Fault')
      assert.equal(first.fault, before === 'Idle' ? 'None' : 'PowerLoss')
      assert.equal(first.escrow, '0')
      assert.ok(first.seats.every(s => s.credits === '1000' && s.gamesPlayed === '0'))
      session.delete()
      session = newSession()
      const second = JSON.parse(session.view())
      assert.equal(second.revision, first.revision)
      assert.deepEqual(second.seats, first.seats)
      assert.deepEqual(second.charts, first.charts)
    } finally { session?.delete() }
  }
})

test('a background-tab clock jump captures the sensor timestamp and pays the recorded pocket', async () => {
  const wasm = await isolatedEngine()
  const session = new wasm.BrowserSession(42)
  try {
    assert.equal(session.start(0), '')
    for (const now of [500, 20500, 21700, 23300]) assert.equal(session.tick(now), '')
    // The ball was sensed at 24,300, even though the tab woke an hour later.
    const wake = 3623300
    assert.equal(session.tick(wake), '')
    const captured = JSON.parse(session.view())
    assert.equal(captured.state, 'Result')
    // No animation callback or rendering participates in settlement.
    assert.equal(session.tick(wake + 3600000), '')
    const paid = JSON.parse(session.view())
    assert.equal(paid.state, 'Payout')
    assert.deepEqual(paid.result, captured.result)
    assert.equal(paid.charts.histogram[captured.result.number], 1)
  } finally { session.delete() }
})

test('out-of-range JS values cannot wrap into a valid one-credit or number-zero wager', async () => {
  const wasm = await isolatedEngine()
  const session = new wasm.BrowserSession(42)
  try {
    assert.equal(session.start(0), '')
    assert.equal(session.tick(500), '')
    for (const stake of [(1n << 64n) + 1n, -(1n << 64n) + 1n, 1, 1.5, '1']) {
      assert.match(session.place(0, stake, 501), /stake/i)
    }
    for (const id of [2 ** 32, 0.5, -1, NaN, Infinity, '0', 0n, null]) {
      assert.match(session.place(id, 1n, 501), /position/i)
    }
    const view = JSON.parse(session.view())
    assert.equal(view.seats[0].credits, '1000')
    assert.equal(view.bets.filter(b => b.seat === 0).length, 0)
    assert.equal(view.events.filter(e => e.kind === 'CommandRejected').length, 13)
  } finally { session.delete() }
})
