# example illustrating writing diy blocks and reading them back in

import diy
import numpy as np
import sys

w = diy.mpi.MPIComm()           # world

m = diy.Master(w)               # master

class Block:

    def __init__(self, core):
        n = 10
        self.data = np.empty(n)
        self.core = core

    def show(self, cp):
        print(w.rank, cp.gid(), self.core)

def add_block(gid, core, bounds, domain, link):
    b = Block(core)
    for i in range(b.data.shape[0]):
        b.data[i] = i
    m.add(gid, b, link)

nblocks = 16
domain = diy.DiscreteBounds([0,0,0], [100, 100, 100])
d = diy.DiscreteDecomposer(3, domain, nblocks)
a = diy.ContiguousAssigner(w.size, nblocks)
d.decompose(w.rank, a, add_block)

print(m)

diy.write_blocks("outfile", m)

m2 = diy.Master(w)
domain2 = diy.read_blocks("outfile", a, m2)
print(domain2)

# check that the data values match
for i in range(m.size()):
    if np.any(m.block(i).data != m2.block(i).data):
        print("Error: data values do not match. Aborting")
        print(m.block(i).data)
        print(m2.block(i).data)
        sys.exit()

diy.write_blocks("outfile-withdomain", m, domain)

m3 = diy.Master(w)
domain3 = diy.read_blocks("outfile-withdomain", a, m3)
print(domain3)

# check that the data values match
for i in range(m.size()):
    if np.any(m.block(i).data != m3.block(i).data):
        print("Error: data values do not match. Aborting")
        print(m.block(i).data)
        print(m3.block(i).data)
        sys.exit()


a2 = diy.ContiguousAssigner(1,2)
m4 = diy.Master(w)
b1 = diy.MyBlock()
b1.x = 42
m4.add(0, b1, diy.RegularGridLink())     # using RegularGridLink because saving of Link is broken in DIY (FIXME)
b2 = diy.MyBlock()
b2.x = 43
m4.add(1, b2, diy.RegularGridLink())     # using RegularGridLink because saving of Link is broken in DIY (FIXME)
m4.foreach(lambda b,cp: print(cp.gid(), b.x))

diy.write_blocks("myblocks", m4, save = diy.my_save_block)

m5 = diy.Master(w)
diy.read_blocks("myblocks", a2, m5, load = diy.my_load_block)
m5.foreach(lambda b,cp: print(cp.gid(), b.x))
