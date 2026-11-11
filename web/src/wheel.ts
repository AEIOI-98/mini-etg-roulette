import type { Catalog } from './bridge-types'

const tau = 2 * Math.PI
const wrap = (value: number) => ((value % tau) + tau) % tau
export interface Pose { rotor: number; ball: number; radius: number }

// Angles are clockwise from the top of the canvas. The rotor finishes with
// the recorded pocket directly beneath the pointer, and the ball in its slot.
export function targetPose(from: Pose, pocket: number, slots: number): Pose {
  return {
    rotor: from.rotor + 2 * tau + wrap(-pocket * tau / slots - from.rotor),
    ball: from.ball - 4 * tau - wrap(from.ball),
    radius: .706,
  }
}

export function interpolate(from: Pose, to: Pose, progress: number): Pose {
  const t = Math.max(0, Math.min(1, progress))
  const ease = 1 - (1 - t) ** 3
  const travel = Math.min(1, t / .33)
  const drop = Math.max(0, (t - .36) / .64)
  const track = from.radius + (.918 - from.radius) * (1 - (1 - travel) ** 2)
  return { rotor: from.rotor + (to.rotor - from.rotor) * ease,
    ball: from.ball + (to.ball - from.ball) * ease,
    radius: track + (to.radius - track) * drop * drop * (3 - 2 * drop) }
}

type Context = CanvasRenderingContext2D
function circle(ctx: Context, r: number, fill: string | CanvasGradient) {
  ctx.beginPath(); ctx.arc(0, 0, r, 0, tau); ctx.fillStyle = fill; ctx.fill()
}
function ring(ctx: Context, r: number, colour: string, width: number) {
  ctx.beginPath(); ctx.arc(0, 0, r, 0, tau); ctx.strokeStyle = colour; ctx.lineWidth = width; ctx.stroke()
}
function radial(ctx: Context, radius: number, stops: [number, string][]) {
  const gradient = ctx.createRadialGradient(0, 0, 0, 0, 0, radius)
  for (const [offset, colour] of stops) gradient.addColorStop(offset, colour)
  return gradient
}
function metal(ctx: Context, radius: number) {
  const g = ctx.createLinearGradient(-radius, -radius, radius * .6, radius)
  for (const [n, c] of [[0,'#654936'], [.2,'#c79863'], [.41,'#fff1cf'], [.52,'#dbaf79'], [.69,'#9f734d'], [1,'#473930']] as const) {
    g.addColorStop(n, c)
  }
  return g
}
const pocketColors = {
  red: ['#ad4843', '#74322f'], black: ['#27353c', '#17272e'], green: ['#27735d', '#1d5142'],
}

function drawRotor(ctx: Context, wheel: Catalog['wheel']) {
  const step = tau / wheel.length
  for (const [index, pocket] of wheel.entries()) {
    ctx.save(); ctx.rotate(index * step)
    const a = -Math.PI / 2 - step / 2
    const b = a + step
    const [outer, inner] = pocketColors[pocket.colour]
    ctx.beginPath(); ctx.arc(0, 0, 84.5, a, b); ctx.arc(0, 0, 65, b, a, true); ctx.closePath()
    ctx.fillStyle = outer; ctx.fill()
    ctx.lineWidth = .23; ctx.strokeStyle = '#d6c4a9'; ctx.stroke()
    ctx.beginPath(); ctx.arc(0, 0, 73.5, a, b); ctx.arc(0, 0, 65.3, b, a, true); ctx.closePath()
    ctx.fillStyle = inner; ctx.fill()
    ctx.beginPath(); ctx.moveTo(65.5 * Math.cos(a), 65.5 * Math.sin(a))
    ctx.lineTo(74.2 * Math.cos(a), 74.2 * Math.sin(a)); ctx.strokeStyle = '#e4ccb2'; ctx.lineWidth = .68; ctx.stroke()
    ctx.fillStyle = '#fff7e8'; ctx.font = 'bold 6.1px Georgia, serif'; ctx.textAlign = 'center'
    ctx.textBaseline = 'middle'; ctx.fillText(String(pocket.number), 0, -79.1)
    ctx.restore()
  }
  circle(ctx, 65.4, radial(ctx, 66, [[0,'#80543a'], [.27,'#775036'], [.43,'#9a6c48'], [.64,'#ab7650'], [.85,'#68432e'], [1,'#352e29']]))
  ring(ctx, 65.6, '#e6c59d', .85)
  for (let i = 0; i < 16; i++) {
    ctx.save(); ctx.rotate(i * tau / 16)
    ctx.beginPath(); ctx.moveTo(0, -15); ctx.bezierCurveTo(-2, -26, -3, -55, 0, -64)
    ctx.strokeStyle = '#47352a55'; ctx.lineWidth = .65; ctx.stroke(); ctx.restore()
  }
  ring(ctx, 61.4, '#e8b98968', .65); ring(ctx, 57.4, '#e8b98930', .28)
  circle(ctx, 17.4, radial(ctx, 17.4, [[0,'#cfa770'], [.56,'#7a5946'], [.8,'#513d2e'], [.9,'#c09564'], [1,'#5c4835']]))
  for (let arm = 0; arm < 4; arm++) {
    ctx.save(); ctx.rotate(.6 + arm * tau / 4)
    ctx.beginPath(); ctx.moveTo(-2.1, -3); ctx.lineTo(-1.2, -24)
    ctx.quadraticCurveTo(-3.2, -26, 0, -28); ctx.quadraticCurveTo(3.2, -26, 1.2, -24)
    ctx.lineTo(2.1, -3); ctx.closePath(); ctx.fillStyle = metal(ctx, 18); ctx.fill()
    ctx.lineWidth = .45; ctx.strokeStyle = '#574638'; ctx.stroke(); ctx.restore()
  }
  circle(ctx, 7, metal(ctx, 7))
  circle(ctx, 4.4, radial(ctx, 4.4, [[0,'#fff3cc'], [.48,'#dbb987'], [1,'#8c694e']]))
}

export class Wheel {
  private readonly canvas = document.createElement('canvas')
  private readonly rotor = document.createElement('canvas')
  private readonly resize: ResizeObserver
  private readonly motion = matchMedia('(prefers-reduced-motion: reduce)')
  private pose: Pose = { rotor: 0, ball: -.48, radius: .918 }
  private animation?: { from: Pose; to: Pose; start: number; duration: number }
  private frame = 0
  private round?: bigint
  private onMotion = () => { if (this.motion.matches) this.finish() }

  constructor(host: HTMLElement, private pockets: Catalog['wheel']) {
    this.canvas.setAttribute('role', 'img')
    this.canvas.setAttribute('aria-label', 'European roulette wheel with 37 numbered pockets')
    host.append(this.canvas)
    this.resize = new ResizeObserver(() => this.rebuild())
    this.resize.observe(this.canvas)
    this.motion.addEventListener('change', this.onMotion)
    this.rebuild()
  }
  show(round: bigint, pocket: number, duration: number) {
    if (round === this.round) {
      if (duration <= 0) this.finish()
      return
    }
    this.finish()
    this.round = round
    const to = targetPose(this.pose, pocket, this.pockets.length)
    this.canvas.setAttribute('aria-label', 'European wheel, recorded result ' + this.pockets[pocket].number)
    if (duration <= 0 || this.motion.matches) {
      this.pose = to; this.draw(); return
    }
    this.animation = { from: this.pose, to, start: performance.now(), duration }
    this.frame = requestAnimationFrame(this.animate)
  }
  finish() {
    if (!this.animation) return
    cancelAnimationFrame(this.frame)
    this.pose = this.animation.to
    this.animation = undefined
    this.draw()
  }
  private animate = (now: number) => {
    const a = this.animation
    if (!a) return
    const p = (now - a.start) / a.duration
    if (p >= 1) { this.finish(); return }
    this.pose = interpolate(a.from, a.to, p)
    this.draw(); this.frame = requestAnimationFrame(this.animate)
  }
  private rebuild() {
    const width = this.canvas.getBoundingClientRect().width
    if (!width) return
    const d = Math.round(width * Math.min(3, window.devicePixelRatio || 1))
    this.canvas.width = this.canvas.height = d
    this.rotor.width = this.rotor.height = d
    const ctx = this.rotor.getContext('2d')
    if (!ctx) return
    ctx.setTransform(d / 206, 0, 0, d / 206, d / 2, d / 2)
    drawRotor(ctx, this.pockets); this.draw()
  }
  private draw() {
    const ctx = this.canvas.getContext('2d')
    if (!ctx || !this.rotor.width) return
    const d = this.canvas.width
    ctx.setTransform(d / 206, 0, 0, d / 206, d / 2, d / 2)
    ctx.clearRect(-103, -103, 206, 206)
    circle(ctx, 98.2, metal(ctx, 98)); circle(ctx, 96.8, radial(ctx, 97, [
      [0,'#352f29'], [.87,'#3c352e'], [.902,'#604d3b'], [.93,'#b08a61'], [.958,'#755b46'], [1,'#2c2925']]))
    ring(ctx, 95.9, '#d9b990', .45); ring(ctx, 85.1, '#ddbb90', .75)
    ctx.save(); ctx.rotate(this.pose.rotor)
    ctx.drawImage(this.rotor, -103, -103, 206, 206); ctx.restore()
    const x = Math.sin(this.pose.ball) * this.pose.radius * 100
    const y = -Math.cos(this.pose.ball) * this.pose.radius * 100
    ctx.save(); ctx.translate(x, y)
    ctx.shadowColor = '#081316b3'; ctx.shadowBlur = d / 135; ctx.shadowOffsetY = d / 250
    const pearl = ctx.createRadialGradient(-.7, -1, .2, 0, 0, 2.4)
    pearl.addColorStop(0, '#ffffff'); pearl.addColorStop(.5, '#fbf6e9'); pearl.addColorStop(1, '#9c9386')
    circle(ctx, 2.4, pearl); ctx.restore()
    ctx.beginPath(); ctx.moveTo(-2.8, -101); ctx.lineTo(2.8, -101); ctx.lineTo(0, -96.7)
    ctx.closePath(); ctx.fillStyle = '#f6e8ca'; ctx.fill()
  }
  destroy() {
    cancelAnimationFrame(this.frame); this.resize.disconnect()
    this.motion.removeEventListener('change', this.onMotion)
  }
}
