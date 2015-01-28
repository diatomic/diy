DIY
===

DIY is a data-parallel out-of-core library.

Components:

 - \ref Initialization
 - \ref Serialization
 - \ref Decomposition
 - \ref Communication

 - diy::Master
 - diy::Communicator
 - diy::Assigner
 - diy::FileStorage
 - [MPI wrapper](\ref MPI) for convenience.
 - IO


Example
-------

Add me.

Design
------

`diy::Master` owns the blocks, put into it using the
[add()](@ref diy::Master::add)
method.  Its main two methods are [foreach()](@ref diy::Master::foreach) and
[exchange()](@ref diy::Master::exchange).


\authors [Dmitriy Morozov](http://mrzv.org)
\authors [Tom Peterka](http://www.mcs.anl.gov/~tpeterka/)

\namespace diy

All classes and functions are declared inside this namespace.
