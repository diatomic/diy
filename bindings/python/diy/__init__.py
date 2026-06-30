from ._diy import *
import atexit

# Monkey-patch the constructor to accept a normal mpi4py communicator

init = mpi.MPIComm.__init__
def convert_mpi_comm(self, *args, **kwargs):
    if len(args) == 0:
        init(self, *args, **kwargs)
    else:
        comm = args[0]

        if not isinstance(comm, mpi.MPIComm):
            from mpi4py import MPI
            comm = MPI._addressof(comm)

        init(self, comm, *args[1:], **kwargs)
mpi.MPIComm.__init__ = convert_mpi_comm

class InitFinalize:
    def __init__(self):
        self._owns_mpi = mpi.init()

    def finalize(self):
        if self._owns_mpi:
            mpi.finalize()
            self._owns_mpi = False

init_finalize = InitFinalize()
atexit.register(init_finalize.finalize)
