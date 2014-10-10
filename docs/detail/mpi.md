\defgroup MPI

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
finalization using RAII. `diy::mpi::communicator` wraps an `MPI_Comm` object
and provides methods to
[send()](@ref diy::mpi::communicator::send) and [recv()](@ref diy::mpi::communicator::recv)
(as well as [isend()](@ref diy::mpi::communicator::isend)) and
[irecv()](@ref diy::mpi::communicator::irecv)).

All functions map C++ types to the appropriate MPI datatypes, with a specialized
interface for an `std::vector` of them.


Collectives
-----------

 - diy::mpi::broadcast()
 - diy::mpi::gather()
 - diy::mpi::reduce()
   ~~~~{.cpp}
   int in = comm.rank() * 3;
   if (comm.rank() == 0)
   {
       int out;
       mpi::reduce(comm, in, out, std::plus<float>());
   } else
       mpi::reduce(comm, in, std::plus<float>());

   ~~~~
 - diy::mpi::all_reduce()
 - diy::mpi::scan()
 - diy::mpi::all_to_all()


Operations
----------

  class                   |     MPI operation
  ------------------------|---------------------
  `diy::mpi::maximum<U>`  |      ``MPI_MAX``
  `diy::mpi::minimum<U>`  |      ``MPI_MIN``
  `std::plus<U>`          |      ``MPI_SUM``
  `std::multiplies<U>`    |      ``MPI_PROD``
  `std::logical_and<U>`   |      ``MPI_LAND``
  `std::logical_or<U>`    |      ``MPI_LOR``



\namespace diy::mpi

C++ MPI wrapper
