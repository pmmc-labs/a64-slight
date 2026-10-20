import net from 'node:net'
import { nexusFromNDSocket } from 'stupidrpc/src/ndsocket.js'
const socket = net.createConnection(Number(process.argv[2]))
await new Promise((res, rej) => { socket.on('connect', res); socket.on('error', rej) })
const debug = { log: (dir, args) => console.log(dir, JSON.stringify([...args])) }
const nexus = nexusFromNDSocket(socket, { prefix: 'js:', debug })
const it = nexus.iter('count', 1000)
await it.next(); await it.next()
nexus.sendMessage_('STOP', 'js:0001')
await new Promise(r => setTimeout(r, 400))
console.log('-- 400ms later')
socket.destroy()
