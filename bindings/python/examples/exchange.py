# example illustrating communication using exchange

import diy

w = diy.mpi.MPIComm()           # world

m = diy.Master(w)               # master

class Block:
    def __init__(self, core):
        self.core = core

    def show(self, cp):
        print(w.rank, cp.gid(), self.core)
        #cp.enqueue(diy.BlockID(1, 0), "abc")

    def send(self, cp):
        link = cp.link()
        for i in range(len(link)):
            target = link.target(i)
            o = [cp.gid(), target.gid]
            dir = link.direction(i)
            print("%d sending to %d: %s to direction %s" % (cp.gid(), target.gid, o, dir))
            cp.enqueue(target, o)

    def recv(self, cp):
        link = cp.link()
        for i in range(len(link)):
            gid = link.target(i).gid
            o = cp.dequeue(gid)
            dir = link.direction(i)
            print("%d received from %d: %s from direction %s" % (cp.gid(), gid, o, dir))

def add_block(gid, core, bounds, domain, link):
    #print(gid, core, bounds, domain)
    m.add(gid, Block(core), link)

nblocks = 16
domain = diy.DiscreteBounds([0,0,0], [100, 100, 100])
d = diy.DiscreteDecomposer(3, domain, nblocks)
a = diy.ContiguousAssigner(w.size, nblocks)
d.decompose(w.rank, a, add_block)

print(m)

m.foreach(Block.show)
m.foreach(Block.send)
m.exchange()

m.foreach(Block.recv)
