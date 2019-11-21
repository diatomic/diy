#ifndef DIY_MPI_STATUS_HPP
#define DIY_MPI_STATUS_HPP

#include "config.hpp"
#include "datatypes.hpp"

namespace diy
{
namespace mpi
{
  struct status
  {
    DIY_MPI_EXPORT_FUNCTION int  source() const;
    DIY_MPI_EXPORT_FUNCTION int  tag() const;
    DIY_MPI_EXPORT_FUNCTION int  error() const;
    DIY_MPI_EXPORT_FUNCTION bool cancelled() const;
    DIY_MPI_EXPORT_FUNCTION int  count(const datatype& type) const;

    template<class T>
                   int count() const
    {
      return this->count(detail::get_mpi_datatype<T>());
    }

    DIY_MPI_Status handle;
  };

}
} // diy::mpi

#ifndef DIY_MPI_AS_LIB
#include "status.cpp"
#endif

#endif // DIY_MPI_STATUS_HPP
