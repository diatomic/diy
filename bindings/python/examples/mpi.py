# example illustrating mixing pyDIY with mpi4py

# pyDIY doesn't provide wrappers around low-level MPI commands; use them from mpi4py instead
# This is the same example as exchange.py, with a few mpi4py calls sprinkled in

# the order of importing diy and mpi4py doesn't matter; either order works
from mpi4py import MPI
import diy

# can get comm either from diy or mpi4py, in this case exercising mpi4py
comm = MPI.COMM_WORLD

c = diy.mpi.MPIComm(comm)
m = diy.Master(c)               # master

# try some more mpi4py to get my rank and size of comm
rank = comm.Get_rank()
size = comm.Get_size()
print("Rank", rank, "out of a communicator of size", size)

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
#             print("%d sending to %d: %s to direction %s" % (cp.gid(), target.gid, o, dir))
            cp.enqueue(target, o)

    def recv(self, cp):
        link = cp.link()
        for i in range(len(link)):
            gid = link.target(i).gid
            o = cp.dequeue(gid)
            dir = link.direction(i)
#             print("%d received from %d: %s from direction %s" % (cp.gid(), gid, o, dir))

def add_block(gid, core, bounds, domain, link):
    #print(gid, core, bounds, domain)
    m.add(gid, Block(core), link)

nblocks = 16
domain = diy.DiscreteBounds([0,0,0], [100, 100, 100])
d = diy.DiscreteDecomposer(3, domain, nblocks)
a = diy.ContiguousAssigner(size, nblocks)
d.decompose(rank, a, add_block)

print(m)

# m.foreach(Block.show)
m.foreach(Block.send)
m.exchange()

# a little more mpi4py: pretend we want a barrier here
comm.Barrier()

m.foreach(Block.recv)

print("Success")
