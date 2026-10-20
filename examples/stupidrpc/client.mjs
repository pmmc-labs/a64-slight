import net from 'node:net'
import { nexusFromNDSocket } from 'stupidrpc/src/ndsocket.js'
const socket = net.createConnection(Number(process.argv[2]))
await new Promise((res, rej) => { socket.on('connect', res); socket.on('error', rej) })
const debug = { log: (dir, args) => console.log(dir, JSON.stringify([...args])) }
const nexus = nexusFromNDSocket(socket, {
  prefix: 'js:', debug,
  callHandlers: { mul: async (a, b) => a * b },
})
const show = async (label, p) => { try { console.log('=>', label, JSON.stringify(await p)) } catch (e) { console.log('=> FAIL', label, JSON.stringify(e)) } }
await show('basic', nexus.call('basic', 'foo', 1, 2.5, true, null, [1, 2], { a: 1 }))
await show('square-via-you', nexus.call('square-via-you', 12))
const it = nexus.iter('generate', 'x', 'y')
for await (const v of it) console.log('=> next', JSON.stringify(v))
await show('generate result', it.result)
await show('fail', nexus.call('fail'))
// STOP: the JS caller can't send it (onStop's `this` bug), so send it by hand
const it2 = nexus.iter('count', 1000)
console.log('=> next', JSON.stringify((await it2.next()).value))
console.log('=> next', JSON.stringify((await it2.next()).value))
nexus.sendMessage('STOP', 'js:0006')
await new Promise(r => setTimeout(r, 400))
console.log('-- 400ms after STOP; any NEXT for js:0006 after the STOP line above means it was not stopped')
socket.destroy()
