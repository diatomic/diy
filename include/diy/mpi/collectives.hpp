#include <vector>

#include "operations.hpp"

namespace diy
{
namespace mpi
{
  //!\addtogroup MPI
  //!@{

  template<class T, class Op>
  struct Collectives
  {
    typedef   detail::mpi_datatype<T>     Datatype;

    static void broadcast(const communicator& comm, T& x, int root)
    {
      MPI_Bcast(Datatype::address(x),
                Datatype::count(x),
                Datatype::datatype(), root, comm);
    }

    static void gather(const communicator& comm, const T& in, std::vector<T>& out, int root)
    {
      size_t s  = comm.size();
             s *= Datatype::count(in);
      out.resize(s);
      MPI_Gather(Datatype::address(in),
                 Datatype::count(in),
                 Datatype::datatype(),
                 Datatype::address(out[0]),
                 Datatype::count(in),
                 Datatype::datatype(),
                 root, comm);
    }

    static void gather(const communicator& comm, const std::vector<T>& in, std::vector< std::vector<T> >& out, int root)
    {
      std::vector<int>  counts(comm.size());
      Collectives<int,void*>::gather(comm, (int) in.size(), counts, root);

      std::vector<int>  offsets(comm.size(), 0);
      for (unsigned i = 1; i < offsets.size(); ++i)
        offsets[i] = offsets[i-1] + counts[i-1];

      std::vector<T> buffer(offsets.back() + counts.back());
      MPI_Gatherv(Datatype::address(in[0]),
                  in.size(),
                  Datatype::datatype(),
                  Datatype::address(buffer[0]),
                  &counts[0],
                  &offsets[0],
                  Datatype::datatype(),
                  root, comm);

      out.resize(comm.size());
      size_t cur = 0;
      for (unsigned i = 0; i < comm.size(); ++i)
      {
          out[i].reserve(counts[i]);
          for (unsigned j = 0; j < counts[i]; ++j)
              out[i].push_back(buffer[cur++]);
      }
    }

    static void gather(const communicator& comm, const T& in, int root)
    {
      MPI_Gather(Datatype::address(in),
                 Datatype::count(in),
                 Datatype::datatype(),
                 Datatype::address(const_cast<T&>(in)),
                 Datatype::count(in),
                 Datatype::datatype(),
                 root, comm);
    }

    static void gather(const communicator& comm, const std::vector<T>& in, int root)
    {
      Collectives<int,void*>::gather(comm, (int) in.size(), root);

      MPI_Gatherv(Datatype::address(in[0]),
                  in.size(),
                  Datatype::datatype(),
                  0, 0, 0,
                  Datatype::datatype(),
                  root, comm);
    }

    static void reduce(const communicator& comm, const T& in, T& out, int root, const Op&)
    {
      MPI_Reduce(Datatype::address(in),
                 Datatype::address(out),
                 Datatype::count(in),
                 Datatype::datatype(),
                 detail::mpi_op<Op>::get(),
                 root, comm);
    }

    static void reduce(const communicator& comm, const T& in, int root, const Op& op)
    {
      MPI_Reduce(Datatype::address(in),
                 Datatype::address(const_cast<T&>(in)),
                 Datatype::count(in),
                 Datatype::datatype(),
                 detail::mpi_op<Op>::get(),
                 root, comm);
    }

    static void all_reduce(const communicator& comm, const T& in, T& out, const Op&)
    {
      MPI_Allreduce(Datatype::address(in),
                    Datatype::address(out),
                    Datatype::count(in),
                    Datatype::datatype(),
                    detail::mpi_op<Op>::get(),
                    comm);
    }

    static void scan(const communicator& comm, const T& in, T& out, const Op&)
    {
      MPI_Scan(Datatype::address(in),
               Datatype::address(out),
               Datatype::count(in),
               Datatype::datatype(),
               detail::mpi_op<Op>::get(),
               comm);
    }

    static void all_to_all(const communicator& comm, const std::vector<T>& in, std::vector<T>& out, int n = 1)
    {
      // NB: this will fail if T is a vector
      MPI_Alltoall(Datatype::address(in[0]), n,
                   Datatype::datatype(),
                   Datatype::address(out[0]), n,
                   Datatype::datatype(),
                   comm);
    }
  };

  //! Broadcast to all processes in `comm`.
  template<class T>
  void      broadcast(const communicator& comm, T& x, int root)
  {
    Collectives<T,void*>::broadcast(comm, x, root);
  }

  //! Gather from all processes in `comm`.
  //!  On `root` process, `out` is resized to `comm.size()` and filled with
  //! elements from the respective ranks.
  template<class T>
  void      gather(const communicator& comm, const T& in, std::vector<T>& out, int root)
  {
    Collectives<T,void*>::gather(comm, in, out, root);
  }

  //! Same as above, but each process is expected to send the same number of
  //! elements, so out is resized to `comm.size() * in.size()` and the elements
  //! from different ranks appear in order (with the elements from the same rank
  //! arranged contiguously).
  template<class T>
  void      gather(const communicator& comm, const std::vector<T>& in, std::vector< std::vector<T> >& out, int root)
  {
    Collectives<T,void*>::gather(comm, in, out, root);
  }

  //! Simplified version (without `out`) for use on non-root processes.
  template<class T>
  void      gather(const communicator& comm, const T& in, int root)
  {
    Collectives<T,void*>::gather(comm, in, root);
  }

  //! Simplified version (without `out`) for use on non-root processes.
  template<class T>
  void      gather(const communicator& comm, const std::vector<T>& in, int root)
  {
    Collectives<T,void*>::gather(comm, in, root);
  }

  //! reduce
  template<class T, class Op>
  void      reduce(const communicator& comm, const T& in, T& out, int root, const Op& op)
  {
    Collectives<T, Op>::reduce(comm, in, out, root, op);
  }

  //! Simplified version (without `out`) for use on non-root processes.
  template<class T, class Op>
  void      reduce(const communicator& comm, const T& in, int root, const Op& op)
  {
    Collectives<T, Op>::reduce(comm, in, root, op);
  }

  //! all_reduce
  template<class T, class Op>
  void      all_reduce(const communicator& comm, const T& in, T& out, const Op& op)
  {
    Collectives<T, Op>::all_reduce(comm, in, out, op);
  }

  //! scan
  template<class T, class Op>
  void      scan(const communicator& comm, const T& in, T& out, const Op& op)
  {
    Collectives<T, Op>::scan(comm, in, out, op);
  }

  //! all_to_all
  template<class T>
  void      all_to_all(const communicator& comm, const std::vector<T>& in, std::vector<T>& out, int n = 1)
  {
    Collectives<T, void*>::all_to_all(comm, in, out, n);
  }

  //!@}
}
}
