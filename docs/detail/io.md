\defgroup IO

Block I/O: DIY writes and reads blocks to and from storage in either collective
or independent mode. The collective mode, which is the default, creates one
file. Independent mode creates one file per process. These features should not be
confused with the temporary block I/O that DIY does in the course of shuffling
blocks in and out of core while executing the block `foreach` functions. This
happens behind the scenes and is not the subject of the I/O module.

Collective I/O
--------------

- `diy::io::write_blocks()`
- `diy::io::read_blocks()`

Independent I/O
--------------

- `diy::io::split::write_blocks()`
- `diy::io::split::read_blocks()`

An example (of collective I/O) is below:
~~~~{.cpp}
int mem_blocks = -1;
int num_threads = 1;
diy::mpi::communicator    world(comm);
diy::FileStorage          storage("./DIY.XXXXXX");
diy::Master               master(world,
                                 num_threads,
                                 mem_blocks);


// --- write ---

// the total number of blocks, tot_blocks, is set by user
diy::ContiguousAssigner assigner(world.size(), tot_blocks);
// the block_save function is unnecessary if it was defined as part of master
diy::io::write_blocks(filename, world, master, &block_save);


// --- read ---

// the number of blocks is -1 because it is determined by read_blocks()
diy::ContiguousAssigner assigner(world.size(), -1);
// the block_load function is unnecessary if it was defined as part of master
diy::io::read_blocks(filename, world, *assigner, master, &block_load);
~~~~

In addition, the [examples/io directory](https://github.com/diatomic/diy/tree/master/examples/io) illustrates BOV (brick of values) and NumPy I/O, which is built on top of MPI-IO and MPI sub-array types. The [test-io.cpp example](\ref io/test-io.cpp) tests the readers and writers for BOV and NumPy.

