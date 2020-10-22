from ._diy import *

# Monkey-patch the constructor to accept a normal mpi4py communicator

init = Master.__init__
def convert_mpi_comm(self, comm, *args, **kwargs):
    if not isinstance(comm, mpi.MPIComm):
        from mpi4py import MPI
        a = MPI._addressof(comm)
    else:
        a = comm.comm()
    init(self, a, *args, **kwargs)
Master.__init__ = convert_mpi_comm

class InitFinalize:
    def __init__(self):
        mpi.init()

    def __del__(self):
        mpi.finalize()

init_finalize = InitFinalize()
