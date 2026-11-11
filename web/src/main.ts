import './global.css'
import { Player } from './player'
import { Operator } from './operator'
import { parseView, type BrowserSession, type Catalog, type SessionView } from './bridge-types'

const screen = document.querySelector<HTMLElement>('#screen')!
const status = document.querySelector<HTMLElement>('.connection')!
const notice = document.querySelector<HTMLElement>('#notice')!
const now = () => Math.floor(performance.now()) >>> 0
const channel = typeof BroadcastChannel === 'function' ? new BroadcastChannel('mini-etg-roulette.views.v1') : undefined
let session: BrowserSession | undefined
let catalog: Catalog | undefined
let view: SessionView | undefined
let player: Player | undefined
let operator: Operator | undefined
let route = ''
let owner = false
let ready = false
let stopped = false
let remaining = 0
let sampled = 0
let recoveryShown = ''
let halted = ''
let unlock: (()=>void) | undefined

function message(value:string) {
  notice.textContent = value
  notice.hidden = !value
}
function connection(label:string, live = false) {
  status.textContent = label
  status.classList.toggle('live',live)
}
function run(action:(s:BrowserSession)=>string) {
  if (!ready || !session || !owner || halted) return 'This table is read-only.'
  const result = action(session)
  refresh(true)
  return result
}
function draw() {
  const next = location.hash === '#/operator' ? 'operator' : 'play'
  document.querySelectorAll<HTMLAnchorElement>('.mast nav a').forEach(a => {
    if (a.hash === '#/'+next) a.setAttribute('aria-current','page')
    else a.removeAttribute('aria-current')
  })
  document.title = (next === 'play' ? 'Player terminal' : 'Operator dashboard') + ' · Orbit Table'
  if (!catalog || !view) return
  if (route !== next) {
    player?.destroy()
    player = undefined
    operator = undefined
    screen.replaceChildren()
    if (next === 'play') player = new Player(screen,catalog,{
      place:(id,stake)=>run(s=>s.place(id,stake,now())), undo:()=>run(s=>s.undo(now())),
      clear:()=>run(s=>s.clear(now())), repeat:()=>run(s=>s.repeat(now()))
    })
    else operator = new Operator(screen,type=>run(s=>s.armFault(type,now())))
    route = next
  }
  const writable = owner && ready && !halted
  const ms = Math.max(0,remaining - (performance.now()-sampled))
  player?.update(view,ms,writable)
  operator?.update(view,writable)
  if (view.recovery && view.recovery !== recoveryShown && !halted) {
    recoveryShown = view.recovery
    message(view.recovery)
  }
}
function sendView(includeCatalog = false) {
  if (!owner) return
  if (!ready && halted) {
    channel?.postMessage({type:'paused', reason:halted})
    return
  }
  if (ready && view) channel?.postMessage({
    type:'view', view, catalog:includeCatalog ? catalog : undefined,
    remaining, time:Date.now(), halted
  })
}
function refresh(force = false) {
  if (!session || !ready) return
  const changed = force || !view || session.revision() !== view.revision
  if (changed) view = parseView(session.view())
  remaining = session.remainingMs(now())
  sampled = performance.now()
  draw()
  if (changed) sendView()
}
function pause(reason:string) {
  halted = reason
  connection('Table paused')
  message(reason)
  if (!view) screen.textContent = 'Table unavailable.'
  draw()
  sendView()
}
async function lead() {
  owner = true
  connection('Starting engine')
  try {
    const moduleUrl = new URL(import.meta.env.BASE_URL + 'engine/roulette.mjs',document.baseURI).href
    const { default: createEngine } = await import(/* @vite-ignore */ moduleUrl)
    if (stopped) return
    const module = await createEngine()
    if (stopped) return
    const seed = crypto.getRandomValues(new Uint32Array(1))[0]
    session = new module.BrowserSession(seed) as BrowserSession
    const error = session.start(now())
    if (error) { pause(error); return }
    catalog = JSON.parse(session.catalog()) as Catalog
    ready = true
    halted = ''
    connection('Table live',true)
    message('')
    refresh(true)
    sendView(true)
  } catch (e) { pause(e instanceof Error ? e.message : 'The browser engine could not start.') }
}

channel?.addEventListener('message',({data})=>{
  if (stopped || !data) return
  if (data.type === 'subscribe' && owner) { sendView(true); return }
  if (owner) return
  if (data.type === 'paused') { pause(String(data.reason)); return }
  if (data.type !== 'view') return
  if (data.catalog) catalog = data.catalog
  if (!catalog) { channel.postMessage({type:'subscribe'}); return }
  view = data.view
  remaining = Math.max(0,data.remaining - Math.max(0,Date.now()-data.time))
  sampled = performance.now()
  halted = data.halted
  connection('Read-only mirror')
  if (halted) message(halted)
  draw()
})
async function connect() {
  if (!navigator.locks) {
    pause('Web Locks are unavailable. Open this table over HTTPS or localhost in a browser with Web Locks.')
    return
  }
  connection('Waiting for table')
  channel?.postMessage({type:'subscribe'})
  // The queued lock transfers ownership after the previous tab releases it.
  await navigator.locks.request('mini-etg-roulette.writer.v1',async()=>{
    if (stopped) return
    const held = new Promise<void>(resolve=>{unlock = resolve})
    await lead()
    await held
  })
}

const timer = window.setInterval(()=>{
  if (owner && ready && session && !halted) {
    try {
      const error = session.tick(now())
      if (error) pause(error)
      else refresh()
    } catch (e) { pause(e instanceof Error ? e.message : 'The table stopped unexpectedly.') }
  } else if (!owner && view) draw()
},150)
window.addEventListener('hashchange',draw)
window.addEventListener('pagehide',()=>{
  stopped = true
  clearInterval(timer)
  player?.destroy()
  channel?.close()
  session?.delete()
  unlock?.()
})
window.addEventListener('pageshow',event=>{ if (event.persisted) location.reload() })
draw()
void connect().catch(e=>pause(String(e)))
