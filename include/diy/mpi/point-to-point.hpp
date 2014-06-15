#include <vector>

namespace diy
{
namespace mpi
{
namespace detail
{
  // send
  template<class T, bool is_mpi_datatype_ = is_mpi_datatype<T>::value>
  struct send;

  template<class T>
  struct send<T, true>
  {
    void operator()(MPI_Comm comm, int dest, int tag, const T& x) const
    {
      typedef       mpi_datatype<T>     Datatype;
      MPI_Send(Datatype::address(x),
               Datatype::count(x),
               Datatype::datatype(),
               dest, tag, comm);
    }
  };

  // recv
  template<class T, bool is_mpi_datatype_ = is_mpi_datatype<T>::value>
  struct recv;

  template<class T>
  struct recv<T, true>
  {
    status operator()(MPI_Comm comm, int source, int tag, T& x) const
    {
      typedef       mpi_datatype<T>     Datatype;
      status s(Datatype::datatype());
      MPI_Recv(&x, 1, get_mpi_datatype<T>(), source, tag, comm, &s.s);
      return s;
    }
  };

  template<class U>
  struct recv<std::vector<U>, true>
  {
    status operator()(MPI_Comm comm, int source, int tag, std::vector<U>& x) const
    {
      int count;
      status s;

      MPI_Probe(source, tag, comm, &s.s);
      x.resize(s.count<U>());
      MPI_Recv(&x[0], x.size(), get_mpi_datatype<U>(), source, tag, comm, &s.s);
      return s;
    }
  };

  // isend
  template<class T, bool is_mpi_datatype_ = is_mpi_datatype<T>::value>
  struct isend;

  template<class T>
  struct isend<T, true>
  {
    request operator()(MPI_Comm comm, int dest, int tag, const T& x) const
    {
      request r;
      typedef       mpi_datatype<T>     Datatype;
      MPI_Isend(Datatype::address(x),
                Datatype::count(x),
                Datatype::datatype(),
                dest, tag, comm, &r.r);
      return r;
    }
  };

  // irecv
  template<class T, bool is_mpi_datatype_ = is_mpi_datatype<T>::value>
  struct irecv;

  template<class T>
  struct irecv<T, true>
  {
    request operator()(MPI_Comm comm, int source, int tag, T& x) const
    {
      request r;
      typedef       mpi_datatype<T>     Datatype;
      MPI_Irecv(Datatype::address(x),
                Datatype::count(x),
                Datatype::datatype(),
                source, tag, comm, &r.r);
      return r;
    }
  };
}
}
}
