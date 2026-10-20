import net from 'node:net'
import { nexusFromNDSocket } from 'stupidrpc/src/ndsocket.js'
const callHandlers = {
  async echo (...args) { return args },
  async *generate (...args) { for (const a of args) yield a; return args.at(-1) },
  async throwError () { throw new Error('boom') },
  async rejectNothing () { return Promise.reject() },
  async big () { return [2 ** 53 + 1, 4611686018427387903] },
}
const server = net.createServer(s => nexusFromNDSocket(s, { prefix: 'jsd:', callHandlers }))
server.listen(Number(process.argv[2]), () => console.log('listening'))
setTimeout(() => process.exit(0), 8000)
