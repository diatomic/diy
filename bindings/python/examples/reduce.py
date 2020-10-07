# example illustrating communication with reductions, e.g., merge-reduce

import diy
import numpy as np

w = diy.mpi.MPIComm()           # world
m = diy.Master(w)               # master

class Block:

    def __init__(self, core):
        n = 10
        self.core = core
        self.data = np.empty(n)

    def show(self, cp):
        print(w.rank, cp.gid(), self.core, self.data)

    def reduce(self, cp, p):
        # receive and accumulate
        link = cp.in_link()
        for i in range(len(link)):
            nbr_gid = link.target(i).gid
            if cp.gid() != nbr_gid:         # don't receive from self
                in_data = cp.dequeue(nbr_gid)
#                 print("round %d gid %d received from gid %d: %s" % (cp.round(), cp.gid(), nbr_gid, in_data))
                self.data += in_data

        # send
        link = cp.out_link()
        for i in range(len(link)):
            target = link.target(i)
            if cp.gid() != target.gid:      # don't send to self
#                 print("round %d gid %d sending to gid %d: %s" % (cp.round(), cp.gid(), target.gid, self.data))
                cp.enqueue(target, self.data)

def add_block(gid, core, bounds, domain, link):
    b = Block(core)
    for i in range(b.data.shape[0]):
        b.data[i] = i
    m.add(gid, b, link)

nblocks = 8
domain = diy.DiscreteBounds([0,0,0], [100, 100, 100])
d = diy.DiscreteDecomposer(3, domain, nblocks)
a = diy.ContiguousAssigner(w.size, nblocks)
p = diy.RegularMergePartners(d, 2, True)
d.decompose(w.rank, a, add_block)

print(m)

# m.foreach(Block.show)
diy.reduce(m, a, p, Block.reduce)
m.foreach(Block.show)

# Pure Python partners

class AllToAllPartners:
    def __init__(self, n):
        self.n = n

    def rounds(self):
        return 1

    def active(self, r, gid, m):
        return True

    def incoming(self, r, gid, m):
        return list(range(self.n))

    def outgoing(self, r, gid, m):
        return list(range(self.n))

def a2a_reduce(block, cp, p):
    print(cp.round(), cp.gid())
    print("  Incoming:", [cp.in_link().target(i) for i in range(len(cp.in_link()))])
    print("  Outgoing:", [cp.out_link().target(i) for i in range(len(cp.out_link()))])

a2a_partners = AllToAllPartners(nblocks)
diy.reduce(m, a, a2a_partners, a2a_reduce)
