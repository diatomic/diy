DIY
===

DIY is a block-parallel library for writing scalable distributed- and shared-memory parallel
algorithms that can run both in- and out-of-core. The same program can be executed with one
or more threads per MPI process and with one or more data blocks resident in main memory.  The
abstraction enabling these capabilities is block-parallelism; blocks and their message
queues are mapped onto processing elements (MPI processes or threads) and are migrated between
memory and storage by the DIY runtime. Complex communication patterns, including neighbor
exchange, merge reduction, swap reduction, and all-to-all exchange, are implemented in DIY.


DIY follows a bulk-synchronous processing (BSP) programming model. A simple program, shown
below, consists of the following components:

- `struct`s called blocks,
- the diy object called `master`,
- callback functions performed on each block by `master.foreach()`,
- message exchanges between the blocks by `master.exchange()`

`diy::Master` owns the blocks, put into it using the [add()](@ref diy::Master::add) method.
Its main two methods are [foreach()](@ref diy::Master::foreach) and [exchange()](@ref diy::Master::exchange).
[foreach(f)](@ref diy::Master::foreach) calls back a function `f()`
with every block.  The function is responsible for performing computation and scheduling
communication using [enqueue()](@ref diy::Master::Proxy::enqueue)/[dequeue()](@ref diy::Master::Proxy::dequeue) operations.
The actual communication is performed by [exchange()](@ref diy::Master::exchange).

Example
-------

The callback functions `enqueue_local()` and `average()` in the example below are given the
block pointer and a communication proxy for the message exchange between blocks. The callback
functions typically enqueue or dequeue messages from the proxy. In this way, information can be
received and sent during rounds of message exchange.

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

Getting Started
---------------

More information about getting started creating blocks, assigning them to processes, and decomposing a domain can be found in the [Initialization](\ref Initialization) page. Complete examples of working programs can be found in the [Examples](\ref Examples) page.

Components
----------

 - \ref Tutorial
 - \ref Serialization
 - \ref Decomposition
 - \ref Communication
 - \ref Callbacks
 - \ref Immediate
 - \ref IO
 - \ref Examples

 - diy::Master
 - diy::Communicator
 - diy::Assigner
 - diy::FileStorage
 - [MPI wrapper](\ref MPI) for convenience.

Download
--------

DIY is [available on Github](http://github.com/diatomic/diy), subject to a [variation of a
3-clause BSD license](https://github.com/diatomic/diy/blob/master/LICENSE.txt).  You can
download the [latest tarball](https://github.com/diatomic/diy/archive/master.tar.gz).

\authors [Dmitriy Morozov](http://mrzv.org)
\authors [Tom Peterka](http://www.mcs.anl.gov/~tpeterka/)

\namespace diy

All classes and functions are declared inside this namespace.
