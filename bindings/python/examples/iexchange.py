# example illustrating communication using iexchange

import diy

w = diy.mpi.MPIComm()           # world

m = diy.Master(w)               # master

class Block:

    def __init__(self, core):
        self.core = core
        self.init = True

    def show(self, cp):
        print(w.rank, cp.gid(), self.core)

    def bounce(self, cp):
        link = cp.link()

        # initial send
        if self.init:
            hops = 3                        # number of times to send
            for i in range(len(link)):
                target = link.target(i)
                o = [cp.gid(), hops]
                print("%d initial sending %s to %d" % (cp.gid(), o, target.gid))
                cp.enqueue(target, o)
            self.init = False

        # while something incoming, receive it and send it, decrementing its hop count down to 0
        while True:

            for i in range(len(link)):
                nbr_gid = link.target(i).gid

                # receive
                while cp.incoming(nbr_gid):     # NB, check for incoming before dequeueing, otherwise segfaults
                    o = cp.dequeue(nbr_gid)
                    print("%d received %s from %d" % (cp.gid(), o, nbr_gid))
                    hops = o[1] - 1

                    # send
                    if hops > 0:
                        o = [cp.gid(), hops]
                        print("%d sending %s to %d" % (cp.gid(), o, nbr_gid))
                        cp.enqueue(link.target(i), o)

            if (not cp.fill_incoming()):
                break

        return True

def add_block(gid, core, bounds, domain, link):
    m.add(gid, Block(core), link)

nblocks = 8
domain = diy.DiscreteBounds([0,0,0], [100, 100, 100])
d = diy.DiscreteDecomposer(3, domain, nblocks)
a = diy.ContiguousAssigner(w.size, nblocks)
d.decompose(w.rank, a, add_block)

print(m)

m.iexchange(Block.bounce)
