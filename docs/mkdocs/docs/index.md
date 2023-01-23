# DIY

DIY is a block-parallel library for writing scalable distributed- and shared-memory parallel
algorithms that can run both in- and out-of-core. The same program can be executed with one
or more threads per MPI process and with one or more data blocks resident in main memory.  The
abstraction enabling these capabilities is block-parallelism; blocks and their message
queues are mapped onto processing elements (MPI processes or threads) and are migrated between
memory and storage by the DIY runtime. Complex communication patterns, including neighbor
exchange, merge reduction, swap reduction, and all-to-all exchange, are implemented in DIY.


DIY follows a bulk-synchronous processing (BSP) programming model.

## Example

A simple program snippet, shown below, consists of the following components:

- a `struct` called `Block`,
    - contains all user-defined state related to a block
- the `diy::Master` object called `master`,
    - `master` owns and manages the blocks assigned to the current MPI process.
- user-defined callback functions performed on each block by `master.foreach()`
    - in this example `enqueue_local()` and `average()`
- message exchanges between the blocks by `master.exchange()`
    - data are enqueued and dequeued to/from message queues during `master.foreach()` callback functions.

~~~~{.cpp}
    // --- main program --- //

    struct Block { float local, average; };             // define your block structure

    Master master(world);                               // world = MPI_Comm
    ...                                                 // populate master with blocks
    master.foreach(&enqueue_local);                     // call enqueue_local() for each block
    master.exchange();                                  // exchange enqueued data between blocks
    master.foreach(&average);                           // call average() for each block

    // --- callback functions --- //

    // enqueue block data prior to exchanging it
    void enqueue_local(Block* b,                        // current block
                       const Proxy& cp)                 // communication proxy provides access to the neighbor blocks
    {
        for (size_t i = 0; i < cp.link()->size(); i++)  // for all neighbor blocks
            cp.enqueue(cp.link()->target(i), b->local); // enqueue the data to be sent to this neighbor
                                                        // block in the next exchange
    }

    // use the received data after exchanging it, in this case compute its average
    void average(Block* b,                              // current block
                 const Proxy& cp)                       // communication proxy provides access to the neighbor blocks
    {
        float x, average = 0;
        for (size_t i = 0; i < cp.link()->size(); i++)  // for all neighbor blocks
        {
            cp.dequeue(cp.link()->target(i).gid, x);    // dequeue the data received from this
                                                        // neighbor block in the last exchange
            average += x;
        }
        b->average = average / cp.link()->size();
    }
~~~~

## Getting Started

More information about getting started creating blocks, assigning them to processes, and decomposing a domain can be found in the [Initialization](initialization) page.
Complete examples of working programs can be found in the [Examples](examples) page.

## Topics

 - [Initialization](initialization)
 - [Domain decomposition](decomposition)
 - [Callback functions](callbacks)
 - Communication
    - [Local communication](local_comm)
    - [Remote communication](remote_comm)
    - [Global commrnication (reductions)](reduce_comm)
 - [Block I/O](io)
 - [Serialization](serialization)
 - [MPI convenience wrapper](mpi)
 - [Algorithms](algorithms)
 - [Writing out-of-core algorithms](ooc)
 - [Automatic block threading](threads)
 - [Examples](examples)

## Download

DIY is [available on Github](https://github.com/diatomic/diy), subject to a [variation of a
3-clause BSD license](https://github.com/diatomic/diy/blob/master/LICENSE.txt).  You can
download the [latest tarball](https://github.com/diatomic/diy/archive/master.tar.gz).

Authors
[Dmitriy Morozov](http://mrzv.org) and
[Tom Peterka](https://web.cels.anl.gov/~tpeterka/)


