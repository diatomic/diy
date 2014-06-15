namespace diy
{
namespace mpi
{
  struct maximum {};
  struct minimum {};

namespace detail
{
  template<class T> struct mpi_op                           { static MPI_Op  get(); };
  template<>        struct mpi_op<maximum>                  { static MPI_Op  get() { return MPI_MAX; }  };
  template<>        struct mpi_op<minimum>                  { static MPI_Op  get() { return MPI_MIN; }  };
  template<class U> struct mpi_op< std::plus<U> >           { static MPI_Op  get() { return MPI_SUM; }  };
  template<class U> struct mpi_op< std::multiplies<U> >     { static MPI_Op  get() { return MPI_PROD; }  };
}
}
}
