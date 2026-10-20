import { Nexus } from '/home/user/stupidrpc/src/nexus.js'
const tick = () => new Promise(r => setTimeout(r, 20))
for (const [pa, pb] of [['a:', 'b:'], [undefined, undefined]]) {
  let a, b
  const log = []
  a = new Nexus({ prefix: pa, sendMessage: (...m) => { log.push('A> ' + JSON.stringify(m)); queueMicrotask(() => b.receiveMessage(...m)) } })
  b = new Nexus({ prefix: pb, sendMessage: (...m) => { log.push('B> ' + JSON.stringify(m)); queueMicrotask(() => a.receiveMessage(...m)) } })
  let produced = 0
  b.callHandlers.count = async function* () { for (let i = 0; i < 5; i++) { produced++; yield i; await tick() } return 'end' }
  // a also has an outgoing call in flight, so a STOP mis-routed to inflightMine can hit it
  b.callHandlers.slow = () => new Promise(r => setTimeout(() => r('slow'), 400))
  a.callHandlers.slow = () => new Promise(r => setTimeout(() => r('slow'), 400))
  const it = a.iter('count')
  await it.next()
  const id = log[0].match(/"CALL","([^"]+)"/)[1]
  b.call('slow')  // b's own outgoing call; with no prefixes its id is also "0001"
  await tick()
  log.push(`-- injecting STOP ${id} into b`)
  try { b.receiveMessage('STOP', id) } catch (e) { log.push('b.receiveMessage threw: ' + e) }
  for (let i = 0; i < 8; i++) await tick()
  console.log(`== prefixes ${pa}/${pb}: callee produced ${produced} of 5`)
  console.log(log.join('\n'))
}
