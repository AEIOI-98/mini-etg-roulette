import assert from 'node:assert/strict'
import { readFile } from 'node:fs/promises'
import { test } from 'node:test'
import ts from 'typescript'

// These two view modules have no runtime dependencies. Transpile them in
// memory so the tests work with the same Node releases supported by Vite.
async function viewModule(name) {
  const code = await readFile(new URL('../src/' + name + '.ts', import.meta.url), 'utf8')
  const { outputText } = ts.transpileModule(code, {
    compilerOptions: { target: ts.ScriptTarget.ES2022, module: ts.ModuleKind.ES2022 }
  })
  return import('data:text/javascript;base64,' + Buffer.from(outputText).toString('base64'))
}

test('wheel lands in every requested pocket from successive previous results', async () => {
  const { targetPose, interpolate } = await viewModule('wheel')
  const tau = Math.PI * 2
  let from = { rotor: 0, ball: -.48, radius: .918 }
  for (let index = 0; index < 37; index++) {
    const target = targetPose(from, index, 37)
    const initial = interpolate(from, target, 0)
    const final = interpolate(from, target, 1)
    for (const key of ['rotor','ball','radius']) {
      assert.ok(Math.abs(initial[key] - from[key]) < 1e-10)
      assert.ok(Math.abs(final[key] - target[key]) < 1e-10)
    }
    const relative = Math.round((final.ball - final.rotor) * 37 / tau)
    assert.equal(((relative % 37) + 37) % 37, index)
    assert.ok(Math.abs(final.radius - .706) < 1e-10)
    const half = interpolate(from, target, .5)
    assert.ok(Math.abs(half.ball - from.ball) > Math.abs(final.ball - half.ball))
    assert.ok(Math.abs(half.rotor - from.rotor) > Math.abs(final.rotor - half.rotor))
    from = final
  }
})

test('all 157 engine selections have unique reachable targets, including zero combinations', async () => {
  const { default: createEngine } = await import('../public/engine/roulette.mjs')
  const wasmBinary = await readFile(new URL('../public/engine/roulette.wasm', import.meta.url))
  const engine = await createEngine({ wasmBinary })
  const session = new engine.BrowserSession(42)
  let catalog
  try { catalog = JSON.parse(session.catalog()) } finally { session.delete() }
  const { layoutSpot } = await viewModule('board')
  assert.equal(catalog.selections.length, 157)
  const spots = catalog.selections.map(s => ({ ...layoutSpot(s), selection: s }))
  const top = (x, y) => spots.filter(p => x >= p.x && x <= p.x + p.w && y >= p.y && y <= p.y + p.h)
    .sort((a, b) => b.layer - a.layer || b.selection.id - a.selection.id)[0]?.selection
  for (const p of spots) {
    assert.ok(p.x >= 0 && p.y >= 0 && p.x + p.w <= 14 && p.y + p.h <= 5.101)
    assert.equal(top(p.x + p.w / 2, p.y + p.h / 2)?.id, p.selection.id, p.selection.label)
  }
  assert.deepEqual(top(1, 1).numbers, [0, 2, 3])
  assert.deepEqual(top(1, 2).numbers, [0, 1, 2])
  assert.deepEqual(top(1, 3).numbers, [0, 1, 2, 3])
  assert.equal(top(2, 3).kind, 'SixLine')
  assert.equal(top(2, 1).kind, 'Corner')
  assert.equal(top(2, .5).kind, 'Split')
})
