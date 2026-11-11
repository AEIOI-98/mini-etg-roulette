import type { DemoFault, SessionView } from './bridge-types'
import './operator.css'

const ns = 'http://www.w3.org/2000/svg'
function mark(svg: SVGElement, name: string, attrs: Record<string, string | number>, text?: string) {
  const el = document.createElementNS(ns, name)
  for (const [k,v] of Object.entries(attrs)) el.setAttribute(k, String(v))
  if (text !== undefined) el.textContent = text
  svg.append(el)
  return el
}
const pct = (v: number) => (v * 100).toFixed(1) + '%'
const credit = (v: bigint) => v.toLocaleString('en-US')
function row(parent: HTMLElement, cells: string[], element = 'td') {
  const tr = document.createElement('tr')
  for (const value of cells) {
    const cell = document.createElement(element)
    cell.textContent = value
    tr.append(cell)
  }
  parent.append(tr)
}
function plotRtp(svg: SVGElement, data: SessionView['charts']['series']) {
  svg.replaceChildren()
  if (!data.length) {
    mark(svg,'text',{x:180,y:84,'text-anchor':'middle'},'No settled bets')
    return
  }
  const top = Math.max(1.1, ...data.map(p => Math.max(p.observed,p.theoretical))) * 1.06
  const x = (r:number) => 40 + (r-1) * 312 / Math.max(1, data[data.length-1].round-1)
  const y = (v:number) => 126 - 110 * v / top
  for (const val of [0,top/2,top]) {
    mark(svg,'line',{x1:40,x2:352,y1:y(val),y2:y(val),class:'axis'})
    mark(svg,'text',{x:35,y:y(val)+3,'text-anchor':'end'},Math.round(val*100)+'%')
  }
  for (const key of ['theoretical','observed'] as const) {
    mark(svg,'polyline',{points:data.map(p => x(p.round)+','+y(p[key])).join(' '),class:key,fill:'none'})
    const last = data[data.length-1]
    mark(svg,'circle',{cx:x(last.round),cy:y(last[key]),r:2.7,class:key})
  }
  mark(svg,'text',{x:40,y:146},'Round 1')
  if (data[data.length-1].round > 1)
    mark(svg,'text',{x:352,y:146,'text-anchor':'end'},String(data[data.length-1].round))
}
function plotHistogram(svg: SVGElement, chart: SessionView['charts']) {
  svg.replaceChildren()
  const peak = Math.max(1, ...chart.histogram,chart.expected)
  const y = (v:number) => 126 - 105*v/peak
  chart.histogram.forEach((count,number) => {
    const bar = mark(svg,'rect',{x:28+number*8.8,y:y(count),width:6.2,height:126-y(count),class:'freq'})
    mark(bar,'title',{},'Number '+number+' · '+count+' results')
  })
  mark(svg,'line',{x1:28,x2:352,y1:y(chart.expected),y2:y(chart.expected),class:'theoretical'})
  mark(svg,'text',{x:23,y:24,'text-anchor':'end'},String(peak))
  for (const n of [0,6,12,18,24,30,36]) mark(svg,'text',{x:31+n*8.8,y:146,'text-anchor':'middle'},String(n))
}
export class Operator {
  private revision = -1n
  private readonly find = <T extends Element>(name:string) => this.root.querySelector<T>('[data-op="'+name+'"]')!
  constructor(private root: HTMLElement, fault: (type: DemoFault)=>string) {
    root.innerHTML = `<section class="op">
      <div class="op-heading"><div><p class="op-kicker">TABLE OPERATIONS</p><h1>Operator dashboard</h1></div>
        <div class="op-state"><span>Engine state</span><strong data-op="state"></strong></div></div>
      <div class="op-top"><section class="op-panel"><header><h2>Seat meters</h2><span data-op="round"></span></header>
      <div class="op-table-scroll"><table><thead><tr><th>Seat</th><th>Credits</th><th>Credits in</th><th>Credits out</th><th>Played</th><th>Won</th></tr></thead><tbody data-op="seats"></tbody></table></div>
      <div class="op-totals"><span>House <b data-op="house"></b></span><span>Escrow <b data-op="escrow"></b></span></div></section>
      <section class="op-panel fault-panel"><header><h2>Fault injection</h2></header><p>Arm a sensor fault for the next wheel start. A voided round returns every active stake.</p>
      <div class="op-buttons"><button type="button" data-op="timeout">Ball not detected</button><button type="button" data-op="sensor">Bad sensor reading</button></div>
      <p data-op="armed" role="status"></p><div class="op-reload"><h3>Power-loss demo</h3><p>Reload during a round to recover the last committed journal.</p><button data-op="reload" type="button">Reload &amp; recover</button></div></section></div>
      <div class="op-charts"><section class="op-panel"><header><h2>Return to player</h2><strong data-op="rtp"></strong></header><p class="op-mode" data-op="mode"></p><div class="legend"><span>Observed</span><span>Theoretical</span></div>
      <svg viewBox="0 0 360 154" role="img" aria-label="Cumulative RTP by settled round" data-op="rtp-chart"></svg></section>
      <section class="op-panel"><header><h2>Result frequency</h2><span data-op="count"></span></header><div class="legend"><span>Results by number</span><span>Expected count</span></div>
      <svg viewBox="0 0 360 154" role="img" aria-label="Result counts for numbers zero through thirty-six" data-op="histogram"></svg></section></div>
      <section class="op-panel"><header><h2>Event journal</h2><span>Latest 80 events</span></header>
      <div class="op-log" tabindex="0" aria-label="Scrollable event journal"><table><thead><tr><th>Sequence</th><th>Round</th><th>Event</th><th>Detail</th></tr></thead><tbody data-op="log"></tbody></table></div></section></section>`
    this.find('timeout').addEventListener('click',()=>this.report(fault('BallTimeout')))
    this.find('sensor').addEventListener('click',()=>this.report(fault('InvalidSensor')))
    this.find('reload').addEventListener('click',()=>location.reload())
  }
  private report(message:string) { if (message) this.find('armed').textContent = message }
  update(v: SessionView, writable: boolean) {
    for (const name of ['timeout','sensor','reload']) this.find<HTMLButtonElement>(name).disabled = !writable
    const sensorNames: Record<string,string> = {BallTimeout:'ball not detected',InvalidSensor:'bad sensor reading',PowerLoss:'power loss'}
    this.find('armed').textContent = v.armedFault ? 'Armed for next spin · '+sensorNames[v.armedFault]
      : v.fault && v.fault !== 'None' ? 'Round voided · '+(sensorNames[v.fault] ?? v.fault)+' · stakes refunded' : ''
    if (this.revision === v.revision) return
    this.revision = v.revision
    this.find('state').textContent = v.state.replace(/([a-z])([A-Z])/g,'$1 $2')
    this.find('round').textContent = 'Round '+v.roundId
    const seats = this.find<HTMLElement>('seats')
    seats.replaceChildren()
    for (const s of v.seats) row(seats,[s.seat+' · '+s.name,credit(s.credits),credit(s.creditsIn),credit(s.creditsOut),credit(s.gamesPlayed),credit(s.gamesWon)])
    this.find('house').textContent = credit(v.house)
    this.find('escrow').textContent = credit(v.escrow)
    const log = this.find<HTMLElement>('log')
    log.replaceChildren()
    for (const e of [...v.events].reverse()) row(log,['#'+e.sequence,String(e.roundId),e.kind,e.detail])
    const settled = v.charts.rounds + (v.charts.rounds === 1 ? ' settled round' : ' settled rounds')
    this.find('count').textContent = settled
    this.find('mode').textContent = (v.luckyEnabled ? 'Lucky Numbers' : 'Standard roulette')+' · '+settled
    const last = v.charts.series.at(-1)
    this.find('rtp').textContent = last ? pct(last.observed)+' / '+pct(last.theoretical) : '—'
    plotRtp(this.find('rtp-chart'),v.charts.series)
    plotHistogram(this.find('histogram'),v.charts)
  }
}
