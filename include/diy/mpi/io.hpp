#ifndef DIY_MPI_IO_HPP
#define DIY_MPI_IO_HPP

#include <vector>
#include <string>

namespace diy
{
namespace mpi
{
namespace io
{
  typedef               MPI_Offset              offset;

  class file
  {
    public:
      enum
      {
        rdonly          = MPI_MODE_RDONLY,
        rdwr            = MPI_MODE_RDWR,
        wronly          = MPI_MODE_WRONLY,
        create          = MPI_MODE_CREATE,
        exclusive       = MPI_MODE_EXCL,
        delete_on_close = MPI_MODE_DELETE_ON_CLOSE,
        unique_open     = MPI_MODE_UNIQUE_OPEN,
        sequential      = MPI_MODE_SEQUENTIAL,
        append          = MPI_MODE_APPEND
      };

    public:
                    file(const communicator&    comm,
                         const std::string&     filename,
                         int                    mode)       { MPI_File_open(comm, const_cast<char*>(filename.c_str()), mode, MPI_INFO_NULL, &fh); }
                    ~file()                                 { MPI_File_close(&fh); }

      template<class T>
      inline void   read_at(offset o, std::vector<T>& data);

      template<class T>
      inline void   read_at_all(offset o, std::vector<T>& data);

      template<class T>
      inline void   write_at(offset o, const std::vector<T>& data);

      template<class T>
      inline void   write_at_all(offset o, const std::vector<T>& data);

    private:
      MPI_File      fh;
  };
}
}
}

template<class T>
void
diy::mpi::io::file::
read_at(offset o, std::vector<T>& data)
{
  status s;
  MPI_File_read_at(fh, o, &data[0], data.size(), detail::get_mpi_datatype<T>(), &s.s);
}

template<class T>
void
diy::mpi::io::file::
read_at_all(offset o, std::vector<T>& data)
{
  status s;
  MPI_File_read_at_all(fh, o, &data[0], data.size(), detail::get_mpi_datatype<T>(), &s.s);
}

template<class T>
void
diy::mpi::io::file::
write_at(offset o, const std::vector<T>& data)
{
  status s;
  MPI_File_write_at(fh, o, &data[0], data.size(), detail::get_mpi_datatype<T>(), &s.s);
}

template<class T>
void
diy::mpi::io::file::
write_at_all(offset o, const std::vector<T>& data)
{

  status s;
  MPI_File_write_at_all(fh, o, &data[0], data.size(), detail::get_mpi_datatype<T>(), &s.s);
}

#endif
