DIY
===

DIY is a data-parallel out-of-core library. 

One partitions there data into blocks. The `diy::Master` owns these blocks. One may use the
[add()](@ref diy::Master::add) method to give a block to Master.  
Its main two methods are [foreach()](@ref diy::Master::foreach) and
[exchange()](@ref diy::Master::exchange),
which together support a [bulk-synchronous processing (BSP)](https://en.wikipedia.org/wiki/Bulk_synchronous_parallel) model of algorithm design.
[foreach(f)](@ref diy::Master::foreach) calls a function `f()` on every block.
The function is responsible for performing computation and scheduling communication using
[enqueue()](@ref diy::Master::Proxy::enqueue)/[dequeue()](@ref diy::Master::Proxy::dequeue)
operations. The actual communication is performed by
[exchange()](@ref diy::Master::exchange).

DIY is [available on Github](http://github.com/diatomic/diy2),
subject to a [variation of a 3-clause BSD license](https://github.com/diatomic/diy2/blob/master/LICENSE.txt).
You can download the [latest tarball](https://github.com/diatomic/diy2/archive/master.tar.gz).


Example
-------

~~~~{.cpp}
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
~~~~


Components
----------

 - \ref Initialization
 - \ref Serialization
 - \ref Decomposition
 - \ref Communication
 - \ref Callbacks

 - diy::Master
 - diy::Communicator
 - diy::Assigner
 - diy::FileStorage
 - [MPI wrapper](\ref MPI) for convenience.
 - IO

\authors [Dmitriy Morozov](http://mrzv.org)
\authors [Tom Peterka](http://www.mcs.anl.gov/~tpeterka/)

\namespace diy

All classes and functions are declared inside this namespace.
