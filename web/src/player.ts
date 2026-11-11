import { BettingBoard } from './board'
import { Wheel } from './wheel'
import type { Catalog, RoundState, SessionView } from './bridge-types'
import './player.css'

export interface PlayerActions {
  place(id: number, stake: bigint): string
  undo(): string
  clear(): string
  repeat(): string
}

const format = (value: bigint) => value.toLocaleString('en-US')
const stage: Record<RoundState, string> = {
  Idle: 'Between rounds', BetsOpen: 'Bets open', BetsClosing: 'No more bets',
  BetsClosed: 'Bets closed', Spinning: 'Spinning', Result: 'Result', Payout: 'Payout', Fault: 'Round voided',
}
const readable = (word: string) => word.replace(/([a-z])([A-Z])/g, '$1 $2').toLowerCase()
const chips = [1n, 5n, 10n, 25n, 100n]
export class Player {
  private readonly board: BettingBoard
  private readonly wheel: Wheel
  private readonly pocketColours: Map<number, string>
  private readonly nodes: Record<string, HTMLElement>
  private readonly chipButtons: HTMLButtonElement[] = []
  private readonly editButtons: Record<'undo' | 'clear' | 'repeat', HTMLButtonElement>
  private chip = 1n
  private view?: SessionView
  private revision?: bigint
  private writable?: boolean
  private recentKey = ''
  private luckyKey = ''

  constructor(private host: HTMLElement, catalog: Catalog, private actions: PlayerActions) {
    this.pocketColours = new Map(catalog.wheel.map(p => [p.number, p.colour]))
    this.host.innerHTML = `
      <div class="etg-player">
        <div class="etg-player-head">
          <div><div class="etg-meta etg-seat"><span class="etg-seat-mark" aria-hidden="true"></span>Seat 00 <i></i> Player</div>
            <h1>Player terminal</h1></div>
          <div class="etg-meters" aria-label="Seat credits">
            <div><span>Balance</span><b data-node="credits"></b></div>
            <div><span>Round stake</span><b data-node="stake"></b></div>
          </div>
        </div>
        <div class="etg-player-grid">
          <section class="etg-card etg-wheel-card" aria-labelledby="etg-wheel-title">
            <div class="etg-card-head"><h2 id="etg-wheel-title">European wheel</h2>
              <span class="etg-round" data-node="round"></span></div>
            <div class="etg-wheel-field"><div class="etg-wheel-drawing" data-node="wheel"></div></div>
            <div class="etg-wheel-state">
              <div><div class="etg-meta">Table status</div><h3 data-node="phase" aria-live="polite"></h3></div>
              <span class="etg-clock" data-node="clock"></span>
            </div>
            <div class="etg-recent"><span class="etg-meta">Recent results</span>
              <div data-node="recent" aria-label="Recent results, newest first"></div></div>
          </section>
          <section class="etg-card etg-board-card" aria-labelledby="etg-board-title">
            <div class="etg-card-head"><h2 id="etg-board-title">Betting layout</h2>
              <div class="etg-switch" aria-label="Bet input method">
                <button data-node="tableMode" type="button" aria-pressed="true">Table</button>
                <button data-node="listMode" type="button" aria-pressed="false">List</button>
              </div></div>
            <div class="etg-layout" data-node="board"></div>
            <div class="etg-board-hint"><span class="etg-cross" aria-hidden="true">⌖</span><span data-node="preview"></span></div>
            <div class="etg-chip-row">
              <div class="etg-chip-group" role="group" aria-label="Chip value"><span class="etg-meta">Chip value</span>
                <div class="etg-chips" data-node="chips"></div></div>
              <div class="etg-edit" role="group" aria-label="Edit bets">
                <button data-node="undo" type="button">Undo</button><button data-node="clear" type="button">Clear</button>
                <button data-node="repeat" type="button">Repeat last</button>
              </div></div>
            <div class="etg-feedback" data-node="feedback" role="status"></div>
            <div class="etg-lucky"><div class="etg-lucky-heading"><span class="etg-spark" aria-hidden="true">✧</span><h3>Lucky numbers</h3></div>
              <div class="etg-lucky-picks" data-node="lucky"></div></div>
          </section>
        </div>
      </div>`
    this.nodes = Object.fromEntries(Array.from(this.host.querySelectorAll<HTMLElement>('[data-node]'),
      e => [e.dataset.node!, e]))
    const n = this.nodes
    this.board = new BettingBoard(n.board, catalog, id => {
      const selection = catalog.selections.find(s => s.id === id)
      const answer = this.actions.place(id, this.chip)
      this.feedback(answer, format(this.chip) + ' on ' + (selection?.label ?? 'selection') + '.')
    }, selection => { n.preview.textContent = selection?.label ?? 'Tap a number, line or intersection to place a chip.' })
    n.preview.textContent = 'Tap a number, line or intersection to place a chip.'
    this.wheel = new Wheel(n.wheel, catalog.wheel)
    for (const value of chips) {
      const button = document.createElement('button')
      button.type = 'button'
      button.className = 'etg-chip chip-' + value
      button.textContent = String(value)
      button.setAttribute('aria-label', value + ' credit chip')
      button.setAttribute('aria-pressed', String(value === this.chip))
      button.addEventListener('click', () => {
        this.chip = value
        for (const b of this.chipButtons) b.setAttribute('aria-pressed', String(b === button))
      })
      this.chipButtons.push(button); n.chips.append(button)
    }
    this.editButtons = { undo: n.undo as HTMLButtonElement, clear: n.clear as HTMLButtonElement, repeat: n.repeat as HTMLButtonElement }
    const messages = { undo: 'Last placement undone.', clear: 'All your placements cleared.', repeat: 'Previous bets repeated.' }
    for (const command of ['undo', 'clear', 'repeat'] as const) {
      this.editButtons[command].addEventListener('click', () => this.feedback(actions[command](), messages[command]))
    }
    const switchMode = (list: boolean) => {
      this.board.showList(list)
      n.listMode.setAttribute('aria-pressed', String(list)); n.tableMode.setAttribute('aria-pressed', String(!list))
    }
    n.tableMode.onclick = () => switchMode(false)
    n.listMode.onclick = () => switchMode(true)
  }
  private feedback(code: string, success: string) {
    const ok = !code
    this.nodes.feedback.textContent = ok ? success : code
    this.nodes.feedback.classList.toggle('error', !ok)
  }
  update(view: SessionView, remainingMs: number, writable: boolean) {
    const n = this.nodes
    const previous = this.view
    this.view = view
    const live = view.state === 'BetsOpen'
    const seconds = String(Math.max(0, Math.ceil(remainingMs / 1000))).padStart(2, '0')
    const clock = live ? '00:' + seconds : view.result ? String(view.result.number) : '—'
    if (n.clock.textContent !== clock) n.clock.textContent = clock
    n.clock.className = 'etg-clock' + (live ? ' open' : view.result ? ' result n-' + this.pocketColours.get(view.result.number) : '')
    if (view.result) this.wheel.show(view.roundId, view.result.pocket, view.state === 'Result' ? Math.max(0, remainingMs - 1300) : 0)
    else this.wheel.finish()
    if (this.revision === view.revision && this.writable === writable) return
    this.revision = view.revision; this.writable = writable
    n.round.textContent = 'Round ' + String(view.roundId).padStart(3, '0')
    n.credits.textContent = format(view.seats[0].credits)
    const ownBets = view.bets.filter(b => b.seat === 0)
    n.stake.textContent = format(ownBets.reduce((sum, b) => sum + b.stake, 0n))
    n.phase.textContent = stage[view.state]
    n.phase.classList.toggle('open', live)
    n.phase.classList.toggle('fault', view.state === 'Fault')
    this.board.setEnabled(writable && live)
    this.board.setBets(ownBets)
    for (const button of this.chipButtons) button.disabled = !(writable && live)
    this.editButtons.clear.disabled = this.editButtons.undo.disabled = !(writable && live && ownBets.length)
    this.editButtons.repeat.disabled = !(writable && live && view.canRepeat)
    if (previous?.state !== view.state || previous.roundId !== view.roundId || !writable) {
      n.feedback.classList.remove('error')
      let message = 'Table limits · ' + format(view.limits.min) + '–' + format(view.limits.max) +
        ' per selection · ' + format(view.limits.seatMax) + ' per seat'
      if (view.state === 'Fault') message = 'Round voided · ' + readable(view.fault) + '. All stakes refunded.'
      else if (view.state === 'Payout') message = 'Settled · ' + format(view.playerReturn) + ' credits returned to your seat.'
      else if (view.state === 'Spinning') message = 'Detecting ball position…'
      else if (view.state === 'Result') message = 'Recorded result · ' + view.result?.number
      n.feedback.textContent = message + (writable ? '' : ' · Read-only seat')
    }
    const recentKey = view.recentResults.join(',')
    if (!n.recent.childElementCount || recentKey !== this.recentKey) {
      this.recentKey = recentKey
      const nodes = view.recentResults.slice(0, 8).map(number => {
        const el = document.createElement('span')
        el.className = 'etg-result n-' + this.pocketColours.get(number)
        el.textContent = String(number); return el
      })
      if (!nodes.length) {
        const pending = document.createElement('span')
        pending.className = 'etg-empty'; pending.textContent = 'Awaiting first result'; nodes.push(pending)
      }
      n.recent.replaceChildren(...nodes)
    }
    const luckyKey = [view.roundId, view.luckyEnabled, ...view.lucky.flatMap(l => [l.number, l.multiplier])].join(',')
    if (luckyKey !== this.luckyKey) {
      this.luckyKey = luckyKey
      const picks = view.lucky.map((l, index) => {
        const el = document.createElement('span')
        el.className = 'etg-lucky-pick'
        el.style.animationDelay = index * 70 + 'ms'
        const multiplier = document.createElement('b')
        multiplier.textContent = l.multiplier + '×'; el.append(String(l.number), multiplier); return el
      })
      if (!picks.length) {
        const info = document.createElement('span'); info.className = 'etg-empty'
        info.textContent = view.luckyEnabled ? 'Revealed when bets close' : 'Standard mode'; picks.push(info)
      }
      n.lucky.replaceChildren(...picks)
    }
  }
  destroy() { this.wheel.destroy(); this.host.replaceChildren() }
}
