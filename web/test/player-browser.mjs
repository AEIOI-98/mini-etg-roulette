import assert from 'node:assert/strict'

export async function checkPlayer(page) {
  const original = page.viewportSize()
  for (const [width, height] of [[1920,1080],[1366,768],[1280,720]]) {
    await page.setViewportSize({ width, height })
    const report = await page.evaluate(() => {
      const blocked = []
      for (const button of document.querySelectorAll('.etg-spot')) {
        const rect = button.getBoundingClientRect()
        const hit = document.elementFromPoint(rect.x + rect.width / 2, rect.y + rect.height / 2)
        if (hit?.closest('.etg-spot') !== button) blocked.push(button.getAttribute('aria-label'))
      }
      return { blocked, width: document.documentElement.scrollWidth, count: document.querySelectorAll('.etg-spot').length,
        bottom: document.querySelector('.etg-lucky').getBoundingClientRect().bottom }
    })
    assert.equal(report.count,157)
    assert.deepEqual(report.blocked,[], 'Overlapping hit targets at '+width+'×'+height)
    assert.ok(report.width <= width, 'Page overflows horizontally')
    assert.ok(report.bottom <= height,'Player controls extend below landscape viewport')
  }
  await page.setViewportSize(original)
  const press = label => page.getByRole('button',{name:label,exact:true}).click()
  const credit = () => page.locator('[data-node="credits"]').innerText()
  const stake = () => page.locator('[data-node="stake"]').innerText()
  for (const [chip, bet] of [[1,'Split 0/1'],[5,'Street 0/1/2'],[10,'Corner 0/1/2/3'],
    [25,'Six line 1/2/3/4/5/6'],[100,'Dozen 3']]) {
    await press(chip + ' credit chip')
    await press(bet)
    assert.equal(await stake(), String(chip))
    assert.equal(await credit(), String(1000 - chip))
    if (chip === 100) {
      await press(bet)
      assert.match(await page.locator('[data-node="feedback"]').innerText(),/limit/i)
      assert.equal(await stake(),'100')
    }
    await press(chip === 100 ? 'Clear' : 'Undo')
    assert.equal(await stake(),'0')
  }
  await press('1 credit chip')
  await press('List')
  await page.getByLabel('Bet type',{exact:true}).selectOption('Street')
  await page.getByLabel('Selection',{exact:true}).selectOption({label:'Street 0/2/3'})
  await press('Place chip')
  assert.equal(await stake(),'1')
  await press('Clear'); await press('Table')
  assert.equal(await credit(),'1,000')
}
