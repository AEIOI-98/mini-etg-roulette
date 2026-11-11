import type { Catalog, Selection, SessionView } from './bridge-types'

// Layout coordinates are measured in number-cell widths/heights. Geometry here
// only positions selections which the C++ catalog has already supplied.
export interface Spot {
  x: number; y: number; w: number; h: number
  layer: number; style: string; text: string
}

const boardWidth = 14
const boardHeight = 5.1
const centre = (n: number) => ({ x: 1.5 + Math.floor((n - 1) / 3), y: 2.5 - (n - 1) % 3 })
const rect = (x: number, y: number, w: number, h: number, layer: number, style: string, text = ''): Spot =>
  ({ x, y, w, h, layer, style, text })
const intersection = (x: number, y: number, style: string) =>
  rect(x - 0.21, y - 0.21, 0.42, 0.42, 4, style)

export function layoutSpot(s: Selection): Spot {
  const [first, second] = s.numbers
  const start = centre(first)
  switch (s.kind) {
    case 'Straight':
      return first === 0 ? rect(0, 0, 1, 3, 0, 'number zero', '0')
        : rect(start.x - .5, start.y - .5, 1, 1, 0, 'number', String(first))
    case 'Split': {
      const end = centre(second)
      const verticalEdge = first === 0 || end.y === start.y
      const x = first === 0 ? 1 : (start.x + end.x) / 2
      const y = first === 0 ? end.y : (start.y + end.y) / 2
      return verticalEdge ? rect(x - .16, y - .5, .32, 1, 2, 'edge')
        : rect(x - .5, y - .16, 1, .32, 2, 'edge')
    }
    case 'Street':
      return first === 0 ? intersection(1, centre(second).y - .5, 'joint')
        : rect(start.x - .5, 2.84, 1, .53, 2, 'street')
    case 'Corner':
      return first === 0 ? intersection(1, 3.08, 'joint baseline')
        : intersection(start.x + .5, start.y - .5, 'joint')
    case 'SixLine':
      return intersection(start.x + .5, 3.08, 'joint baseline')
    case 'Column':
      return rect(13, start.y - .5, 1, 1, 0, 'outside column', '2 : 1')
    case 'Dozen':
      return rect(start.x - .5, 3.43, 4, .8, 0, 'outside', [ '1st 12', '2nd 12', '3rd 12' ][Math.floor((first - 1) / 12)])
    default: {
      const labels = { Low: '1 – 18', Even: 'Even', Red: 'Red', Black: 'Black', Odd: 'Odd', High: '19 – 36' }
      const index = Object.keys(labels).indexOf(s.kind)
      return rect(1 + 2 * index, 4.27, 2, .83, 0, 'outside ' + s.kind.toLowerCase(), labels[s.kind])
    }
  }
}

export class BettingBoard {
  private buttons = new Map<number, HTMLButtonElement>()
  private numbers = new Map<number, HTMLButtonElement>()
  private markers = new Map<number, HTMLElement>()
  private highlighted: number[] = []
  private enabled = false
  private readonly grid = document.createElement('div')
  private readonly list = document.createElement('div')
  private readonly type = document.createElement('select')
  private readonly selection = document.createElement('select')
  private readonly submit = document.createElement('button')
  private readonly colours: Map<number, string>

  constructor(private host: HTMLElement, private catalog: Catalog,
    private onPlace: (id: number) => void, private onPreview: (s: Selection | null, list: boolean) => void) {
    this.colours = new Map(catalog.wheel.map(p => [p.number, p.colour]))
    this.grid.className = 'etg-felt'
    this.grid.setAttribute('role', 'group')
    this.grid.setAttribute('aria-label', 'Roulette betting layout')
    for (const choice of catalog.selections) this.add(choice)
    this.grid.addEventListener('pointerleave', () => this.preview(null))
    this.grid.addEventListener('focusout', event => {
      if (!this.grid.contains(event.relatedTarget as Node | null)) this.preview(null)
    })

    this.list.className = 'etg-picker'
    const category = document.createElement('label')
    category.textContent = 'Bet type'
    this.type.setAttribute('aria-label', 'Bet type')
    category.append(this.type)
    const bet = document.createElement('label')
    bet.textContent = 'Selection'
    this.selection.setAttribute('aria-label', 'Selection')
    bet.append(this.selection)
    for (const kind of new Set(catalog.selections.map(s => s.kind))) {
      this.type.add(new Option(kind === 'SixLine' ? 'Six line' : kind, kind))
    }
    this.type.addEventListener('change', () => this.fillOptions())
    this.selection.addEventListener('change', () => this.preview(this.current()))
    this.submit.type = 'button'
    this.submit.textContent = 'Place chip'
    this.submit.addEventListener('click', () => {
      const selection = this.current()
      if (this.enabled && selection) this.onPlace(selection.id)
    })
    this.list.append(category, bet, this.submit)
    this.list.hidden = true
    this.host.append(this.grid, this.list)
    this.fillOptions()
  }

  private add(s: Selection) {
    const b = document.createElement('button')
    const p = layoutSpot(s)
    b.type = 'button'
    b.className = 'etg-spot ' + p.style
    if (s.kind === 'Straight') {
      b.classList.add('n-' + this.colours.get(s.numbers[0]))
      this.numbers.set(s.numbers[0], b)
    }
    b.style.left = (p.x / boardWidth * 100) + '%'
    b.style.top = (p.y / boardHeight * 100) + '%'
    b.style.width = (p.w / boardWidth * 100) + '%'
    b.style.height = (p.h / boardHeight * 100) + '%'
    b.style.zIndex = String(p.layer)
    b.textContent = p.text
    b.setAttribute('aria-label', s.label)
    b.title = s.label
    b.dataset.selection = String(s.id)
    b.addEventListener('pointerenter', () => this.preview(s))
    b.addEventListener('focus', () => this.preview(s))
    b.addEventListener('click', () => {
      if (this.enabled) this.onPlace(s.id)
    })
    this.buttons.set(s.id, b)
    this.grid.append(b)
  }

  private current() { return this.catalog.selections.find(s => s.id === Number(this.selection.value)) ?? null }
  private fillOptions() {
    this.selection.replaceChildren()
    for (const s of this.catalog.selections.filter(s => s.kind === this.type.value)) {
      this.selection.add(new Option(s.label, String(s.id)))
    }
    if (!this.list.hidden) this.preview(this.current())
  }
  private preview(s: Selection | null) {
    for (const n of this.highlighted) this.numbers.get(n)?.classList.remove('covered')
    this.highlighted = s?.numbers ?? []
    for (const n of this.highlighted) this.numbers.get(n)?.classList.add('covered')
    this.onPreview(s, !this.list.hidden)
  }
  showList(show: boolean) {
    this.list.hidden = !show
    this.grid.hidden = show
    this.preview(show ? this.current() : null)
  }
  setEnabled(enabled: boolean) {
    this.enabled = enabled
    this.host.classList.toggle('locked', !enabled)
    this.submit.disabled = !enabled
    for (const b of this.buttons.values()) b.disabled = !enabled
  }
  setBets(bets: SessionView['bets']) {
    // Chip counts are a display aggregation of the accepted bets. Only the C++
    // engine can change the balance or decide whether a placement succeeded.
    const totals = new Map<number, bigint>()
    for (const b of bets) if (b.seat === 0) totals.set(b.selectionId, (totals.get(b.selectionId) ?? 0n) + b.stake)
    for (const [id, marker] of this.markers) {
      if (!totals.has(id)) {
        marker.remove()
        this.markers.delete(id)
        this.buttons.get(id)?.removeAttribute('aria-description')
      }
    }
    for (const [id, amount] of totals) {
      const button = this.buttons.get(id)
      if (!button) continue
      let marker = this.markers.get(id)
      if (!marker) {
        marker = document.createElement('span')
        marker.className = 'etg-placed'
        marker.setAttribute('aria-hidden', 'true')
        // Render in a separate pointer-transparent layer so a chip on a number
        // remains visible beside higher-priority split/corner hit targets.
        marker.style.left = (parseFloat(button.style.left) + parseFloat(button.style.width) / 2) + '%'
        marker.style.top = (parseFloat(button.style.top) + parseFloat(button.style.height) / 2) + '%'
        this.grid.append(marker)
        this.markers.set(id, marker)
      }
      marker.textContent = amount.toString()
      button.setAttribute('aria-description', amount + ' credits placed here.')
    }
  }
}
