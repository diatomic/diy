#include "diy/mpi.hpp"

template <typename MPIType, typename DIYClass>
struct TestConversions
{
  static void ExpectsMPIType(MPIType) {}
  static void ExpectsDIYClass(DIYClass) {}

  static void Run(MPIType mt = MPIType{})
  {
    // test initialization
    DIYClass dc1 = mt;

    // can we pass MPIType object where an object of DIYHandle is expected?
    ExpectsDIYClass(mt);

    // test conversion operator
    MPIType mt2 = dc1;
    static_cast<void>(mt2);

    // can we pass DIYHandle object where an object of MPIType is expected?
    ExpectsMPIType(dc1);
  }
};

int main()
{
  diy::mpi::environment env;

  TestConversions<MPI_Comm, diy::mpi::communicator>::Run(MPI_COMM_WORLD);
  TestConversions<MPI_Datatype, diy::mpi::datatype>::Run();
  TestConversions<MPI_Op, diy::mpi::operation>::Run();
  TestConversions<MPI_Status, diy::mpi::status>::Run();

  // This test is expected to pass or fail during compilation
  return 0;
}
