#ifdef VTKMDIY_MPI_AS_LIB
#include "datatypes.hpp"
#endif

namespace diy
{
namespace mpi
{

namespace detail
{

#define DIY_MPI_DATATYPE_MAP(cpp_type, mpi_type)                                                  \
  template<>  datatype get_mpi_datatype<cpp_type>() {                                             \
    datatype dt;                                                                                  \
    mpi_cast(dt.handle) = mpi_type;                                                               \
    return dt;                                                                                    \
  }

  DIY_MPI_DATATYPE_MAP(char,                MPI_BYTE)
  DIY_MPI_DATATYPE_MAP(unsigned char,       MPI_BYTE)
  DIY_MPI_DATATYPE_MAP(bool,                MPI_BYTE)
  DIY_MPI_DATATYPE_MAP(int,                 MPI_INT)
  DIY_MPI_DATATYPE_MAP(unsigned,            MPI_UNSIGNED)
  DIY_MPI_DATATYPE_MAP(long,                MPI_LONG)
  DIY_MPI_DATATYPE_MAP(unsigned long,       MPI_UNSIGNED_LONG)
  DIY_MPI_DATATYPE_MAP(long long,           MPI_LONG_LONG_INT)
  DIY_MPI_DATATYPE_MAP(unsigned long long,  MPI_UNSIGNED_LONG_LONG)
  DIY_MPI_DATATYPE_MAP(float,               MPI_FLOAT)
  DIY_MPI_DATATYPE_MAP(double,              MPI_DOUBLE)

#undef DIY_MPI_DATATYPE_MAP

}
}
} // diy::mpi::detail
