The block-parallel model in DIY passes messages between *blocks* rather than *processes*, as MPI does. There are many
advantages to *block parallelism* as opposed to *process parallelism*---flexible assignment of blocks to processes,
in/out-of-core block and message queue migration, block threading, and others---but sometimes you may still want to
perform some operation using process parallelism and send plain old MPI messages between processes instead of blocks
(even though we highly encourage you to think and write in terms of block parallelism and avoid the temptation to revert
to process parallelism).

It's possible to mix and match the two paradigms, and nothing prevents inserting plain old MPI calls
in a DIY program in addition to using DIY's block-parallel features. In such cases, one can use MPI's syntax, but we
recommend using DIY's wrapper around MPI instead.

DIY includes a simple C++ wrapper around the MPI commands. It borrows heavily
from [Boost.MPI](http://www.boost.org/doc/libs/1_56_0/doc/html/mpi.html) design.

~~~~{.cpp}
#include <diy/mpi.hpp>

int main(int argc, char* argv[])
{
  diy::mpi::environment     env(argc, argv);        // RAII
  diy::mpi::communicator    world;
  ...
}
~~~~

A simple wrapper `diy::mpi::environment` takes care of MPI initialization and
finalization using the RAII (resource acquisition is initialization) idiom. `diy::mpi::communicator` wraps an `MPI_Comm` object
and provides methods to `send()` and `recv()` as well as `isend()` and `irecv()`.

All functions map C++ types to the appropriate MPI datatypes, with a specialized
interface for an `std::vector` of them.

## Communicator

The following functions are members of `diy::mpi::communicator`.

- rank and size: `diy::mpi::rank()`, `diy::mpi::size()`
- point-to-point communication: `diy::mpi::send()`, `diy::mpi::recv()`, `diy::mpi::isend()`, `diy::mpi::issend()`, `diy::mpi::irecv()`
- probe: `diy::mpi::probe()`, `diy::mpi::iprobe()`
- barrier: `diy::mpi::barrier()`, `diy::mpi::ibarrier()`
- derive new communicator: `diy::mpi::split()`, `diy::mpi::duplicate()`

## Point-to-point communication

In addition to the above member functions of `diy::mpi::communicator`, the same point-to-point functions are available
if passing the `diy::mpi::communicator` as an argument.

- `diy::mpi::send()`, `diy::mpi::recv()`, `diy::mpi::isend()`, `diy::mpi::issend()`, `diy::mpi::irecv()`

## Collectives

 - `diy::mpi::broadcast()`, `diy::mpi::ibroadcast()`
 - `diy::mpi::gather()`, `diy::mpi::gather_v()`, `diy::mpi::all_gather()`, `diy::mpi::all_gather_v()`,
 - `diy::mpi::reduce()`, `diy::mpi::all_reduce()`, `diy::mpi::iall_reduce()`

~~~~{.cpp}
   int in = comm.rank() * 3;
   if (comm.rank() == 0)
   {
       int out;
       mpi::reduce(comm, in, out, std::plus<float>());
   } else
       mpi::reduce(comm, in, std::plus<float>());

~~~~

 - `diy::mpi::scan()`, `diy::mpi::all_to_all()`


## Operations

  class                   |     MPI operation
  ------------------------|---------------------
  `diy::mpi::maximum<U>`  |      ``MPI_MAX``
  `diy::mpi::minimum<U>`  |      ``MPI_MIN``
  `std::plus<U>`          |      ``MPI_SUM``
  `std::multiplies<U>`    |      ``MPI_PROD``
  `std::logical_and<U>`   |      ``MPI_LAND``
  `std::logical_or<U>`    |      ``MPI_LOR``

## Others

There are several other classes of MPI functions wrapped in DIY, including:

- status and request for nonblocking communication
- I/O
- one-sided windows

Please refer to the [DIY header files](https://github.com/diatomic/diy/tree/master/include/diy/mpi) for details.

