# DIY is a data-parallel out-of core library

DIY2 is a data-parallel library for implementing scalable algorithms that can execute both
in-core and out-of-core. The same program can be executed with one or more threads per MPI
process, seamlessly combining distributed-memory message passing with shared-memory thread
parallelism.  The abstraction enabling these capabilities is block-based parallelism. The
program is structured into data-parallel blocks, which are the fundamental units of parallel
work and communication.  Blocks and their message queues are mapped onto processing elements
(MPI processes or threads) and are migrated between memory and storage by the DIY2
runtime. Complex communication patterns, including neighbor exchange, merge reduction, swap
reduction, and all-to-all exchange, are possible out-of-core in DIY2.

# Licensing

DIY is released as open source software under a BSD style [license](./LICENSE.txt).

# Installation

a. You can clone this repository, or

b. You can download the [latest tarball](https://github.com/diatomic/diy2/archive/master.tar.gz).

# Building

DIY2 is available as header files that do not need to be built, only included in your project. The examples can be build using cmake from the top level directory.

# Documentation

Available in [Doxygen](https://diatomic.github.io/diy2).

# Example

A simple DIY program looks like this:

```cpp
    struct Block { float local, average; };

    Master master(world);   // world = MPI_Comm
    ...                     // populate master with blocks
    master.foreach<Block>(&enqueue_local);
    master.exchange();
    master.foreach<Block>(&average);

    void enqueue_local(Block* b, const Proxy& cp, void* aux)
    {
        for (size_t i = 0; i < cp.link()->size(); i++)
            cp.enqueue(cp.link()->target(i), b->local);
    }

    void average(Block* b, const Proxy& cp, void* aux)
    {
        float x, average = 0;
        for (size_t i = 0; i < cp.link()->size(); i++)
        {
            cp.dequeue(cp.link()->target(i).gid, x);
            average += x;
        }
        b->average = average / cp.link()->size();
    }
```
