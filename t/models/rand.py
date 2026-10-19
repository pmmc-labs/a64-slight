# Java's SplittableRandom, as slight's random numbers are (D222), for the
# models of tests that draw: SplitMix64, a state stepped by an odd gamma
# and mixed. A fork's child is split() from its parent.
M = (1 << 64) - 1
GOLDEN_GAMMA = 0x9e3779b97f4a7c15


def mix64(z):
    z = ((z ^ (z >> 30)) * 0xbf58476d1ce4e5b9) & M
    z = ((z ^ (z >> 27)) * 0x94d049bb133111eb) & M
    return z ^ (z >> 31)


def mix_gamma(z):
    z = ((z ^ (z >> 33)) * 0xff51afd7ed558ccd) & M
    z = ((z ^ (z >> 33)) * 0xc4ceb9fe1a85ec53) & M
    z = (z ^ (z >> 33)) | 1
    return z ^ 0xaaaaaaaaaaaaaaaa if bin(z ^ (z >> 1)).count('1') < 24 else z


class Rand:
    def __init__(self, seed, gamma=GOLDEN_GAMMA):
        self.seed, self.gamma = seed & M, gamma

    def next_seed(self):
        self.seed = (self.seed + self.gamma) & M
        return self.seed

    def next64(self):
        return mix64(self.next_seed())

    def split(self):
        return Rand(self.next64(), mix_gamma(self.next_seed()))

    def int(self, bound):
        """0 to bound - 1: Java's nextLong(bound), which draws again where a
        draw would favor some."""
        signed = lambda x: x - (1 << 64) if x >= 1 << 63 else x
        m, r = bound - 1, self.next64()
        if bound & m == 0:
            return r & m
        u = r >> 1
        while True:
            r = u % bound
            if signed((u + m - r) & M) >= 0:
                return r
            u = self.next64() >> 1

    def float(self):
        return (self.next64() >> 11) * 2.0 ** -53
