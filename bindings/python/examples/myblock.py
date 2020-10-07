import diy

w = diy.mpi.MPIComm()
m = diy.Master(w)

diy.add_my_block(m, 0)
diy.add_my_block(m, 5)

print(m)
