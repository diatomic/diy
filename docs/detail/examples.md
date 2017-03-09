\defgroup Examples

The best way to learn DIY is by example, and the examples directory contains
numerous complete programs that demonstrate most of the concepts in DIY.
Example names preceded by the heading (*commented*) have ample comments
embedded in the source code, meaning they are good starting points. The
remaining, uncommented examples are easier to understand once the commented
ones are clear.

- [Simple](https://github.com/diatomic/diy/tree/master/examples/simple):
  includes common operations such as initializing DIY, adding links to form block
  neighborhoods, creating callback functions for each block, communicating
  between block neighbors, performing collectives over all blocks, and writing
  and reading blocks to and from a DIY file. These examples also show how to
  create, destroy, and serialize blocks in (*commented*) [block.h](\ref simple/block.h).

  - (*commented*) [simple.cpp](\ref simple/simple.cpp): exercises
    simple neighbor communication by creating a linear chain of blocks, each
    connected to two neighbors (predecessor and successor), except for the
    first and the last blocks, which have only one or the other. Each block
    computes an average of its values and those of its neighbors. The average is
    stored in the block, and the blocks are written to a file in storage.

  - [until-done.cpp](\ref simple/until-done.cpp): shows how to use
    collectives in the DIY model, i.e., block-based collectives that can run
    in-/out-of-core and single/multithreaded. The same result can be
    accomplished using the reduction patterns in
    [examples/reduce](https://github.com/diatomic/diy/tree/master/examples/reduce),
    but using DIY collectives can be easier for simple "one-line" reductions
    such as global sums or boolean ands. This example shows how to do an [all_reduce()](@ref diy::Master::Proxy::all_reduce)
    in order to determine whether all blocks have finished processing.

  - [read-blocks.cpp](\ref simple/read-blocks.cpp): shows how to read
    a DIY file back into memory and create a new DIY master and assignment of
    blocks to processes that may be different than when the blocks were written.
    The blocks from [simple.cpp](\ref simple/simple.cpp) are read back into
    memory from the file, and their values are printed.

- [Decomposition](https://github.com/diatomic/diy/tree/master/examples/decomposition):
  demonstrates how to decompose a regular grid into blocks and create links between them.

  - (*commented*) [regular-decomposer-long.cpp](\ref decomposition/regular-decomposer-long.cpp):
    blocks are defined via a lambda function provided to the decomposer. This
    example shows how to set shared faces, ghost regions, and periodic boundaries
    in the decomposition. It also shows both how to create a `RegularDecomposer`
    and call its `decompose` member function, as well as how to combine those two
    steps using one helper function.

  - (*commented*) [regular-decomposer-short.cpp](\ref decomposition/regular-decomposer-short.cpp):
    domain can be decomposed just by providing a `Master` object to the
    decomposer.

- [Serialization](https://github.com/diatomic/diy/tree/master/examples/serialization):
  blocks that are loaded and saved in and out of core are serialized by DIY. This
  example shows how to write the `load` and `save` functions for two data
  structures. Both require just a one-line definition because DIY can serialize
  such structures automatically.

  - [test-serialization.cpp](\ref serialization/test-serialization.cpp):
    a 3-d `Point` and an n-d `PointVec` are serialized automatically, and the
    main program tests loading and saving both structures.

- [MPI](https://github.com/diatomic/diy/tree/master/examples/mpi):
  shows DIY's [convenience wrapper for MPI](\ref MPI). It exists only to make the
  code simpler; the user is free to use it or the original MPI routines
  interchangeably.

  - [test-mpi.cpp](\ref mpi/test-mpi.cpp): this example exercises `send`,
    `receive`, `iprobe`, `broadcast`, `reduce`, `scan`, and `all_gather`.

- [I/O](https://github.com/diatomic/diy/tree/master/examples/io): illustrates
  BOV (brick of values) and NumPy I/O, both built on top of MPI-IO and MPI
  sub-array types.

  - [test-io.cpp](\ref io/test-io.cpp): tests the readers and writers for
    BOV and NumPy. The reader would typically be called from the `create`
    callback passed to diy::decompose and tell the reader to read a specific
    block of data that the decomposer chose. Blocks can read data collectively if
    the number of blocks on each processor is the same. Same goes for the
    writers. The writer would typically be called from the `foreach` callback for
    each block.

- [Reduction](https://github.com/diatomic/diy/tree/master/examples/reduce):
  DIY supports general reductions to implement more complex global operations
  than the collective one-liners. These reductions are general-purpose (any
  global communication pattern can be implemented); they operate over blocks,
  which cycle in and out of core as necessary; the
  operations are (multi-)threaded automatically using the same mechanism as
  [foreach()](@ref diy::Master::foreach). Although any global communication can
  be expressed using the reduction mechanism, all the reductions included in DIY
  operate in rounds over a k-ary reduction tree. The value of k used in each round
  can vary, but if it's fixed, the number of rounds is log_k(nblocks).

  - (*commented*) [merge-reduce.cpp](\ref reduce/merge-reduce.cpp):
    merges blocks together, computing a sum of their values. At each
    round, one block of a group of k blocks is the root of the group. The other
    blocks send their data to the root, which computes the sum, and the
    root block (only) proceeds to the next round. After `log_k(numblocks)` rounds,
    one block contains the global sum of the values. Calling merge-reduce
    is done by creating `diy::RegularMergePartners` and then calling
    `diy::reduce`. For regular grids of blocks, groups can be formed by either
    "distance-doubling" or "distance-halving" depending on the value of the
    `contiguous` parameter in `diy::RegularMergePartners`.

  - (*commented*) [swap-reduce.cpp](\ref reduce/swap-reduce.cpp): unlike
    merge-reduce, the swap-reduction does not idle blocks from one round to the
    next and does not aggregate all the results to a single block. Rather, block
    data are split into k pieces that are swapped between the k members of
    a group. This particular example begins with an unsorted set of points that
    do not lie in the bounds of the blocks, and the swap reduction is used to
    sort the points in the correct blocks with respect to the block bounds.

  - [all-to-all.cpp](\ref reduce/all-to-all.cpp): this example solves
    the same problem as [swap-reduce.cpp](\ref reduce/swap-reduce.cpp): sorts
    points in blocks. The difference is that
    [swap-reduce.cpp](\ref reduce/swap-reduce.cpp) does everything manually;
    whereas in [all-to-all.cpp](\ref reduce/all-to-all.cpp), the user only
    specifies how to enqueue data (from each block to each block) at the start
    and how to dequeue data at the end. `diy::all_to_all()` takes care of all the
    intermediate rounds, routing the data appropriately.

  - (*commented*) [all-done.cpp](\ref reduce/all-done.cpp): this is another example of using
    `diy::all_to_all()` for a very common case: to determine globally whether any blocks have
    any local work left to do.

  - [sort.cpp](\ref reduce/sort.cpp): shows how to use reduction to sort
    a 1-d vector of integers. The algorithm is a histogram-based sort that
    combines both merge and swap reductions. It merges histograms of local data
    distributions, computes quantiles of the histograms, and then swaps data
    values among blocks based on the quantiles.

- [Algorithms](https://github.com/diatomic/diy/tree/master/examples/reduce):
  Examples of the following algorithms are included.

  - [sample-sort.cpp](\ref reduce/sample-sort.cpp): This example calls the parallel
    sample sort algorithm of [Blelloch 1998] that's included in DIY.
    The built-in  `diy::sort` used in this example is easier to use than
    the manual sort implemented by the example above, and we are in the process of comparing the
    performance of the two versions. Note that
    [sort.cpp](\ref reduce/sort.cpp) can only sort arithmetic types (to be able
    to compute the histograms), whereas `diy::sort` supports any type, which has
    a user-supplied comparison operation.

  - [kd-tree.cpp](\ref reduce/kd-tree.cpp): Like the swap-reduce example above, this
    example begins with an unsorted set of points that do not lie in the bounds
    of any blocks, but the points are sorted into kd-tree of blocks with
    approximately equal numbers of points in each block.

Various other open-source projects have been DIY'ed, and these are also good, albeit more involved, places to learn DIY. Here are a few suggestions:

- [cian2](https://github.com/tpeterka/cian2) is a suite of benchmarks that test
  various HPC tasks. The [communication](https://github.com/tpeterka/cian2/communication) part of cian
  tests common communication patterns including most of the above reductions and
  neighbor communication.
- [tess2](https://github.com/diatomic/tess2) is a parallel Voronoi and Delaunay tessellation library that is parallelized using DIY.
    - The [src](https://github.com/diatomic/tess2/src) directory is the library code that uses DIY to compute the tessellation in parallel and write it to disk in the DIY block format.
    - The [examples](https://github.com/diatomic/tess2/examples) directory contains numerous DIY programs that use the tess library.
    - The [tools](https://github.com/diatomic/tess2/tools) directory contains a serial rendering program `draw.cpp` that reads the DIY block format from disk and uses it to draw the tessellation.


\example simple/simple.cpp
\example simple/block.h
\example simple/read-blocks.cpp
\example simple/until-done.cpp
\example decomposition/test-decomposition.cpp
\example decomposition/regular-decomposer-short.cpp
\example decomposition/regular-decomposer-long.cpp
\example serialization/test-serialization.cpp
\example io/test-io.cpp
\example mpi/test-mpi.cpp
\example reduce/merge-reduce.cpp
\example reduce/swap-reduce.cpp
\example reduce/all-to-all.cpp
\example reduce/all-done.cpp
\example reduce/kd-tree.cpp
\example reduce/sort.cpp
\example reduce/sample-sort.cpp
