// Exercise the production bundle behind a repository subpath, like Pages.
// CHROME_BIN may point to an existing local Chrome installation.
import assert from 'node:assert/strict'
import { readFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve, sep } from 'node:path'
import { fileURLToPath } from 'node:url'
import { test } from 'node:test'
import { checkPlayer } from './player-browser.mjs'
import { chromium } from 'playwright-core'

test('Pages assets, shared seat meters, fault refunds and writer handoff in Chrome', async () => {
  const dist = fileURLToPath(new URL('../dist/', import.meta.url))
  const prefix = '/mini-etg-roulette/'
  const server = createServer(async (req,res) => {
    const path = new URL(req.url,'http://localhost').pathname
    const file = resolve(dist, path.slice(prefix.length) || 'index.html')
    if (!path.startsWith(prefix) || !file.startsWith(resolve(dist)+sep)) {
      res.writeHead(404).end(); return
    }
    const type = { html:'text/html', js:'application/javascript', mjs:'application/javascript',
      css:'text/css', wasm:'application/wasm' }[file.split('.').at(-1)]
    try { const bytes = await readFile(file); res.writeHead(200,{'Content-Type':type || 'application/octet-stream'}).end(bytes) }
    catch { res.writeHead(404).end() }
  })
  await new Promise(resolve => server.listen(0,'127.0.0.1',resolve))
  let browser
  try {
    browser = await chromium.launch({executablePath:process.env.CHROME_BIN || undefined, headless:true})
    const context = await browser.newContext({viewport:{width:1366,height:768}})
    const page = await context.newPage()
    const errors = []
    page.on('pageerror', e => errors.push(e.message))
    const url = 'http://127.0.0.1:'+server.address().port+prefix
    const asset = await context.request.get(url+'engine/roulette.wasm')
    assert.equal(asset.headers()['content-type'],'application/wasm')
    assert.deepEqual([...((await asset.body()).subarray(0,4))],[0,97,115,109])
    await page.clock.install({time:new Date('2026-01-01T12:00:00Z')})
    await page.clock.pauseAt(new Date('2026-01-01T12:01:00Z'))
    await page.goto(url+'#/play')
    await page.getByText('Table live',{exact:true}).waitFor()
    const move = async (ms) => { await page.clock.fastForward(ms+160) }
    await move(600)
    await page.getByText('Bets open',{exact:true}).waitFor()
    await checkPlayer(page)
    const spot = page.locator('.etg-spot.number').first()
    await spot.click()
    assert.equal(await page.locator('[data-node="credits"]').innerText(),'999')
    await page.getByRole('link',{name:'Operator',exact:true}).click()
    const seat = page.locator('[data-op="seats"] tr').first()
    assert.equal(await seat.locator('td').nth(1).innerText(),'999')
    for (const [width,height] of [[1920,1080],[1366,768],[1280,720]]) {
      await page.setViewportSize({width,height})
      assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth))
      for (const label of ['Seat meters','Fault injection','Return to player','Result frequency','Event journal'])
        assert.ok(await page.getByRole('heading',{name:label}).isVisible())
    }
    const mirror = await context.newPage()
    await mirror.goto(url+'#/operator')
    await mirror.getByText('Read-only mirror',{exact:true}).waitFor()
    assert.equal(await mirror.getByRole('button',{name:'Bad sensor reading'}).isDisabled(),true)
    for (const [button,timeout] of [['Bad sensor reading',1100],['Ball not detected',4100]]) {
      await page.getByRole('button',{name:button}).click()
      for (const duration of [20100,1300,1700,timeout]) await move(duration)
      await page.getByText('Fault',{exact:true}).waitFor()
      assert.equal(await seat.locator('td').nth(1).innerText(),'1,000')
      assert.match(await page.locator('[data-op="log"]').innerText(),/BetRefunded/)
      assert.equal(await page.locator('[data-op="count"]').innerText(),'0 settled rounds')
      await move(6600); await move(600)
    }
    await page.getByRole('link',{name:'Player terminal',exact:true}).click()
    await page.getByRole('button', {name:'10 credit chip', exact:true}).click()
    await page.getByRole('button', {name:'Red', exact:true}).click()
    assert.equal(await page.locator('[data-node="credits"]').innerText(), '990')
    assert.equal(await page.locator('[data-node="stake"]').innerText(), '10')
    // Watch a normal recorded result, leave the animation by switching routes,
    // and confirm the core still settles it exactly once.
    for (const [duration,phase] of [[20100,'No more bets'],[1300,'Bets closed'],[1700,'Spinning'],[1100,'Result']]) {
      await move(duration)
      assert.equal(await page.locator('[data-node="phase"]').innerText(),phase)
    }
    assert.ok(await page.locator('.etg-lucky-pick').count() > 0)
    const resultColour = await page.locator('[data-node="clock"]').getAttribute('class')
    assert.match(resultColour, /\bn-(red|black|green)\b/)
    const expectedBalance = resultColour.includes('n-red') ? '1,010' : '990'
    await page.getByRole('link',{name:'Operator',exact:true}).click()
    await move(5100)
    assert.equal(await page.locator('[data-op="state"]').innerText(),'Payout')
    assert.equal(await page.locator('[data-op="count"]').innerText(),'1 settled round')
    const paidBalance = await seat.locator('td').nth(1).innerText()
    assert.equal(paidBalance, expectedBalance)
    assert.equal(await page.locator('[data-op="escrow"]').innerText(), '0')
    await move(3600); await move(600)
    await page.getByRole('link',{name:'Player terminal',exact:true}).click()
    await page.getByRole('button',{name:'Repeat last'}).click()
    assert.equal(await page.locator('[data-node="stake"]').innerText(),'10')
    // Closing the writer simulates a crash during open betting. The waiting mirror
    // must acquire the lock before recovery and publish a single refund.
    await page.close()
    await mirror.getByText('Table live',{exact:true}).waitFor()
    await mirror.locator('#notice').filter({hasText:'outstanding stakes refunded'}).waitFor()
    assert.equal(await mirror.locator('[data-op="seats"] tr').first().locator('td').nth(1).innerText(),paidBalance)
    assert.equal(await mirror.locator('[data-op="escrow"]').innerText(),'0')
    await mirror.reload()
    await mirror.getByText('Table live',{exact:true}).waitFor()
    assert.equal(await mirror.locator('[data-op="seats"] tr').first().locator('td').nth(1).innerText(),paidBalance)
    assert.deepEqual(errors,[])
  } finally {
    await browser?.close()
    await new Promise(resolve=>server.close(resolve))
  }
})
