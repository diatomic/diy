#include <vector>

namespace diy
{
namespace mpi
{
namespace detail
{
  template<class T> MPI_Datatype  get_mpi_datatype();
  template<>        MPI_Datatype  get_mpi_datatype<char>()      { return MPI_BYTE; }
  template<>        MPI_Datatype  get_mpi_datatype<int>()       { return MPI_INT; }
  template<>        MPI_Datatype  get_mpi_datatype<float>()     { return MPI_FLOAT; }

  /* is_mpi_datatype */
  template<class T>
  struct is_mpi_datatype
  { static const bool value; };

  template<>  const bool is_mpi_datatype<char>::value    = true;
  template<>  const bool is_mpi_datatype<int>::value     = true;
  template<>  const bool is_mpi_datatype<float>::value   = true;

  template<>  const bool is_mpi_datatype< std::vector<char> >::value    = true;
  template<>  const bool is_mpi_datatype< std::vector<int> >::value     = true;
  template<>  const bool is_mpi_datatype< std::vector<float> >::value   = true;


  /* mpi_datatype: helper routines, specialized for std::vector<...> */
  template<class T>
  struct mpi_datatype
  {
    static MPI_Datatype         datatype()              { return get_mpi_datatype<T>(); }
    static const void*          address(const T& x)     { return &x; }
    static void*                address(T& x)           { return &x; }
    static int                  count(const T& x)       { return 1; }
  };

  template<class U>
  struct mpi_datatype< std::vector<U> >
  {
    typedef     std::vector<U>      VecU;

    static MPI_Datatype         datatype()              { return get_mpi_datatype<U>(); }
    static const void*          address(const VecU& x)  { return &x[0]; }
    static void*                address(VecU& x)        { return &x[0]; }
    static int                  count(const VecU& x)    { return x.size(); }
  };

}
}
}
