import { Nexus } from '/home/user/stupidrpc/src/nexus.js'
const wire = (prefixA, prefixB) => {
  let a, b
  const log = []
  a = new Nexus({ prefix: prefixA, sendMessage: (...m) => { log.push(['A>', ...m]); queueMicrotask(() => b.receiveMessage(...m)) } })
  b = new Nexus({ prefix: prefixB, sendMessage: (...m) => { log.push(['B>', ...m]); queueMicrotask(() => a.receiveMessage(...m)) } })
  return { a, b, log }
}
const tick = () => new Promise(r => setTimeout(r, 20))
for (const [pa, pb] of [['a:', 'b:'], [undefined, undefined]]) {
  const { a, b, log } = wire(pa, pb)
  let produced = 0
  b.callHandlers.count = async function* () { for (let i = 0; i < 5; i++) { produced++; yield i; await tick() } return 'end' }
  const it = a.iter('count')
  try {
    for await (const v of it) { if (v === 1) break }
    await tick(); await tick(); await tick(); await tick(); await tick(); await tick()
  } catch (e) { console.log('caught', e) }
  console.log(`prefixes ${pa}/${pb}: callee produced ${produced} of 5; b.inflightTheirs.size=${b.inflightTheirs.size}`)
  console.log(log.map(m => JSON.stringify(m)).join('\n'))
}
process.on('uncaughtException', e => console.log('uncaught:', e))
