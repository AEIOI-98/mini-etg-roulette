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

function nativeTrace() {
  const dir = mkdtempSync(join(tmpdir(), 'roulette-parity-'))
  const sources = dir => readdirSync(join(root, dir)).filter(s => s.endsWith('.cpp')).map(s => join(dir, s))
  try {
    const binary = join(dir, 'native-trace')
    execFileSync(process.env.CXX || 'c++', ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
      '-Icore/include', '-Isim/shared/include', '-Iweb/wasm', ...sources('core/src'), ...sources('sim/shared/src'),
      'web/wasm/catalog.cpp', 'web/wasm/history.cpp', 'web/wasm/session.cpp', 'web/test/native_trace.cpp',
      '-o', binary], { cwd: root, stdio: 'inherit' })
    return execFileSync(binary, { input: fixture.join('\n') + '\n', encoding: 'utf8', maxBuffer: 10e6 })
      .trimEnd().split('\n').map(line => {
        const [answer, view] = line.split('\t')
        return { answer, view: JSON.parse(view) }
      })
  } finally { rmSync(dir, { recursive: true, force: true }) }
}

test('native and WASM agree on all meters, bets, pockets, events, recovery and RTP', async () => {
  const saved = new Map()
  globalThis.localStorage = {
    getItem: key => saved.get(key) ?? null,
    setItem: (key, value) => saved.set(key, value)
  }
  const wasmBinary = readFileSync(new URL('../public/engine/roulette.wasm', import.meta.url))
  const wasm = await createModule({ wasmBinary })
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
