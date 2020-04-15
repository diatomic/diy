#ifndef VTKMDIY_MPI_STATUS_HPP
#define VTKMDIY_MPI_STATUS_HPP

#include "config.hpp"
#include "datatypes.hpp"

namespace diy
{
namespace mpi
{
  struct status
  {
    VTKMDIY_MPI_EXPORT_FUNCTION int  source() const;
    VTKMDIY_MPI_EXPORT_FUNCTION int  tag() const;
    VTKMDIY_MPI_EXPORT_FUNCTION int  error() const;
    VTKMDIY_MPI_EXPORT_FUNCTION bool cancelled() const;
    VTKMDIY_MPI_EXPORT_FUNCTION int  count(const datatype& type) const;

    template<class T>
                   int count() const
    {
      return this->count(detail::get_mpi_datatype<T>());
    }

    DIY_MPI_Status handle;
  };

}
} // diy::mpi

#ifndef VTKMDIY_MPI_AS_LIB
#include "status.cpp"
#endif

#endif // VTKMDIY_MPI_STATUS_HPP
