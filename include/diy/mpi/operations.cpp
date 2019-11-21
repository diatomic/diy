#ifdef DIY_MPI_AS_LIB
#include "operations.hpp"
#endif

#include <functional>

namespace diy
{
namespace mpi
{

namespace detail
{

operation get_builtin_operation(BuiltinOperation id)
{
  operation op;
  switch(id)
  {
    case OP_MAXIMUM:     mpi_cast(op.handle) = MPI_MAX;  break;
    case OP_MINIMUM:     mpi_cast(op.handle) = MPI_MIN;  break;
    case OP_PLUS:        mpi_cast(op.handle) = MPI_SUM;  break;
    case OP_MULTIPLIES:  mpi_cast(op.handle) = MPI_PROD; break;
    case OP_LOGICAL_AND: mpi_cast(op.handle) = MPI_LAND; break;
    case OP_LOGICAL_OR:  mpi_cast(op.handle) = MPI_LOR;  break;
    default: break;
  }
  return op;
}

}
}
} // diy::mpi::detail
