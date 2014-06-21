#include <vector>

namespace diy
{
namespace mpi
{
namespace detail
{
  template<class T> MPI_Datatype  get_mpi_datatype();

  /* is_mpi_datatype */
  template<class T>
  struct is_mpi_datatype
  { static const bool value; };

#define DIY_MPI_DATATYPE_MAP(type, mpi_type) \
  template<>  MPI_Datatype  get_mpi_datatype<type>()                        { return mpi_type; }  \
  template<>  const bool    is_mpi_datatype<type>::value                    = true;     \
  template<>  const bool    is_mpi_datatype< std::vector<type> >::value     = true;

  DIY_MPI_DATATYPE_MAP(char,                  MPI_BYTE);
  DIY_MPI_DATATYPE_MAP(bool,                  MPI_BYTE);
  DIY_MPI_DATATYPE_MAP(int,                   MPI_INT);
  //DIY_MPI_DATATYPE_MAP(unsigned,              MPI_UNSIGNED_INT);
  DIY_MPI_DATATYPE_MAP(long,                  MPI_LONG);
  DIY_MPI_DATATYPE_MAP(unsigned long,         MPI_UNSIGNED_LONG);
  DIY_MPI_DATATYPE_MAP(long long,             MPI_LONG_LONG_INT);
  //DIY_MPI_DATATYPE_MAP(unsigned long long,    MPI_UNSIGNED_LONG_LONG_INT);
  DIY_MPI_DATATYPE_MAP(float,                 MPI_FLOAT);
  DIY_MPI_DATATYPE_MAP(double,                MPI_DOUBLE);

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
