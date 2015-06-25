## DIY is a data-parallel out-of-core library

DIY2 is a data-parallel library for implementing scalable algorithms that can execute both
in-core and out-of-core. The same program can be executed with one or more threads per MPI
process, seamlessly combining distributed-memory message passing with shared-memory thread
parallelism.  The abstraction enabling these capabilities is block-based parallelism; blocks
and their message queues are mapped onto processing elements (MPI processes or threads) and are
migrated between memory and storage by the DIY2 runtime. Complex communication patterns,
including neighbor exchange, merge reduction, swap reduction, and all-to-all exchange, are
possible in- and out-of-core in DIY2.

## Licensing

DIY is released as open source software under a BSD style [license](./LICENSE.txt).

## Dependencies

DIY requires an MPI installation. We recommend MPICH:

```
wget http://www.mpich.org/static/downloads/3.1.4/mpich-3.1.4.tar.gz
tar -xvf mpich-3.1.4.tar.gz
cd mpich-3.1.4

./configure \
--prefix=/path/to/mpich-3.1.4/install \
--disable-fortran \
--enable-shared \
--enable-g=all \

make
make install
```

## Installation

a. You can clone this repository, or

b. You can download the [latest tarball](https://github.com/diatomic/diy2/archive/master.tar.gz).

## Building

DIY2 is available as header files that do not need to be built, only included in your project. The examples can be build using cmake from the top level directory.

## Documentation

Available in [Doxygen](https://diatomic.github.io/diy2).

## Example

A simple DIY program, shown below, consists of the following components:

- structs called blocks,
- a diy object called the master,
- a set of callback functions performed on each block by master.foreach(),
- optionally one or more message exchanges between the blocks by master.exchange(), and
- there may be other collectives and global reductions not shown below.

The callback functions (enqueue_block() and average() in the example below) are given the block
data and a communication proxy for the message exchange between blocks. It is usual for the
callback functions to enqueue or dequeue messages from the proxy, so that information can be
received and sent during rounds of message exchange.

```cpp
    struct Block { float local, average; };          // define your block structure

    Master master(world);                            // world = MPI_Comm
    ...                                              // populate master with blocks
    master.foreach<Block>(&enqueue_local);           // call enqueue_local() for each block
    master.exchange();                               // exchange enqueued data between blocks
    master.foreach<Block>(&average);                 // call average() for each block

    // enqueue block data prior to exchanging it
    void enqueue_local(Block* b,                     // one block
                       const Proxy& cp,              // communication proxy, i.e., the other blocks
                                                     // with which this block communicates
                       void* aux)                    // user-defined additional arguments
    {
        for (size_t i = 0; i < cp.link()->size(); i++)
            cp.enqueue(cp.link()->target(i), b->local);
    }

    // process received data after exchanging it, in this case compute its average
    void average(Block* b,                           // one block
                 const Proxy& cp,                    // communication proxy, i.e., the other blocks
                                                     // with which this block communicates
                 void* aux)                          // user-defined additional arguments
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
