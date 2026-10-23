# bank in SimPy (D241), its bank example as SimPy's documentation has it:
# a process for each customer, and the bank a Resource of 3 tellers. The
# times come from Park and Miller's generator, as in bank.slight. Needs
# SimPy (pip install simpy); bench/run.py leaves it out if it can't be
# imported.
import sys

import simpy


def customer(env, bank, took, stats):
    arrived = env.now
    with bank.request() as req:
        yield req
        waited = env.now - arrived
        stats[0] += waited
        stats[1] += waited > 0
        yield env.timeout(took)


def source(env, bank, n, stats):
    x = 1
    for _ in range(n):
        y = 48271 * x % 2147483647
        x = 48271 * y % 2147483647
        env.process(customer(env, bank, 1 + y % 5399, stats))
        yield env.timeout(1 + x % 1999)


n     = int(sys.argv[1])
env   = simpy.Environment()
bank  = simpy.Resource(env, capacity=3)
stats = [0, 0]
env.process(source(env, bank, n, stats))
env.run()
print(n)
print(stats[0])
print(stats[1])
